#include "GrassTileManager.h"
#include "GrassSystem.h"  // For TiledGrassPushConstants
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

GrassTileManager::~GrassTileManager() {
    destroy();
}

bool GrassTileManager::init(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    framesInFlight_ = info.framesInFlight;

    computePipelineLayout_ = info.computePipelineLayout;
    computePipeline_ = info.computePipeline;

    // Initialize resource pool
    GrassTileResourcePool::InitInfo poolInfo{};
    poolInfo.device = info.device;
    poolInfo.descriptorPool = info.descriptorPool;
    poolInfo.framesInFlight = info.framesInFlight;
    poolInfo.computeDescriptorSetLayout = info.computeDescriptorSetLayout;

    if (!resourcePool_.init(poolInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GrassTileManager: Failed to initialize resource pool");
        return false;
    }

    // Configure load queue
    GrassTileLoadQueue::Config loadConfig{};
    loadConfig.maxLoadsPerFrame = info.maxLoadsPerFrame;
    loadConfig.teleportThreshold = info.teleportThreshold;
    loadConfig.clearOnTeleport = true;
    loadQueue_.setConfig(loadConfig);

    enabled_ = true;

    SDL_Log("GrassTileManager: Initialized with async loading (max %u tiles/frame)",
        info.maxLoadsPerFrame);

    return true;
}

void GrassTileManager::destroy() {
    resourcePool_.destroy();
    loadQueue_.clear();
    activeTiles_.clear();
    tileCreationTimes_.clear();
    enabled_ = false;
}

void GrassTileManager::updateActiveTiles(const glm::vec3& cameraPos, uint64_t frameNumber,
                                          float currentTime) {
    currentTime_ = currentTime;

    // Check for teleportation (clears load queue if detected)
    bool teleported = loadQueue_.updateCameraPosition(cameraPos);
    if (teleported) {
        SDL_Log("GrassTileManager: Teleport detected, clearing load queue");
    }

    // Reset frame budget for load queue
    loadQueue_.resetFrameBudget();

    // Update tracker to get load/unload requests
    auto result = tracker_.update(cameraPos, frameNumber, framesInFlight_);

    // Queue new load requests
    for (const auto& req : result.loadRequests) {
        loadQueue_.enqueue(req.coord, req.priority);
    }

    // Process unloads immediately (safe tiles only)
    processUnloads(result.unloadRequests);

    // Process load queue with frame budget
    processLoadQueue(currentTime);

    // Build active tiles list from loaded tiles
    activeTiles_.clear();
    for (const auto& coord : result.activeTiles) {
        if (resourcePool_.hasTileResources(coord)) {
            ActiveTileData data;
            data.coord = coord;
            data.creationTime = tileCreationTimes_.count(coord) ?
                                tileCreationTimes_[coord] : currentTime;
            activeTiles_.push_back(data);
        }
    }
}

void GrassTileManager::processLoadQueue(float currentTime) {
    auto tilesToLoad = loadQueue_.dequeueForFrame();

    for (const auto& coord : tilesToLoad) {
        // Allocate resources for tile
        if (resourcePool_.allocateForTile(coord)) {
            // Mark as loaded in tracker
            tracker_.markTileLoaded(coord, 0);  // Frame number updated in next update()

            // Store creation time for fade-in
            tileCreationTimes_[coord] = currentTime;

            SDL_Log("GrassTileManager: Loaded LOD%u tile at (%d, %d)",
                coord.lod, coord.x, coord.z);
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "GrassTileManager: Failed to load tile (%d, %d, LOD%u)",
                coord.x, coord.z, coord.lod);
        }
    }
}

void GrassTileManager::processUnloads(
    const std::vector<GrassTileTracker::TileRequest>& unloadRequests) {

    for (const auto& req : unloadRequests) {
        // Cancel pending load if any
        loadQueue_.cancel(req.coord);

        // Release resources
        resourcePool_.releaseForTile(req.coord);

        // Update tracker
        tracker_.markTileUnloaded(req.coord);

        // Remove creation time
        tileCreationTimes_.erase(req.coord);

        SDL_Log("GrassTileManager: Unloaded tile at (%d, %d, LOD%u)",
            req.coord.x, req.coord.z, req.coord.lod);
    }
}

