#include "FoamBuffer.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include "DescriptorManager.h"
#include <SDL3/SDL_log.h>
#include <array>
#include <cstring>

using namespace vk;

std::unique_ptr<FoamBuffer> FoamBuffer::create(const InitInfo& info) {
    std::unique_ptr<FoamBuffer> system(new FoamBuffer());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

FoamBuffer::~FoamBuffer() {
    cleanup();
}

bool FoamBuffer::initInternal(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    commandPool = info.commandPool;
    computeQueue = info.computeQueue;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    resolution = info.resolution;
    worldSize = info.worldSize;

    SDL_Log("FoamBuffer: Initializing with %dx%d resolution, %.1f world size",
            resolution, resolution, worldSize);

    if (!createFoamBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FoamBuffer: Failed to create foam buffers");
        return false;
    }

    if (!createWakeBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FoamBuffer: Failed to create wake buffers");
        return false;
    }

    if (!createComputePipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FoamBuffer: Failed to create compute pipeline");
        return false;
    }

    if (!createDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FoamBuffer: Failed to create descriptor sets");
        return false;
    }

    SDL_Log("FoamBuffer: Initialized successfully with wake system support");
    return true;
}

void FoamBuffer::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    // Destroy descriptor pool
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    // RAII wrappers handle cleanup automatically - just reset them
    descriptorSetLayout = ManagedDescriptorSetLayout();
    computePipeline = ManagedPipeline();
    computePipelineLayout = ManagedPipelineLayout();
    sampler = ManagedSampler();

    // Destroy foam buffers
    for (int i = 0; i < 2; i++) {
        if (foamBufferView[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, foamBufferView[i], nullptr);
            foamBufferView[i] = VK_NULL_HANDLE;
        }
        if (foamBuffer[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, foamBuffer[i], foamAllocation[i]);
            foamBuffer[i] = VK_NULL_HANDLE;
            foamAllocation[i] = VK_NULL_HANDLE;
        }
    }

    // Destroy wake uniform buffers (RAII-managed, destroyed automatically)
    wakeUniformBuffers_.clear();
    wakeUniformMapped.clear();

    SDL_Log("FoamBuffer: Destroyed");
}

bool FoamBuffer::createFoamBuffers() {
    // Create two foam buffers for ping-pong
    // Using Vulkan-Hpp type-safe structs
    for (int i = 0; i < 2; i++) {
        ImageCreateInfo imageInfo{
            {},                              // flags
            ImageType::e2D,
            Format::eR16Sfloat,
            Extent3D{resolution, resolution, 1},
            1,                               // mipLevels
            1,                               // arrayLayers
            SampleCountFlagBits::e1,
            ImageTiling::eOptimal,
            ImageUsageFlagBits::eStorage | ImageUsageFlagBits::eSampled | ImageUsageFlagBits::eTransferDst,
            SharingMode::eExclusive,
            0,                               // queueFamilyIndexCount
            nullptr,                         // pQueueFamilyIndices
            ImageLayout::eUndefined
        };

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        // VMA requires C-style struct, cast from vk:: to Vk
        auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        if (vmaCreateImage(allocator, &vkImageInfo, &allocInfo, &foamBuffer[i], &foamAllocation[i], nullptr) != VK_SUCCESS) {
            return false;
        }

        ImageViewCreateInfo viewInfo{
            {},                              // flags
            foamBuffer[i],
            ImageViewType::e2D,
            Format::eR16Sfloat,
            ComponentMapping{},              // identity swizzle
            ImageSubresourceRange{
                ImageAspectFlagBits::eColor,
                0,                           // baseMipLevel
                1,                           // levelCount
                0,                           // baseArrayLayer
                1                            // layerCount
            }
        };

        // vkCreateImageView requires C-style struct
        auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
        if (vkCreateImageView(device, &vkViewInfo, nullptr, &foamBufferView[i]) != VK_SUCCESS) {
            return false;
        }
    }

    // Create sampler
    SamplerCreateInfo samplerInfo{
        {},                                  // flags
        Filter::eLinear,                     // magFilter
        Filter::eLinear,                     // minFilter
        SamplerMipmapMode::eNearest,
        SamplerAddressMode::eClampToEdge,    // addressModeU
        SamplerAddressMode::eClampToEdge,    // addressModeV
        SamplerAddressMode::eClampToEdge,    // addressModeW
        0.0f,                                // mipLodBias
        VK_FALSE,                            // anisotropyEnable
        1.0f,                                // maxAnisotropy
        VK_FALSE,                            // compareEnable
        CompareOp::eNever,
        0.0f,                                // minLod
        0.0f,                                // maxLod
        BorderColor::eFloatTransparentBlack,
        VK_FALSE                             // unnormalizedCoordinates
    };

    return ManagedSampler::create(device, static_cast<VkSamplerCreateInfo>(samplerInfo), sampler);
}

