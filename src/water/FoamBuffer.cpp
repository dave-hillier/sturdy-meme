#include "FoamBuffer.h"
#include "ShaderLoader.h"
#include "VulkanResourceFactory.h"
#include "DescriptorManager.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <array>
#include <cstring>

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
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "FoamBuffer requires raiiDevice");
        return false;
    }

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
    descriptorSetLayout_.reset();
    computePipeline_.reset();
    computePipelineLayout_.reset();
    sampler_.reset();

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
    for (int i = 0; i < 2; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R16_SFLOAT;
        imageInfo.extent = {resolution, resolution, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &foamBuffer[i], &foamAllocation[i], nullptr) != VK_SUCCESS) {
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = foamBuffer[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &foamBufferView[i]) != VK_SUCCESS) {
            return false;
        }
    }

    // Create sampler
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMipLodBias(0.0f)
        .setAnisotropyEnable(VK_FALSE)
        .setMaxAnisotropy(1.0f)
        .setCompareEnable(VK_FALSE)
        .setMinLod(0.0f)
        .setMaxLod(0.0f)
        .setBorderColor(vk::BorderColor::eFloatTransparentBlack);

    try {
        sampler_.emplace(*raiiDevice_, samplerInfo);
        return true;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create foam sampler: %s", e.what());
        return false;
    }
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
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};

    // Binding 0: Current foam buffer (storage image, read/write)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Previous foam buffer (sampled image, read)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Flow map (sampled image, for advection)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Wake sources uniform buffer (Phase 16)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout rawLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &rawLayout) != VK_SUCCESS) {
        return false;
    }
    descriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    // Push constant range
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(FoamPushConstants));

    // Pipeline layout
    try {
        vk::DescriptorSetLayout layouts[] = { **descriptorSetLayout_ };
        auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
            .setSetLayouts(layouts)
            .setPushConstantRanges(pushConstantRange);
        computePipelineLayout_.emplace(*raiiDevice_, pipelineLayoutInfo);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create foam pipeline layout: %s", e.what());
        return false;
    }

    // Load compute shader
    std::string shaderFile = shaderPath + "/foam_blur.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModule) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "FoamBuffer: Compute shader not found at %s", shaderFile.c_str());
        return true;  // Allow system to work without temporal foam
    }

    auto shaderStage = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(shaderStage)
        .setLayout(**computePipelineLayout_);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
        reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), nullptr, &rawPipeline);

    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (result == VK_SUCCESS) {
        computePipeline_.emplace(*raiiDevice_, rawPipeline);
    }

    return result == VK_SUCCESS;
}

bool FoamBuffer::createDescriptorSets() {
    // Create descriptor pool (need 2 sets for ping-pong, times frames in flight)
    uint32_t setCount = framesInFlight * 2;  // 2 for ping-pong per frame

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = setCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = setCount * 2;  // prev foam + flow map
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = setCount;  // wake uniform buffer

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = setCount;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        return false;
    }

    // Allocate descriptor sets (2 per frame for ping-pong)
    std::vector<VkDescriptorSetLayout> layouts(setCount, **descriptorSetLayout_);

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
    if (!computePipeline_) return;

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
        .writeImage(1, foamBufferView[readBuffer], **sampler_)
        .writeImage(2, flowMapView, flowMapSampler)
        .writeBuffer(3, wakeUniformBuffers_[frameIndex].get(), 0, sizeof(WakeUniformData))
        .update();

    // Transition write buffer to general layout
    vk::CommandBuffer vkCmd(cmd);
    auto writeBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eGeneral)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(foamBuffer[writeBuffer])
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, writeBarrier);

    // Transition read buffer to shader read if needed
    auto readBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(foamBuffer[readBuffer])
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                          {}, {}, {}, readBarrier);

    // Bind compute pipeline
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computePipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **computePipelineLayout_,
                             0, vk::DescriptorSet(descriptorSets[descSetIndex]), {});

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

    vkCmd.pushConstants<FoamPushConstants>(**computePipelineLayout_,
                                            vk::ShaderStageFlagBits::eCompute, 0, pushConstants);

    // Dispatch compute shader
    uint32_t groupSize = 16;
    uint32_t groupsX = (resolution + groupSize - 1) / groupSize;
    uint32_t groupsY = (resolution + groupSize - 1) / groupSize;
    vkCmd.dispatch(groupsX, groupsY, 1);

    // Transition write buffer to shader read for water shader sampling
    auto samplingBarrier = vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
        .setOldLayout(vk::ImageLayout::eGeneral)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(foamBuffer[writeBuffer])
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, samplingBarrier);

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
    vk::ClearColorValue clearValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
    vk::CommandBuffer vkCmd(cmd);

    for (int i = 0; i < 2; i++) {
        // Transition to transfer dst
        auto transferDstBarrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(foamBuffer[i])
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                              {}, {}, {}, transferDstBarrier);

        auto range = vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1);

        vkCmd.clearColorImage(foamBuffer[i], vk::ImageLayout::eTransferDstOptimal, clearValue, range);

        // Transition to shader read
        auto samplingBarrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(foamBuffer[i])
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                              {}, {}, {}, samplingBarrier);
    }

    currentBuffer = 0;
}
