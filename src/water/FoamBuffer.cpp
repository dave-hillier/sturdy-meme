#include "FoamBuffer.h"
#include "ShaderLoader.h"
#include "VmaBufferFactory.h"
#include "SamplerFactory.h"
#include "DescriptorManager.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/vulkan/BarrierHelpers.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <array>
#include <cstring>

std::unique_ptr<FoamBuffer> FoamBuffer::create(const InitInfo& info) {
    auto system = std::make_unique<FoamBuffer>(ConstructToken{});
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
    if (!raiiDevice_) return;

    raiiDevice_->waitIdle();

    // RAII wrappers handle cleanup automatically - just reset them
    descriptorPool_.reset();
    descriptorSetLayout_.reset();
    computePipeline_.reset();
    computePipelineLayout_.reset();
    sampler_.reset();

    // Destroy foam buffers
    for (int i = 0; i < 2; i++) {
        foamBufferView_[i].reset();
        if (foamBuffer[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, foamBuffer[i], foamAllocation[i]);
            foamBuffer[i] = VK_NULL_HANDLE;
            foamAllocation[i] = VK_NULL_HANDLE;
        }
    }

    // Destroy wake uniform buffers (RAII-managed, destroyed automatically)
    wakeUniformBuffers_.clear();
    wakeUniformMapped.clear();

    device = VK_NULL_HANDLE;
    raiiDevice_ = nullptr;
    SDL_Log("FoamBuffer: Destroyed");
}

bool FoamBuffer::createFoamBuffers() {
    // Create two foam buffers for ping-pong
    for (int i = 0; i < 2; i++) {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(resolution, resolution)
                .setFormat(VK_FORMAT_R16_SFLOAT)
                .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .setGpuOnly()
                .build(*raiiDevice_, image, foamBufferView_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create foam buffer %d", i);
            return false;
        }
        image.releaseToRaw(foamBuffer[i], foamAllocation[i]);
    }

    // Create sampler using factory
    sampler_ = SamplerFactory::createSamplerLinearClampLimitedMip(*raiiDevice_, 0.0f);
    if (!sampler_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create foam sampler");
        return false;
    }
    return true;
}

bool FoamBuffer::createWakeBuffers() {
    // Create uniform buffers for wake data (one per frame in flight)
    wakeUniformBuffers_.resize(framesInFlight);
    wakeUniformMapped.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        if (!VmaBufferFactory::createUniformBuffer(allocator, sizeof(WakeUniformData), wakeUniformBuffers_[i])) {
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
    std::array<vk::DescriptorSetLayoutBinding, 4> bindings = {
        // Binding 0: Current foam buffer (storage image, read/write)
        vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eStorageImage)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute),
        // Binding 1: Previous foam buffer (sampled image, read)
        vk::DescriptorSetLayoutBinding{}
            .setBinding(1)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute),
        // Binding 2: Flow map (sampled image, for advection)
        vk::DescriptorSetLayoutBinding{}
            .setBinding(2)
            .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute),
        // Binding 3: Wake sources uniform buffer (Phase 16)
        vk::DescriptorSetLayoutBinding{}
            .setBinding(3)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute)
    };

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    try {
        descriptorSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create foam descriptor set layout: %s", e.what());
        return false;
    }

    // Pipeline layout using builder
    if (!PipelineLayoutBuilder(*raiiDevice_)
            .addDescriptorSetLayout(**descriptorSetLayout_)
            .addPushConstantRange<FoamPushConstants>(vk::ShaderStageFlagBits::eCompute)
            .buildInto(computePipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create foam pipeline layout");
        return false;
    }

    // Create compute pipeline - allow failure since system works without temporal foam
    if (!ComputePipelineBuilder(*raiiDevice_)
            .setShader(shaderPath + "/foam_blur.comp.spv")
            .setPipelineLayout(**computePipelineLayout_)
            .buildInto(computePipeline_)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "FoamBuffer: Failed to create compute pipeline, temporal foam disabled");
        return true;  // Allow system to work without temporal foam
    }

    return true;
}

