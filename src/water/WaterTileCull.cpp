#include "WaterTileCull.h"
#include "ShaderLoader.h"
#include "VmaResources.h"
#include "DescriptorManager.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <array>
#include <cstring>

std::unique_ptr<WaterTileCull> WaterTileCull::create(const InitInfo& info) {
    std::unique_ptr<WaterTileCull> system(new WaterTileCull());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

WaterTileCull::~WaterTileCull() {
    cleanup();
}

bool WaterTileCull::initInternal(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    commandPool = info.commandPool;
    computeQueue = info.computeQueue;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    extent = info.extent;
    tileSize = info.tileSize;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterTileCull: raiiDevice is required");
        return false;
    }

    // Calculate tile count
    tileCount.x = (extent.width + tileSize - 1) / tileSize;
    tileCount.y = (extent.height + tileSize - 1) / tileSize;

    if (!createBuffers()) return false;
    if (!createComputePipeline()) return false;
    if (!createDescriptorSets()) return false;

    SDL_Log("WaterTileCull initialized: %dx%d tiles (%u px each)",
            tileCount.x, tileCount.y, tileSize);
    return true;
}

void WaterTileCull::cleanup() {
    if (!raiiDevice_) return;

    raiiDevice_->waitIdle();

    // RAII wrappers handle cleanup automatically - just reset them
    descriptorPool_.reset();
    computePipeline_.reset();
    computePipelineLayout_.reset();
    descriptorSetLayout_.reset();
    depthSampler_.reset();

    // ManagedBuffer cleanup (RAII handles via reset)
    tileBuffer_.reset();
    counterBuffer_.reset();
    counterMapped = nullptr;
    counterReadbackBuffer_.reset();
    counterReadbackMapped = nullptr;
    indirectDrawBuffer_.reset();

    device = VK_NULL_HANDLE;
    raiiDevice_ = nullptr;
}

void WaterTileCull::resize(VkExtent2D newExtent) {
    if (newExtent.width == extent.width && newExtent.height == extent.height) {
        return;
    }

    extent = newExtent;

    // Recalculate tile count
    glm::uvec2 newTileCount;
    newTileCount.x = (extent.width + tileSize - 1) / tileSize;
    newTileCount.y = (extent.height + tileSize - 1) / tileSize;

    // Only recreate buffers if tile count changed
    if (newTileCount != tileCount) {
        tileCount = newTileCount;

        // Destroy and recreate buffers (RAII via reset)
        tileBuffer_.reset();
        indirectDrawBuffer_.reset();

        createBuffers();

        // Recreate descriptor sets (RAII handles cleanup)
        descriptorPool_.reset();
        createDescriptorSets();
    }

    SDL_Log("WaterTileCull resized: %dx%d tiles", tileCount.x, tileCount.y);
}