bool FoamBuffer::createWakeBuffers() {
    // Create uniform buffers for wake data (one per frame in flight)
    wakeUniformBuffers_.resize(framesInFlight);
    wakeUniformMapped.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        if (!VulkanResourceFactory::createUniformBuffer(allocator, sizeof(WakeUniformData), wakeUniformBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create wake uniform buffer %u", i);
            return false;
        }

        wakeUniformMapped[i] = wakeUniformBuffers_[i].map();

        // Initialize to zero
        std::memset(wakeUniformMapped[i], 0, sizeof(WakeUniformData));
    }

    SDL_Log("FoamBuffer: Created %u wake uniform buffers", framesInFlight);
    return true;
}

bool FoamBuffer::createComputePipeline() {
    // Descriptor set layout
    std::array<DescriptorSetLayoutBinding, 4> bindings{{
        // Binding 0: Current foam buffer (storage image, read/write)
        {0, DescriptorType::eStorageImage, 1, ShaderStageFlagBits::eCompute},
        // Binding 1: Previous foam buffer (sampled image, read)
        {1, DescriptorType::eCombinedImageSampler, 1, ShaderStageFlagBits::eCompute},
        // Binding 2: Flow map (sampled image, for advection)
        {2, DescriptorType::eCombinedImageSampler, 1, ShaderStageFlagBits::eCompute},
        // Binding 3: Wake sources uniform buffer (Phase 16)
        {3, DescriptorType::eUniformBuffer, 1, ShaderStageFlagBits::eCompute}
    }};

    DescriptorSetLayoutCreateInfo layoutInfo{
        {},                                          // flags
        static_cast<uint32_t>(bindings.size()),
        bindings.data()
    };

    if (!ManagedDescriptorSetLayout::create(device, static_cast<VkDescriptorSetLayoutCreateInfo>(layoutInfo), descriptorSetLayout)) {
        return false;
    }

    // Push constant range
    PushConstantRange pushConstantRange{
        ShaderStageFlagBits::eCompute,
        0,                                           // offset
        sizeof(FoamPushConstants)
    };

    // Pipeline layout - use raw struct for VMA/raw Vulkan interop
    VkDescriptorSetLayout rawLayout = descriptorSetLayout.get();
    VkPushConstantRange vkPushConstantRange = static_cast<VkPushConstantRange>(pushConstantRange);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &rawLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &vkPushConstantRange;

    if (!ManagedPipelineLayout::create(device, pipelineLayoutInfo, computePipelineLayout)) {
        return false;
    }

    // Load compute shader
    std::string shaderFile = shaderPath + "/foam_blur.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModule) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "FoamBuffer: Compute shader not found at %s", shaderFile.c_str());
        return true;  // Allow system to work without temporal foam
    }

    // 
    PipelineShaderStageCreateInfo shaderStage{
        {},                                          // flags
        ShaderStageFlagBits::eCompute,
        *shaderModule,
        "main"
    };

    ComputePipelineCreateInfo pipelineInfo{
        {},                                          // flags
        static_cast<VkPipelineShaderStageCreateInfo>(shaderStage),
        computePipelineLayout.get()
    };

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    auto vkPipelineInfo = static_cast<VkComputePipelineCreateInfo>(pipelineInfo);
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &vkPipelineInfo, nullptr, &rawPipeline);

    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (result == VK_SUCCESS) {
        computePipeline = ManagedPipeline::fromRaw(device, rawPipeline);
    }

    return result == VK_SUCCESS;
}

bool FoamBuffer::createDescriptorSets() {
    // Create descriptor pool (need 2 sets for ping-pong, times frames in flight)
    uint32_t setCount = framesInFlight * 2;  // 2 for ping-pong per frame

    // 
    std::array<DescriptorPoolSize, 3> poolSizes{{
        {DescriptorType::eStorageImage, setCount},
        {DescriptorType::eCombinedImageSampler, setCount * 2},  // prev foam + flow map
        {DescriptorType::eUniformBuffer, setCount}              // wake uniform buffer
    }};

    DescriptorPoolCreateInfo poolInfo{
        {},                                          // flags
        setCount,                                    // maxSets
        static_cast<uint32_t>(poolSizes.size()),
        poolSizes.data()
    };

    auto vkPoolInfo = static_cast<VkDescriptorPoolCreateInfo>(poolInfo);
    if (vkCreateDescriptorPool(device, &vkPoolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        return false;
    }

    // Allocate descriptor sets (2 per frame for ping-pong) - use raw struct for Vulkan interop
    std::vector<VkDescriptorSetLayout> layouts(setCount, descriptorSetLayout.get());

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = setCount;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(setCount);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        return false;
    }

    // Note: We'll update the descriptor sets when recordCompute is called with flow map info
    return true;
}

