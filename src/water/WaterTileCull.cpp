#include "WaterTileCull.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include "DescriptorManager.h"
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
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    // RAII wrappers handle cleanup automatically - just reset them
    computePipeline = ManagedPipeline();
    computePipelineLayout = ManagedPipelineLayout();
    descriptorSetLayout = ManagedDescriptorSetLayout();
    depthSampler = ManagedSampler();

    // ManagedBuffer cleanup (RAII handles via reset)
    tileBuffer_.reset();
    counterBuffer_.reset();
    counterMapped = nullptr;
    counterReadbackBuffer_.reset();
    counterReadbackMapped = nullptr;
    indirectDrawBuffer_.reset();

    device = VK_NULL_HANDLE;
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

        // Recreate descriptor sets
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
        }
        createDescriptorSets();
    }

    SDL_Log("WaterTileCull resized: %dx%d tiles", tileCount.x, tileCount.y);
}

bool WaterTileCull::createBuffers() {
    uint32_t maxTiles = tileCount.x * tileCount.y;

    // Tile buffer - stores visibility data for each tile
    if (!VulkanResourceFactory::createStorageBuffer(allocator, maxTiles * sizeof(TileData), tileBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile buffer");
        return false;
    }

    // Counter buffer - atomic counter for visible tile count (CPU-to-GPU, mapped)
    VkDeviceSize counterSize = sizeof(uint32_t) * framesInFlight;
    if (!VulkanResourceFactory::createStorageBufferHostReadable(allocator, counterSize, counterBuffer_)) {
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
    if (!VulkanResourceFactory::createReadbackBuffer(allocator, counterSize, counterReadbackBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create counter readback buffer");
        return false;
    }
    counterReadbackMapped = counterReadbackBuffer_.map();

    uint32_t* readbackPtr = static_cast<uint32_t*>(counterReadbackMapped);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        readbackPtr[i] = counters[i];
    }

    // Indirect draw buffer
    if (!VulkanResourceFactory::createIndirectBuffer(allocator, sizeof(IndirectDrawCommand), indirectDrawBuffer_)) {
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

    auto makeComputeBinding = [](uint32_t binding, VkDescriptorType type) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        return b;
    };

    std::array<VkDescriptorSetLayoutBinding, 4> bindings = {
        makeComputeBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        makeComputeBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        makeComputeBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        makeComputeBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (!ManagedDescriptorSetLayout::create(device, layoutInfo, descriptorSetLayout)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull descriptor set layout");
        return false;
    }

    // Push constant range
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TileCullPushConstants);

    VkDescriptorSetLayout rawLayout = descriptorSetLayout.get();
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &rawLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (!ManagedPipelineLayout::create(device, pipelineLayoutInfo, computePipelineLayout)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull pipeline layout");
        return false;
    }

    // Load compute shader
    std::string shaderFile = shaderPath + "/water_tile_cull.comp.spv";
    auto shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load tile cull compute shader: %s", shaderFile.c_str());
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = *shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = computePipelineLayout.get();

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr, &rawPipeline);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull compute pipeline");
        return false;
    }

    computePipeline = ManagedPipeline::fromRaw(device, rawPipeline);

    SDL_Log("WaterTileCull compute pipeline created");

    // Create depth sampler for tile culling
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (!ManagedSampler::create(device, samplerInfo, depthSampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull depth sampler");
        return false;
    }

    return true;
}

bool WaterTileCull::createDescriptorSets() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = framesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = framesInFlight * 3;  // tile, counter, indirect

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = framesInFlight;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull descriptor pool");
        return false;
    }

    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, descriptorSetLayout.get());

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = framesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(framesInFlight);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate tile cull descriptor sets");
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
        .writeImage(0, depthView, depthSampler.get())
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

    // Bind pipeline
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline.get());
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout.get(),
                             0, vk::DescriptorSet(descriptorSets[frameIndex]), {});
    vkCmd.pushConstants<TileCullPushConstants>(computePipelineLayout.get(),
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
    Barriers::BarrierBatch batch(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
    batch.bufferBarrier(counterBuffer_.get(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                        frameIndex * sizeof(uint32_t), sizeof(uint32_t));
    batch.bufferBarrier(tileBuffer_.get(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    batch.bufferBarrier(indirectDrawBuffer_.get(), VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_INDIRECT_COMMAND_READ_BIT, 0, sizeof(IndirectDrawCommand));
}

void WaterTileCull::barrierCounterForHostRead(VkCommandBuffer cmd, uint32_t frameIndex) {
    Barriers::BarrierBatch batch(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
    batch.bufferBarrier(counterReadbackBuffer_.get(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
                        frameIndex * sizeof(uint32_t), sizeof(uint32_t));
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
