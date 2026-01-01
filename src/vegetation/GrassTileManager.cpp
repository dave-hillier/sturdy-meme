#include "GrassTileManager.h"
#include "GrassSystem.h"  // For TiledGrassPushConstants
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

GrassTileManager::~GrassTileManager() {
    destroy();
}

bool GrassTileManager::init(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    framesInFlight_ = info.framesInFlight;

    computeDescriptorSetLayout_ = info.computeDescriptorSetLayout;
    computePipelineLayout_ = info.computePipelineLayout;
    computePipeline_ = info.computePipeline;

    enabled_ = true;

    SDL_Log("GrassTileManager: Initialized with %u tiles per axis, %.1fm tile size",
        GrassConstants::TILES_PER_AXIS, GrassConstants::TILE_SIZE);

    return true;
}

void GrassTileManager::destroy() {
    // Descriptor sets are managed by the pool, no need to free individually
    tileDescriptorSets_.clear();
    activeTiles_.clear();
    tiles_.clear();

    enabled_ = false;
}

GrassTile::TileCoord GrassTileManager::worldToTileCoord(const glm::vec2& worldPos) const {
    // Floor division to get tile coordinate
    return {
        static_cast<int>(std::floor(worldPos.x / GrassConstants::TILE_SIZE)),
        static_cast<int>(std::floor(worldPos.y / GrassConstants::TILE_SIZE))
    };
}