void GrassTileManager::updateDescriptorSets(
    vk::ImageView terrainHeightMapView, vk::Sampler terrainHeightMapSampler,
    vk::ImageView displacementView, vk::Sampler displacementSampler,
    vk::ImageView tileArrayView, vk::Sampler tileSampler,
    const std::array<vk::Buffer, 3>& tileInfoBuffers,
    const std::vector<vk::Buffer>& cullingUniformBuffers,
    const std::vector<vk::Buffer>& grassParamsBuffers
) {
    // Update resource pool with shared resources
    resourcePool_.setSharedImages(
        terrainHeightMapView, terrainHeightMapSampler,
        displacementView, displacementSampler,
        tileArrayView, tileSampler
    );

    resourcePool_.setSharedBufferArrays(
        tileInfoBuffers,
        cullingUniformBuffers,
        grassParamsBuffers
    );

    // Note: Per-tile descriptor sets are updated in recordCompute
}

void GrassTileManager::setSharedBuffers(vk::Buffer sharedInstanceBuffer,
                                         vk::Buffer sharedIndirectBuffer) {
    sharedInstanceBuffer_ = sharedInstanceBuffer;
    sharedIndirectBuffer_ = sharedIndirectBuffer;
    resourcePool_.setSharedBuffers(sharedInstanceBuffer, sharedIndirectBuffer);
}

void GrassTileManager::recordCompute(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                                      uint32_t computeBufferSet) {
    if (!enabled_ || activeTiles_.empty()) return;

    // Reset the shared indirect buffer once at the start
    if (sharedIndirectBuffer_) {
        cmd.fillBuffer(sharedIndirectBuffer_, 0, sizeof(VkDrawIndirectCommand), 0);
        auto clearBarrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eComputeShader,
                            {}, clearBarrier, {}, {});
    }

    // Bind compute pipeline once
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline_);

    // Process each active tile
    for (const auto& tileData : activeTiles_) {
        vk::DescriptorSet descSet = resourcePool_.getDescriptorSet(tileData.coord, computeBufferSet);
        if (!descSet) continue;

        // Update per-frame bindings
        resourcePool_.writePerFrameBindings(tileData.coord, computeBufferSet);

        // Bind descriptor set
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                               computePipelineLayout_, 0,
                               descSet, {});

        // Calculate tile properties using LOD strategy
        const IGrassLODStrategy* strategy = tracker_.getLODStrategy();
        float tileSize = strategy->getTileSize(tileData.coord.lod);
        float spacingMult = strategy->getSpacingMultiplier(tileData.coord.lod);
        glm::vec2 tileOrigin(
            static_cast<float>(tileData.coord.x) * tileSize,
            static_cast<float>(tileData.coord.z) * tileSize
        );

        // Push constants
        TiledGrassPushConstants push{};
        push.time = time;
        push.tileOriginX = tileOrigin.x;
        push.tileOriginZ = tileOrigin.y;
        push.tileSize = tileSize;
        push.spacingMult = spacingMult;
        push.lodLevel = tileData.coord.lod;
        push.tileLoadTime = tileData.creationTime;
        push.padding = 0.0f;

        cmd.pushConstants(computePipelineLayout_,
                          vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(TiledGrassPushConstants), &push);

        // Dispatch compute for this tile
        cmd.dispatch(GrassConstants::TILE_DISPATCH_SIZE,
                     GrassConstants::TILE_DISPATCH_SIZE, 1);
    }

    // Memory barrier: compute write -> vertex shader read and indirect draw
    auto computeBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
                        {}, computeBarrier, {}, {});
}

void GrassTileManager::recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                                   uint32_t renderBufferSet,
                                   vk::Pipeline graphicsPipeline,
                                   vk::PipelineLayout graphicsPipelineLayout,
                                   vk::DescriptorSet graphicsDescriptorSet,
                                   vk::Buffer sharedIndirectBuffer,
                                   const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO) {
    if (!enabled_ || activeTiles_.empty()) return;

    // In tiled mode with shared buffers, we do a single draw call
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

    // Bind descriptor set with dynamic offset for renderer UBO
    if (dynamicRendererUBO && dynamicRendererUBO->isValid()) {
        uint32_t dynamicOffset = dynamicRendererUBO->getDynamicOffset(frameIndex);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               graphicsPipelineLayout, 0,
                               graphicsDescriptorSet, dynamicOffset);
    } else {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               graphicsPipelineLayout, 0,
                               graphicsDescriptorSet, {});
    }

    // Push constants
    TiledGrassPushConstants push{};
    push.time = time;
    push.tileOriginX = 0.0f;
    push.tileOriginZ = 0.0f;
    push.tileSize = GrassConstants::TILE_SIZE_LOD0;
    push.spacingMult = 1.0f;
    push.lodLevel = 0;
    push.tileLoadTime = 0.0f;
    push.padding = 0.0f;

    cmd.pushConstants(graphicsPipelineLayout,
                      vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(TiledGrassPushConstants), &push);

    // Single draw from shared indirect buffer
    cmd.drawIndirect(sharedIndirectBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}
