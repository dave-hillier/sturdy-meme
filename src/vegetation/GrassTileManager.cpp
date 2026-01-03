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

GrassTile::TileCoord GrassTileManager::worldToTileCoord(const glm::vec2& worldPos, uint32_t lod) const {
    // Floor division to get tile coordinate based on LOD tile size
    float tileSize = GrassConstants::getTileSizeForLod(lod);
    return {
        static_cast<int>(std::floor(worldPos.x / tileSize)),
        static_cast<int>(std::floor(worldPos.y / tileSize)),
        lod
    };
}

GrassTile* GrassTileManager::getOrCreateTile(const GrassTile::TileCoord& coord) {
    auto it = tiles_.find(coord);
    if (it != tiles_.end()) {
        return it->second.get();
    }

    // Create new tile (tiles are lightweight - shared buffers in manager)
    auto tile = std::make_unique<GrassTile>();
    tile->init(coord, currentTime_);

    SDL_Log("GrassTileManager: Created LOD%u tile at (%d, %d), world origin (%.1f, %.1f), size %.0fm",
        coord.lod, coord.x, coord.z, tile->getWorldOrigin().x, tile->getWorldOrigin().y,
        tile->getTileSize());

    // Create descriptor sets for this tile
    GrassTile* tilePtr = tile.get();
    if (!createTileDescriptorSets(tilePtr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GrassTileManager: Failed to create descriptor sets for tile (%d, %d)",
            coord.x, coord.z);
        return nullptr;
    }

    // Write descriptor sets with shared resources (terrain, displacement, etc.)
    for (uint32_t setIndex = 0; setIndex < framesInFlight_; ++setIndex) {
        updateTileDescriptorSets(tilePtr, setIndex);
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

bool GrassTileManager::isCoveredByHigherLod(const glm::vec2& worldPos, uint32_t currentLod,
                                             const glm::vec2& cameraXZ) const {
    // Check if this world position falls within the active tile range of any higher LOD level
    for (uint32_t higherLod = 0; higherLod < currentLod; ++higherLod) {
        float tileSize = GrassConstants::getTileSizeForLod(higherLod);
        uint32_t tilesPerAxis = (higherLod == 0) ? GrassConstants::TILES_PER_AXIS_LOD0 :
                                (higherLod == 1) ? GrassConstants::TILES_PER_AXIS_LOD1 :
                                                   GrassConstants::TILES_PER_AXIS_LOD2;
        int halfExtent = static_cast<int>(tilesPerAxis) / 2;

        // Calculate the range covered by higher LOD tiles
        GrassTile::TileCoord cameraTile = worldToTileCoord(cameraXZ, higherLod);
        float minX = static_cast<float>(cameraTile.x - halfExtent) * tileSize;
        float maxX = static_cast<float>(cameraTile.x + halfExtent + 1) * tileSize;
        float minZ = static_cast<float>(cameraTile.z - halfExtent) * tileSize;
        float maxZ = static_cast<float>(cameraTile.z + halfExtent + 1) * tileSize;

        if (worldPos.x >= minX && worldPos.x < maxX &&
            worldPos.y >= minZ && worldPos.y < maxZ) {
            return true;  // Position is covered by higher LOD
        }
    }
    return false;
}

void GrassTileManager::updateActiveTiles(const glm::vec3& cameraPos, uint64_t frameNumber, float currentTime) {
    glm::vec2 cameraXZ(cameraPos.x, cameraPos.z);
    currentFrame_ = frameNumber;
    currentTime_ = currentTime;

    // Clear active tiles list
    activeTiles_.clear();

    // Load tiles at each LOD level
    // LOD 0: High detail (closest to camera)
    // LOD 1: Medium detail
    // LOD 2: Low detail (furthest from camera)
    for (uint32_t lod = 0; lod < GrassConstants::NUM_LOD_LEVELS; ++lod) {
        float tileSize = GrassConstants::getTileSizeForLod(lod);
        uint32_t tilesPerAxis = (lod == 0) ? GrassConstants::TILES_PER_AXIS_LOD0 :
                                (lod == 1) ? GrassConstants::TILES_PER_AXIS_LOD1 :
                                             GrassConstants::TILES_PER_AXIS_LOD2;
        int halfExtent = static_cast<int>(tilesPerAxis) / 2;

        // Get camera tile for this LOD level
        GrassTile::TileCoord cameraTile = worldToTileCoord(cameraXZ, lod);

        // Load tiles in a grid around the camera
        for (int dz = -halfExtent; dz <= halfExtent; ++dz) {
            for (int dx = -halfExtent; dx <= halfExtent; ++dx) {
                GrassTile::TileCoord coord{
                    cameraTile.x + dx,
                    cameraTile.z + dz,
                    lod
                };

                // For LOD 1 and 2: Skip tiles that are covered by higher LOD levels
                if (lod > 0) {
                    glm::vec2 tileCenter = glm::vec2(
                        static_cast<float>(coord.x) * tileSize + tileSize * 0.5f,
                        static_cast<float>(coord.z) * tileSize + tileSize * 0.5f
                    );
                    if (isCoveredByHigherLod(tileCenter, lod, cameraXZ)) {
                        continue;  // Skip - covered by higher LOD tiles
                    }
                }

                GrassTile* tile = getOrCreateTile(coord);
                if (tile) {
                    tile->markUsed(frameNumber);
                    activeTiles_.push_back(tile);
                }
            }
        }
    }

    // Update current camera tile (LOD 0 for legacy compatibility)
    currentCameraTile_ = worldToTileCoord(cameraXZ, 0);

    // Sort tiles by LOD first (render high LOD first), then by distance within LOD
    std::sort(activeTiles_.begin(), activeTiles_.end(),
        [&cameraXZ](const GrassTile* a, const GrassTile* b) {
            // Sort by LOD level first (lower LOD number = higher detail = render first)
            if (a->getLodLevel() != b->getLodLevel()) {
                return a->getLodLevel() < b->getLodLevel();
            }
            // Within same LOD, sort by distance (closest first)
            return a->distanceSquaredTo(cameraXZ) < b->distanceSquaredTo(cameraXZ);
        });

    // Unload distant tiles that are safe to release
    unloadDistantTiles(cameraXZ, frameNumber);
}

void GrassTileManager::unloadDistantTiles(const glm::vec2& cameraXZ, uint64_t currentFrame) {
    // Collect tiles to unload (can't modify map while iterating)
    std::vector<GrassTile::TileCoord> tilesToUnload;

    for (auto& [coord, tile] : tiles_) {
        // Calculate unload distance based on LOD level
        uint32_t lod = coord.lod;
        float tileSize = GrassConstants::getTileSizeForLod(lod);
        uint32_t tilesPerAxis = (lod == 0) ? GrassConstants::TILES_PER_AXIS_LOD0 :
                                (lod == 1) ? GrassConstants::TILES_PER_AXIS_LOD1 :
                                             GrassConstants::TILES_PER_AXIS_LOD2;
        float halfExtent = static_cast<float>(tilesPerAxis) / 2.0f;
        float activeRadius = (halfExtent + 0.5f) * tileSize;
        float unloadRadius = activeRadius + GrassConstants::TILE_UNLOAD_MARGIN;
        float unloadRadiusSq = unloadRadius * unloadRadius;

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

    // Use computeBufferSet consistently for all buffer indexing (triple buffering)
    // This ensures descriptor set N uses buffers N, avoiding synchronization issues
    uint32_t bufferIndex = computeBufferSet;

    // Reset the shared indirect buffer once at the start (not per-tile)
    if (sharedIndirectBuffer_) {
        cmd.fillBuffer(sharedIndirectBuffer_, 0, sizeof(VkDrawIndirectCommand), 0);
        auto clearBarrier = vk::MemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                            {}, clearBarrier, {}, {});
    }

    // Bind compute pipeline once
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline_);

    // Process each active tile
    for (GrassTile* tile : activeTiles_) {
        auto it = tileDescriptorSets_.find(tile);
        if (it == tileDescriptorSets_.end()) continue;

        vk::DescriptorSet descSet = it->second[bufferIndex];

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

        if (!cullingUniformBuffers_.empty() && bufferIndex < cullingUniformBuffers_.size()) {
            writer.writeBuffer(2, cullingUniformBuffers_[bufferIndex], 0,
                               sizeof(CullingUniforms));
        }

        if (!tileInfoBuffers_.empty() && bufferIndex < tileInfoBuffers_.size() &&
            tileInfoBuffers_[bufferIndex]) {
            writer.writeBuffer(6, tileInfoBuffers_[bufferIndex], 0, VK_WHOLE_SIZE,
                               VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        if (!grassParamsBuffers_.empty() && bufferIndex < grassParamsBuffers_.size()) {
            writer.writeBuffer(7, grassParamsBuffers_[bufferIndex], 0,
                               sizeof(GrassParams));
        }

        writer.update();

        // Bind descriptor set for this tile
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                               computePipelineLayout_, 0,
                               descSet, {});

        // Push constants with tile origin and LOD information
        glm::vec2 tileOrigin = tile->getWorldOrigin();
        TiledGrassPushConstants push{};
        push.time = time;
        push.tileOriginX = tileOrigin.x;
        push.tileOriginZ = tileOrigin.y;
        push.tileSize = tile->getTileSize();
        push.spacingMult = tile->getSpacingMult();
        push.lodLevel = tile->getLodLevel();
        push.tileLoadTime = tile->getCreationTime();
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
    // LOD info not needed for graphics - instances already have world positions
    TiledGrassPushConstants push{};
    push.time = time;
    push.tileOriginX = 0.0f;
    push.tileOriginZ = 0.0f;
    push.tileSize = GrassConstants::TILE_SIZE_LOD0;  // Default, not used in vertex shader
    push.spacingMult = 1.0f;
    push.lodLevel = 0;
    push.tileLoadTime = 0.0f;  // Not used in graphics pass
    push.padding = 0.0f;

    cmd.pushConstants(graphicsPipelineLayout,
                      vk::ShaderStageFlagBits::eVertex,
                      0, sizeof(TiledGrassPushConstants), &push);

    // Single draw from shared indirect buffer
    cmd.drawIndirect(sharedIndirectBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}