bool FoamBuffer::createDescriptorSets() {
    // Create descriptor pool (need 2 sets for ping-pong, times frames in flight)
    uint32_t setCount = framesInFlight * 2;  // 2 for ping-pong per frame

    std::array<vk::DescriptorPoolSize, 3> poolSizes = {
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eStorageImage)
            .setDescriptorCount(setCount),
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(setCount * 2),  // prev foam + flow map
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(setCount)  // wake uniform buffer
    };

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setPoolSizes(poolSizes)
        .setMaxSets(setCount);

    try {
        descriptorPool_.emplace(*raiiDevice_, poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create foam descriptor pool: %s", e.what());
        return false;
    }

    // Allocate descriptor sets (2 per frame for ping-pong)
    std::vector<vk::DescriptorSetLayout> layouts(setCount, **descriptorSetLayout_);

    auto allocInfo = vk::DescriptorSetAllocateInfo{}
        .setDescriptorPool(**descriptorPool_)
        .setSetLayouts(layouts);

    try {
        auto allocatedSets = vk::Device(device).allocateDescriptorSets(allocInfo);
        descriptorSets.resize(setCount);
        for (uint32_t i = 0; i < setCount; ++i) {
            descriptorSets[i] = allocatedSets[i];
        }
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate foam descriptor sets: %s", e.what());
        return false;
    }

    // Note: We'll update the descriptor sets when recordCompute is called with flow map info
    return true;
}

void FoamBuffer::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex, float deltaTime,
                                VkImageView flowMapView, VkSampler flowMapSampler) {
    if (!computePipeline_) return;

    // Update wake uniform buffer for this frame
    // Bounds check: frameIndex must be within range, not just non-empty
    if (frameIndex < wakeUniformMapped.size()) {
        std::memcpy(wakeUniformMapped[frameIndex], &wakeData, sizeof(WakeUniformData));
    }

    // Determine which buffers to use (ping-pong)
    int readBuffer = currentBuffer;
    int writeBuffer = 1 - currentBuffer;

    // Update descriptor sets for this frame's configuration
    uint32_t descSetIndex = frameIndex * 2 + writeBuffer;

    // Update descriptor set using SetWriter
    DescriptorManager::SetWriter(device, descriptorSets[descSetIndex])
        .writeStorageImage(0, **foamBufferView_[writeBuffer])
        .writeImage(1, **foamBufferView_[readBuffer], **sampler_)
        .writeImage(2, flowMapView, flowMapSampler)
        .writeBuffer(3, wakeUniformBuffers_[frameIndex].get(), 0, sizeof(WakeUniformData))
        .update();

    // Transition write buffer to general layout
    vk::CommandBuffer vkCmd(cmd);
    BarrierHelpers::imageToGeneral(vkCmd, foamBuffer[writeBuffer]);

    // Transition read buffer to shader read for compute sampling
    BarrierHelpers::transitionImageLayout(vkCmd, foamBuffer[readBuffer],
        vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eComputeShader,
        {}, vk::AccessFlagBits::eShaderRead);

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
    BarrierHelpers::imageToShaderRead(vkCmd, foamBuffer[writeBuffer]);

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

    auto range = vk::ImageSubresourceRange{}
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setBaseMipLevel(0)
        .setLevelCount(1)
        .setBaseArrayLayer(0)
        .setLayerCount(1);

    for (int i = 0; i < 2; i++) {
        // Transition to transfer dst
        BarrierHelpers::transitionImageLayout(vkCmd, foamBuffer[i],
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
            {}, vk::AccessFlagBits::eTransferWrite);

        vkCmd.clearColorImage(foamBuffer[i], vk::ImageLayout::eTransferDstOptimal, clearValue, range);

        // Transition to shader read
        BarrierHelpers::transitionImageLayout(vkCmd, foamBuffer[i],
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);
    }

    currentBuffer = 0;
}