GrassTile* GrassTileManager::getOrCreateTile(const GrassTile::TileCoord& coord) {
    auto it = tiles_.find(coord);
    if (it != tiles_.end()) {
        return it->second.get();
    }

    // Create new tile
    auto tile = std::make_unique<GrassTile>();
    if (!tile->init(allocator_, coord, framesInFlight_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GrassTileManager: Failed to create tile at (%d, %d)", coord.x, coord.z);
        return nullptr;
    }

    // Create descriptor sets for this tile
    GrassTile* tilePtr = tile.get();
    if (!createTileDescriptorSets(tilePtr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GrassTileManager: Failed to create descriptor sets for tile (%d, %d)",
            coord.x, coord.z);
        return nullptr;
    }

    tiles_[coord] = std::move(tile);
    return tilePtr;
}

bool GrassTileManager::createTileDescriptorSets(GrassTile* tile) {
    if (!descriptorPool_ || !computeDescriptorSetLayout_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GrassTileManager: Cannot create descriptor sets - pool or layout not set");
        return false;
    }

    // Allocate descriptor sets for all buffer sets
    auto rawSets = descriptorPool_->allocate(computeDescriptorSetLayout_, framesInFlight_);
    if (rawSets.empty()) {
        return false;
    }

    std::vector<vk::DescriptorSet> sets(rawSets.size());
    for (size_t i = 0; i < rawSets.size(); ++i) {
        sets[i] = rawSets[i];
    }

    tileDescriptorSets_[tile] = std::move(sets);
    return true;
}

void GrassTileManager::updateActiveTiles(const glm::vec3& cameraPos, uint64_t frameNumber) {
    glm::vec2 cameraXZ(cameraPos.x, cameraPos.z);
    GrassTile::TileCoord cameraTile = worldToTileCoord(cameraXZ);
    currentFrame_ = frameNumber;

    // Clear active tiles list
    activeTiles_.clear();

    // Calculate tile range around camera
    int halfExtent = static_cast<int>(GrassConstants::TILES_PER_AXIS) / 2;

    // Load tiles in a grid around the camera
    for (int dz = -halfExtent; dz <= halfExtent; ++dz) {
        for (int dx = -halfExtent; dx <= halfExtent; ++dx) {
            GrassTile::TileCoord coord{
                cameraTile.x + dx,
                cameraTile.z + dz
            };

            GrassTile* tile = getOrCreateTile(coord);
            if (tile) {
                tile->markUsed(frameNumber);
                activeTiles_.push_back(tile);
            }
        }
    }

    // Update current camera tile
    currentCameraTile_ = cameraTile;

    // Sort tiles by distance to camera (render closest first)
    std::sort(activeTiles_.begin(), activeTiles_.end(),
        [&cameraXZ](const GrassTile* a, const GrassTile* b) {
            return a->distanceSquaredTo(cameraXZ) < b->distanceSquaredTo(cameraXZ);
        });

    // Unload distant tiles that are safe to release
    unloadDistantTiles(cameraXZ, frameNumber);
}

void GrassTileManager::unloadDistantTiles(const glm::vec2& cameraXZ, uint64_t currentFrame) {
    // Calculate unload distance threshold with hysteresis
    // Active range is halfExtent tiles, unload beyond that + margin
    float halfExtent = static_cast<float>(GrassConstants::TILES_PER_AXIS) / 2.0f;
    float activeRadius = (halfExtent + 0.5f) * GrassConstants::TILE_SIZE;
    float unloadRadius = activeRadius + GrassConstants::TILE_UNLOAD_MARGIN;
    float unloadRadiusSq = unloadRadius * unloadRadius;

    // Collect tiles to unload (can't modify map while iterating)
    std::vector<GrassTile::TileCoord> tilesToUnload;

    for (auto& [coord, tile] : tiles_) {
        float distSq = tile->distanceSquaredTo(cameraXZ);

        // Only unload if beyond unload radius AND safe (not in use by GPU)
        if (distSq > unloadRadiusSq && tile->canUnload(currentFrame, framesInFlight_)) {
            tilesToUnload.push_back(coord);
        }
    }

    // Unload collected tiles
    for (const auto& coord : tilesToUnload) {
        auto it = tiles_.find(coord);
        if (it != tiles_.end()) {
            GrassTile* tilePtr = it->second.get();

            // Remove descriptor sets for this tile
            tileDescriptorSets_.erase(tilePtr);

            // Log unload for debugging
            SDL_Log("GrassTileManager: Unloading tile at (%d, %d)", coord.x, coord.z);

            // Remove from tile map (destructor will clean up GPU resources)
            tiles_.erase(it);
        }
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
    // Store shared resources
    terrainHeightMapView_ = terrainHeightMapView;
    terrainHeightMapSampler_ = terrainHeightMapSampler;
    displacementView_ = displacementView;
    displacementSampler_ = displacementSampler;
    tileArrayView_ = tileArrayView;
    tileSampler_ = tileSampler;
    tileInfoBuffers_ = tileInfoBuffers;
    cullingUniformBuffers_ = cullingUniformBuffers;
    grassParamsBuffers_ = grassParamsBuffers;

    // Update all existing tile descriptor sets
    for (auto& [coord, tile] : tiles_) {
        for (uint32_t setIndex = 0; setIndex < framesInFlight_; ++setIndex) {
            updateTileDescriptorSets(tile.get(), setIndex);
        }
    }
}

void GrassTileManager::setSharedBuffers(vk::Buffer sharedInstanceBuffer, vk::Buffer sharedIndirectBuffer) {
    sharedInstanceBuffer_ = sharedInstanceBuffer;
    sharedIndirectBuffer_ = sharedIndirectBuffer;
}

void GrassTileManager::updateTileDescriptorSets(GrassTile* tile, uint32_t bufferSetIndex) {
    auto it = tileDescriptorSets_.find(tile);
    if (it == tileDescriptorSets_.end()) return;

    vk::DescriptorSet descSet = it->second[bufferSetIndex];

    // Use non-fluent pattern to avoid copy semantics issues
    DescriptorManager::SetWriter writer(device_, descSet);

    // Binding 0: Shared instance buffer (all tiles write to the same buffer)
    // Size: MAX_INSTANCES * sizeof(GrassInstance) = 100000 * 48 = 4800000 bytes
    constexpr VkDeviceSize instanceSize = 48;
    if (sharedInstanceBuffer_) {
        writer.writeBuffer(0, sharedInstanceBuffer_, 0,
                           instanceSize * GrassConstants::MAX_INSTANCES,
                           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    // Binding 1: Shared indirect buffer
    if (sharedIndirectBuffer_) {
        writer.writeBuffer(1, sharedIndirectBuffer_, 0,
                           sizeof(VkDrawIndirectCommand),
                           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    // Binding 2: Culling uniforms (shared, per-frame)
    // Use frame 0 for initial setup - will be updated per-frame in recordCompute
    if (!cullingUniformBuffers_.empty()) {
        writer.writeBuffer(2, cullingUniformBuffers_[0], 0,
                           sizeof(CullingUniforms));
    }

    // Binding 3: Terrain heightmap (shared)
    if (terrainHeightMapView_) {
        writer.writeImage(3, terrainHeightMapView_, terrainHeightMapSampler_);
    }

    // Binding 4: Displacement map (shared)
    if (displacementView_) {
        writer.writeImage(4, displacementView_, displacementSampler_);
    }

    // Binding 5: Tile array (shared)
    if (tileArrayView_) {
        writer.writeImage(5, tileArrayView_, tileSampler_);
    }

    // Binding 6: Tile info buffer (shared, per-frame)
    if (!tileInfoBuffers_.empty() && tileInfoBuffers_[0]) {
        writer.writeBuffer(6, tileInfoBuffers_[0], 0, VK_WHOLE_SIZE,
                           VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    // Binding 7: GrassParams (shared, per-frame)
    if (!grassParamsBuffers_.empty()) {
        writer.writeBuffer(7, grassParamsBuffers_[0], 0,
                           sizeof(GrassParams));
    }

    writer.update();
}

void GrassTileManager::recordCompute(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                                      uint32_t computeBufferSet) {
    if (!enabled_ || activeTiles_.empty()) return;

    // Reset the shared indirect buffer once at the start (not per-tile)
    if (sharedIndirectBuffer_) {
        Barriers::clearBufferForComputeReadWrite(cmd,
            sharedIndirectBuffer_, 0, sizeof(VkDrawIndirectCommand));
    }

    // Bind compute pipeline once
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline_);

    // Process each active tile
    for (GrassTile* tile : activeTiles_) {
        auto it = tileDescriptorSets_.find(tile);
        if (it == tileDescriptorSets_.end()) continue;

        vk::DescriptorSet descSet = it->second[computeBufferSet];

        // Update per-frame bindings (culling uniforms, tile info, grass params)
        // Also update shared buffers if they've changed
        DescriptorManager::SetWriter writer(device_, descSet);

        // Binding 0: Shared instance buffer
        constexpr VkDeviceSize instanceSize = 48;
        if (sharedInstanceBuffer_) {
            writer.writeBuffer(0, sharedInstanceBuffer_, 0,
                               instanceSize * GrassConstants::MAX_INSTANCES,
                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        // Binding 1: Shared indirect buffer
        if (sharedIndirectBuffer_) {
            writer.writeBuffer(1, sharedIndirectBuffer_, 0,
                               sizeof(VkDrawIndirectCommand),
                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        if (!cullingUniformBuffers_.empty() && frameIndex < cullingUniformBuffers_.size()) {
            writer.writeBuffer(2, cullingUniformBuffers_[frameIndex], 0,
                               sizeof(CullingUniforms));
        }

        if (!tileInfoBuffers_.empty() && frameIndex < tileInfoBuffers_.size() &&
            tileInfoBuffers_[frameIndex]) {
            writer.writeBuffer(6, tileInfoBuffers_[frameIndex], 0, VK_WHOLE_SIZE,
                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        if (!grassParamsBuffers_.empty() && frameIndex < grassParamsBuffers_.size()) {
            writer.writeBuffer(7, grassParamsBuffers_[frameIndex], 0,
                               sizeof(GrassParams));
        }

        writer.update();

        // Bind descriptor set for this tile
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                               computePipelineLayout_, 0,
                               descSet, {});

        // Push constants with tile origin
        glm::vec2 tileOrigin = tile->getWorldOrigin();
        TiledGrassPushConstants push{};
        push.time = time;
        push.tileOriginX = tileOrigin.x;
        push.tileOriginZ = tileOrigin.y;

        cmd.pushConstants(computePipelineLayout_,
                          vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(TiledGrassPushConstants), &push);

        // Dispatch compute for this tile
        cmd.dispatch(GrassConstants::TILE_DISPATCH_SIZE,
                     GrassConstants::TILE_DISPATCH_SIZE, 1);
    }

    // Memory barrier: compute write -> vertex shader read and indirect draw
    Barriers::computeToVertexAndIndirectDraw(cmd);
}

void GrassTileManager::recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                                   uint32_t renderBufferSet,
                                   vk::Pipeline graphicsPipeline,
                                   vk::PipelineLayout graphicsPipelineLayout,
                                   vk::DescriptorSet graphicsDescriptorSet,
                                   vk::Buffer sharedIndirectBuffer,
                                   const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO) {
    if (!enabled_ || activeTiles_.empty()) return;

    // In tiled mode with shared buffers, we just do a single draw call
    // All tiles wrote to the same shared instance/indirect buffers

    // Bind graphics pipeline
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

    // Push constants (tile origin is 0,0 since all tiles wrote to shared buffer)
    TiledGrassPushConstants push{};
    push.time = time;
    push.tileOriginX = 0.0f;
    push.tileOriginZ = 0.0f;

    cmd.pushConstants(graphicsPipelineLayout,
                      vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(TiledGrassPushConstants), &push);

    // Single draw from shared indirect buffer
    cmd.drawIndirect(sharedIndirectBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}
