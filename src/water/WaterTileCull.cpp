#include "WaterTileCull.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstring>

bool WaterTileCull::init(const InitInfo& info) {
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

void WaterTileCull::destroy() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    if (computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }
    if (computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
        computePipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (depthSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, depthSampler, nullptr);
        depthSampler = VK_NULL_HANDLE;
    }

    if (tileBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, tileBuffer, tileAllocation);
        tileBuffer = VK_NULL_HANDLE;
    }
    if (counterBuffer != VK_NULL_HANDLE) {
        // Note: Do NOT call vmaUnmapMemory here - the buffer was created with
        // VMA_ALLOCATION_CREATE_MAPPED_BIT, so VMA manages the mapping automatically
        vmaDestroyBuffer(allocator, counterBuffer, counterAllocation);
        counterBuffer = VK_NULL_HANDLE;
        counterMapped = nullptr;
    }
    if (counterReadbackBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, counterReadbackBuffer, counterReadbackAllocation);
        counterReadbackBuffer = VK_NULL_HANDLE;
        counterReadbackAllocation = VK_NULL_HANDLE;
        counterReadbackMapped = nullptr;
    }
    if (indirectDrawBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, indirectDrawBuffer, indirectDrawAllocation);
        indirectDrawBuffer = VK_NULL_HANDLE;
    }

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

        // Destroy and recreate buffers
        if (tileBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, tileBuffer, tileAllocation);
            tileBuffer = VK_NULL_HANDLE;
        }
        if (indirectDrawBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, indirectDrawBuffer, indirectDrawAllocation);
            indirectDrawBuffer = VK_NULL_HANDLE;
        }

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
    VkBufferCreateInfo tileBufferInfo{};
    tileBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    tileBufferInfo.size = maxTiles * sizeof(TileData);
    tileBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateBuffer(allocator, &tileBufferInfo, &allocInfo,
                        &tileBuffer, &tileAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile buffer");
        return false;
    }

    // Counter buffer - atomic counter for visible tile count
    VkBufferCreateInfo counterBufferInfo{};
    counterBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    counterBufferInfo.size = sizeof(uint32_t) * framesInFlight;
    counterBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo counterAllocInfo{};
    counterAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    counterAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo mapInfo;
    if (vmaCreateBuffer(allocator, &counterBufferInfo, &counterAllocInfo,
                        &counterBuffer, &counterAllocation, &mapInfo) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create counter buffer");
        return false;
    }
    counterMapped = mapInfo.pMappedData;

    // Initialize counter to non-zero so water renders on first frames
    // (visibility will be properly computed after first tile cull pass)
    uint32_t* counters = static_cast<uint32_t*>(counterMapped);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        counters[i] = 1;  // Assume visible initially
    }

    // Counter readback buffer (host-visible)
    VkBufferCreateInfo counterReadbackInfo{};
    counterReadbackInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    counterReadbackInfo.size = sizeof(uint32_t) * framesInFlight;
    counterReadbackInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo counterReadbackAllocInfo{};
    counterReadbackAllocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    counterReadbackAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo readbackInfo;
    if (vmaCreateBuffer(allocator, &counterReadbackInfo, &counterReadbackAllocInfo,
                        &counterReadbackBuffer, &counterReadbackAllocation, &readbackInfo) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create counter readback buffer");
        return false;
    }
    counterReadbackMapped = readbackInfo.pMappedData;

    uint32_t* readbackPtr = static_cast<uint32_t*>(counterReadbackMapped);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        readbackPtr[i] = counters[i];
    }

    // Indirect draw buffer
    VkBufferCreateInfo indirectBufferInfo{};
    indirectBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indirectBufferInfo.size = sizeof(IndirectDrawCommand);
    indirectBufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (vmaCreateBuffer(allocator, &indirectBufferInfo, &allocInfo,
                        &indirectDrawBuffer, &indirectDrawAllocation, nullptr) != VK_SUCCESS) {
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

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull descriptor set layout");
        return false;
    }

    // Push constant range
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TileCullPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull pipeline layout");
        return false;
    }

    // Load compute shader
    std::string shaderFile = shaderPath + "/water_tile_cull.comp.spv";
    VkShaderModule shaderModule = ShaderLoader::loadShaderModule(device, shaderFile);
    if (shaderModule == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load tile cull compute shader: %s", shaderFile.c_str());
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = computePipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr, &computePipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tile cull compute pipeline");
        return false;
    }

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

    if (vkCreateSampler(device, &samplerInfo, nullptr, &depthSampler) != VK_SUCCESS) {
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

    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, descriptorSetLayout);

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
    vmaFlushAllocation(allocator, counterAllocation, frameIndex * sizeof(uint32_t), sizeof(uint32_t));

    // Update descriptor set with depth texture and storage buffers
    VkDescriptorImageInfo depthImageInfo{};
    depthImageInfo.sampler = depthSampler;
    depthImageInfo.imageView = depthView;
    depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo tileBufferInfo{};
    tileBufferInfo.buffer = tileBuffer;
    tileBufferInfo.offset = 0;
    tileBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo counterBufferInfo{};
    counterBufferInfo.buffer = counterBuffer;
    counterBufferInfo.offset = 0;
    counterBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo indirectBufferInfo{};
    indirectBufferInfo.buffer = indirectDrawBuffer;
    indirectBufferInfo.offset = 0;
    indirectBufferInfo.range = sizeof(IndirectDrawCommand);

    std::array<VkWriteDescriptorSet, 4> writes{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSets[frameIndex];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &depthImageInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSets[frameIndex];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &tileBufferInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptorSets[frameIndex];
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &counterBufferInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descriptorSets[frameIndex];
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].pBufferInfo = &indirectBufferInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            computePipelineLayout, 0, 1, &descriptorSets[frameIndex], 0, nullptr);
    vkCmdPushConstants(cmd, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(TileCullPushConstants), &pc);

    // Dispatch one thread per tile
    uint32_t groupsX = (tileCount.x + 7) / 8;
    uint32_t groupsY = (tileCount.y + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    barrierCullResultsForDrawAndTransfer(cmd, frameIndex);

    // Copy the counter value for this frame to the host-visible readback buffer
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = frameIndex * sizeof(uint32_t);
    copyRegion.dstOffset = frameIndex * sizeof(uint32_t);
    copyRegion.size = sizeof(uint32_t);
    vkCmdCopyBuffer(cmd, counterBuffer, counterReadbackBuffer, 1, &copyRegion);

    barrierCounterForHostRead(cmd, frameIndex);
}

void WaterTileCull::barrierCullResultsForDrawAndTransfer(VkCommandBuffer cmd, uint32_t frameIndex) {
    Barriers::BarrierBatch batch(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
    batch.bufferBarrier(counterBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                        frameIndex * sizeof(uint32_t), sizeof(uint32_t));
    batch.bufferBarrier(tileBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    batch.bufferBarrier(indirectDrawBuffer, VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_INDIRECT_COMMAND_READ_BIT, 0, sizeof(IndirectDrawCommand));
}

void WaterTileCull::barrierCounterForHostRead(VkCommandBuffer cmd, uint32_t frameIndex) {
    Barriers::BarrierBatch batch(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
    batch.bufferBarrier(counterReadbackBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
                        frameIndex * sizeof(uint32_t), sizeof(uint32_t));
}

uint32_t WaterTileCull::getVisibleTileCount(uint32_t frameIndex) const {
    if (counterReadbackMapped == nullptr) return 0;

    VkResult invalidateResult = vmaInvalidateAllocation(allocator, counterReadbackAllocation,
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