bool WaterTileCull::createBuffers() {
    uint32_t maxTiles = tileCount.x * tileCount.y;

    // Tile buffer - stores visibility data for each tile
    if (!VmaBufferFactory::createStorageBuffer(allocator, maxTiles * sizeof(TileData), tileBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile buffer");
        return false;
    }

    // Counter buffer - atomic counter for visible tile count (CPU-to-GPU, mapped)
    VkDeviceSize counterSize = sizeof(uint32_t) * framesInFlight;
    if (!VmaBufferFactory::createStorageBufferHostReadable(allocator, counterSize, counterBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create counter buffer");
        return false;
    }
    counterMapped = counterBuffer_.map();

    // Initialize counter to non-zero so water renders on first frames
    uint32_t* counters = static_cast<uint32_t*>(counterMapped);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        counters[i] = 1;  // Assume visible initially
    }

    // Counter readback buffer (host-visible)
    if (!VmaBufferFactory::createReadbackBuffer(allocator, counterSize, counterReadbackBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create counter readback buffer");
        return false;
    }
    counterReadbackMapped = counterReadbackBuffer_.map();

    uint32_t* readbackPtr = static_cast<uint32_t*>(counterReadbackMapped);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        readbackPtr[i] = counters[i];
    }

    // Indirect draw buffer
    if (!VmaBufferFactory::createIndirectBuffer(allocator, sizeof(IndirectDrawCommand), indirectDrawBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect draw buffer");
        return false;
    }

    SDL_Log("WaterTileCull buffers created for %u tiles", maxTiles);
    return true;
}

bool WaterTileCull::createComputePipeline() {
    // Descriptor set layout:
    // 0: Depth buffer (sampler2D)
    // 1: Tile output buffer (storage)
    // 2: Counter buffer (storage)
    // 3: Indirect draw buffer (storage)

    auto makeComputeBinding = [](uint32_t binding, vk::DescriptorType type) {
        return vk::DescriptorSetLayoutBinding{}
            .setBinding(binding)
            .setDescriptorType(type)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eCompute);
    };

    std::array<vk::DescriptorSetLayoutBinding, 4> bindings = {
        makeComputeBinding(0, vk::DescriptorType::eCombinedImageSampler),
        makeComputeBinding(1, vk::DescriptorType::eStorageBuffer),
        makeComputeBinding(2, vk::DescriptorType::eStorageBuffer),
        makeComputeBinding(3, vk::DescriptorType::eStorageBuffer)
    };

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    try {
        descriptorSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull descriptor set layout: %s", e.what());
        return false;
    }

    // Push constant range
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(TileCullPushConstants));

    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(**descriptorSetLayout_)
        .setPushConstantRanges(pushConstantRange);

    try {
        computePipelineLayout_.emplace(*raiiDevice_, pipelineLayoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull pipeline layout: %s", e.what());
        return false;
    }

    if (!ComputePipelineBuilder(*raiiDevice_)
            .setShader(shaderPath + "/water_tile_cull.comp.spv")
            .setPipelineLayout(**computePipelineLayout_)
            .buildInto(computePipeline_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull compute pipeline");
        return false;
    }

    SDL_Log("WaterTileCull compute pipeline created");

    // Create depth sampler for tile culling
    auto sampler = SamplerFactory::createSamplerNearestClamp(*raiiDevice_);
    if (!sampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull depth sampler");
        return false;
    }
    depthSampler_ = std::move(*sampler);

    return true;
}

bool WaterTileCull::createDescriptorSets() {
    std::array<vk::DescriptorPoolSize, 2> poolSizes = {
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(framesInFlight),
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(framesInFlight * 3)  // tile, counter, indirect
    };

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setPoolSizes(poolSizes)
        .setMaxSets(framesInFlight);

    try {
        descriptorPool_.emplace(*raiiDevice_, poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull descriptor pool: %s", e.what());
        return false;
    }

    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, **descriptorSetLayout_);

    auto allocInfo = vk::DescriptorSetAllocateInfo{}
        .setDescriptorPool(**descriptorPool_)
        .setSetLayouts(layouts);

    try {
        auto allocatedSets = vk::Device(device).allocateDescriptorSets(allocInfo);
        descriptorSets.resize(framesInFlight);
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            descriptorSets[i] = allocatedSets[i];
        }
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate tile cull descriptor sets: %s", e.what());
        return false;
    }

    // Note: Depth texture binding is updated in recordTileCull
    return true;
}

void WaterTileCull::recordTileCull(VkCommandBuffer cmd, uint32_t frameIndex,
                                    const glm::mat4& viewProj,
                                    const glm::vec3& cameraPos,
                                    float waterLevel,
                                    VkImageView depthView) {
    if (!enabled || descriptorSets.empty()) {
        return;
    }

    // Reset counter for this frame
    uint32_t* counterPtr = static_cast<uint32_t*>(counterMapped) + frameIndex;
    *counterPtr = 0;
    vmaFlushAllocation(counterBuffer_.allocator(), counterBuffer_.getAllocation(),
                       frameIndex * sizeof(uint32_t), sizeof(uint32_t));

    // Update descriptor set with depth texture and storage buffers
    DescriptorManager::SetWriter(device, descriptorSets[frameIndex])
        .writeImage(0, depthView, **depthSampler_)
        .writeBuffer(1, tileBuffer_.get(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeBuffer(2, counterBuffer_.get(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .writeBuffer(3, indirectDrawBuffer_.get(), 0, sizeof(IndirectDrawCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .update();

    // Push constants
    TileCullPushConstants pc{};
    pc.viewProjMatrix = viewProj;
    pc.waterPlane = glm::vec4(0.0f, 1.0f, 0.0f, -waterLevel);  // Y-up plane at waterLevel
    pc.cameraPos = glm::vec4(cameraPos, 1.0f);
    pc.screenSize = glm::uvec2(extent.width, extent.height);
    pc.tileCount = tileCount;
    pc.waterLevel = waterLevel;
    pc.tileSize = static_cast<float>(tileSize);
    pc.nearPlane = 0.1f;
    pc.farPlane = 1000.0f;
    pc.maxTiles = tileCount.x * tileCount.y;  // Output buffer capacity
    pc._pad0 = 0;

    // Bind pipeline
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computePipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **computePipelineLayout_,
                             0, vk::DescriptorSet(descriptorSets[frameIndex]), {});
    vkCmd.pushConstants<TileCullPushConstants>(**computePipelineLayout_,
                                                vk::ShaderStageFlagBits::eCompute, 0, pc);

    // Dispatch one thread per tile
    uint32_t groupsX = (tileCount.x + 7) / 8;
    uint32_t groupsY = (tileCount.y + 7) / 8;
    vkCmd.dispatch(groupsX, groupsY, 1);

    barrierCullResultsForDrawAndTransfer(cmd, frameIndex);

    // Copy the counter value for this frame to the host-visible readback buffer
    auto copyRegion = vk::BufferCopy{}
        .setSrcOffset(frameIndex * sizeof(uint32_t))
        .setDstOffset(frameIndex * sizeof(uint32_t))
        .setSize(sizeof(uint32_t));
    vkCmd.copyBuffer(counterBuffer_.get(), counterReadbackBuffer_.get(), copyRegion);

    barrierCounterForHostRead(cmd, frameIndex);
}

void WaterTileCull::barrierCullResultsForDrawAndTransfer(VkCommandBuffer cmd, uint32_t frameIndex) {
    vk::CommandBuffer vkCmd(cmd);

    std::array<vk::BufferMemoryBarrier, 3> barriers = {
        vk::BufferMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(counterBuffer_.get())
            .setOffset(frameIndex * sizeof(uint32_t))
            .setSize(sizeof(uint32_t)),
        vk::BufferMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(tileBuffer_.get())
            .setOffset(0)
            .setSize(VK_WHOLE_SIZE),
        vk::BufferMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eIndirectCommandRead)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setBuffer(indirectDrawBuffer_.get())
            .setOffset(0)
            .setSize(sizeof(IndirectDrawCommand))
    };

    vkCmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eDrawIndirect,
        {}, {}, barriers, {});
}

void WaterTileCull::barrierCounterForHostRead(VkCommandBuffer cmd, uint32_t frameIndex) {
    vk::CommandBuffer vkCmd(cmd);

    auto barrier = vk::BufferMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eHostRead)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setBuffer(counterReadbackBuffer_.get())
        .setOffset(frameIndex * sizeof(uint32_t))
        .setSize(sizeof(uint32_t));

    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
                          {}, {}, barrier, {});
}

uint32_t WaterTileCull::getVisibleTileCount(uint32_t frameIndex) const {
    if (counterReadbackMapped == nullptr) return 0;

    VkResult invalidateResult = vmaInvalidateAllocation(counterReadbackBuffer_.allocator(),
                                                        counterReadbackBuffer_.getAllocation(),
                                                        frameIndex * sizeof(uint32_t), sizeof(uint32_t));
    if (invalidateResult != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to invalidate counter readback allocation");
        return 0;
    }

    const uint32_t* counterPtr = static_cast<const uint32_t*>(counterReadbackMapped) + frameIndex;
    return *counterPtr;
}

bool WaterTileCull::wasWaterVisibleLastFrame(uint32_t /*currentFrameIndex*/) const {
    // Use CPU-side absolute frame tracking to avoid double-buffer aliasing issues.
    // The per-frame-index readback buffers have 2-frame latency which caused
    // alternating visibility flickering.
    //
    // Instead, we track if water was visible in any recent frame and provide
    // a grace period to handle transient occlusion without popping.
    return (currentAbsoluteFrame <= lastVisibleFrame + VISIBILITY_GRACE_FRAMES);
}

void WaterTileCull::endFrame(uint32_t frameIndex) {
    // Increment absolute frame counter
    currentAbsoluteFrame++;

    // Check if water was visible this frame using the per-frame-index readback
    // This data is from the tile cull that just ran, which will be available
    // after GPU sync (fence wait at start of next frame using this index)
    uint32_t visibleTiles = getVisibleTileCount(frameIndex);
    if (visibleTiles > 0) {
        lastVisibleFrame = currentAbsoluteFrame;
    }
}