void FoamBuffer::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex, float deltaTime,
                                VkImageView flowMapView, VkSampler flowMapSampler) {
    if (!computePipeline.get()) return;

    // Update wake uniform buffer for this frame
    if (!wakeUniformMapped.empty()) {
        std::memcpy(wakeUniformMapped[frameIndex], &wakeData, sizeof(WakeUniformData));
    }

    // Determine which buffers to use (ping-pong)
    int readBuffer = currentBuffer;
    int writeBuffer = 1 - currentBuffer;

    // Update descriptor sets for this frame's configuration
    uint32_t descSetIndex = frameIndex * 2 + writeBuffer;

    // Update descriptor set using SetWriter
    DescriptorManager::SetWriter(device, descriptorSets[descSetIndex])
        .writeStorageImage(0, foamBufferView[writeBuffer])
        .writeImage(1, foamBufferView[readBuffer], sampler.get())
        .writeImage(2, flowMapView, flowMapSampler)
        .writeBuffer(3, wakeUniformBuffers_[frameIndex].get(), 0, sizeof(WakeUniformData))
        .update();

    // Transition write buffer to general layout
    Barriers::prepareImageForCompute(cmd, foamBuffer[writeBuffer]);

    // Transition read buffer to shader read if needed
    Barriers::transitionImage(cmd, foamBuffer[readBuffer],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, VK_ACCESS_SHADER_READ_BIT);

    // Bind compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout.get(),
                           0, 1, &descriptorSets[descSetIndex], 0, nullptr);

    // Push constants
    FoamPushConstants pushConstants{};
    pushConstants.worldExtent = glm::vec4(worldCenter, glm::vec2(worldSize));
    pushConstants.deltaTime = deltaTime;
    pushConstants.blurStrength = blurStrength;
    pushConstants.decayRate = decayRate;
    pushConstants.injectionStrength = injectionStrength;
    pushConstants.wakeCount = wakeCount;
    pushConstants.padding[0] = 0.0f;
    pushConstants.padding[1] = 0.0f;
    pushConstants.padding[2] = 0.0f;

    vkCmdPushConstants(cmd, computePipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(pushConstants), &pushConstants);

    // Dispatch compute shader
    uint32_t groupSize = 16;
    uint32_t groupsX = (resolution + groupSize - 1) / groupSize;
    uint32_t groupsY = (resolution + groupSize - 1) / groupSize;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Transition write buffer to shader read for water shader sampling
    Barriers::imageComputeToSampling(cmd, foamBuffer[writeBuffer]);

    // Swap buffers for next frame
    currentBuffer = writeBuffer;

    // Clear wake sources after processing (they're per-frame)
    clearWakeSources();
}

void FoamBuffer::addWakeSource(const glm::vec2& position, const glm::vec2& velocity,
                                float radius, float intensity) {
    if (wakeCount >= MAX_WAKE_SOURCES) {
        return;  // Silently ignore if at capacity
    }

    WakeSource& wake = wakeData.sources[wakeCount];
    wake.position = position;
    wake.velocity = velocity;
    wake.radius = radius;
    wake.intensity = intensity;
    wake.wakeAngle = 0.3403f;  // Kelvin wake angle: 19.47 degrees in radians
    wake.padding = 0.0f;

    wakeCount++;
}

void FoamBuffer::addWake(const glm::vec2& position, float radius, float intensity) {
    // Simple wake without velocity - just a circular disturbance
    addWakeSource(position, glm::vec2(0.0f), radius, intensity);
}

void FoamBuffer::clearWakeSources() {
    wakeCount = 0;
    // No need to clear the data, wakeCount controls how many are used
}

void FoamBuffer::setWorldExtent(const glm::vec2& center, const glm::vec2& size) {
    worldCenter = center;
    worldSize = std::max(size.x, size.y);
}

void FoamBuffer::clear(VkCommandBuffer cmd) {
    // Clear both foam buffers to zero
    ClearColorValue clearValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

    for (int i = 0; i < 2; i++) {
        Barriers::prepareImageForTransferDst(cmd, foamBuffer[i]);

        ImageSubresourceRange range{
            ImageAspectFlagBits::eColor,
            0,  // baseMipLevel
            1,  // levelCount
            0,  // baseArrayLayer
            1   // layerCount
        };

        auto vkRange = static_cast<VkImageSubresourceRange>(range);
        auto vkClearValue = static_cast<VkClearColorValue>(clearValue);
        vkCmdClearColorImage(cmd, foamBuffer[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vkClearValue, 1, &vkRange);

        Barriers::imageTransferToSampling(cmd, foamBuffer[i]);
    }

    currentBuffer = 0;
}
