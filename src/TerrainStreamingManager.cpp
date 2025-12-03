#include "TerrainStreamingManager.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>

TerrainStreamingManager::~TerrainStreamingManager() {
    shutdown();
}

bool TerrainStreamingManager::init(const StreamingManager::InitInfo& baseInfo,
                                    const TerrainStreamingConfig& terrainConfig) {
    config = terrainConfig;

    SDL_Log("TerrainStreamingManager: Cache directory: %s",
            config.tileConfig.cacheDirectory.empty() ? "(empty - procedural)" : config.tileConfig.cacheDirectory.c_str());

    // Ensure LOD levels match tile config
    config.tileConfig.numLODLevels = static_cast<uint32_t>(config.lodLevels.size());

    // Override budget with terrain-specific settings
    StreamingManager::InitInfo info = baseInfo;
    info.budget = config.budget;

    if (!StreamingManager::init(info)) {
        return false;
    }

    return true;
}

void TerrainStreamingManager::shutdown() {
    // First shutdown the base (stops worker threads)
    StreamingManager::shutdown();

    // Clear pending uploads
    {
        std::lock_guard<std::mutex> lock(pendingUploadMutex);
        pendingGPUUpload.clear();
    }

    // Destroy all tile GPU resources
    for (auto& [coord, tile] : tiles) {
        if (tile->getLoadState() == TileLoadState::Loaded) {
            tile->destroyGPUResources(device, allocator);
            removeGPUMemory(tile->getGPUMemoryUsage());
        }
    }

    tiles.clear();
    visibleTiles.clear();

    {
        std::lock_guard<std::mutex> lock(loadingTilesMutex);
        loadingTiles.clear();
    }
}

void TerrainStreamingManager::update(const glm::vec3& cameraPos, uint64_t frameNumber) {
    // Process any completed background loads
    processCompletedLoads();

    // Determine which tiles should be loaded (all LOD levels)
    updateTileRequests(cameraPos, frameNumber);

    // Evict tiles that are too far or we're over budget
    evictTiles(cameraPos, frameNumber);

    // Update the list of visible tiles
    updateVisibleTiles(cameraPos);

    lastCameraPos = cameraPos;
    hasLastCameraPos = true;
}

uint32_t TerrainStreamingManager::processCompletedLoads() {
    std::vector<TerrainTile*> tilesToUpload;

    {
        std::lock_guard<std::mutex> lock(pendingUploadMutex);
        tilesToUpload.swap(pendingGPUUpload);
    }

    uint32_t processed = 0;

    for (TerrainTile* tile : tilesToUpload) {
        if (tile->getLoadState() != TileLoadState::Loading) {
            continue;  // State changed, skip
        }

        // Create GPU resources on main thread
        if (tile->createGPUResources(device, allocator, graphicsQueue, commandPool)) {
            tile->setLoadState(TileLoadState::Loaded);
            addGPUMemory(tile->getGPUMemoryUsage());
            processed++;
        } else {
            // Failed to create GPU resources
            tile->setLoadState(TileLoadState::Unloaded);
        }

        // Remove from loading set
        {
            std::lock_guard<std::mutex> loadLock(loadingTilesMutex);
            loadingTiles.erase(tile->getCoord());
        }
    }

    return processed;
}

float TerrainStreamingManager::getTileSizeForLOD(uint32_t lodLevel) const {
    return config.tileConfig.baseTileSize * static_cast<float>(1u << lodLevel);
}

uint32_t TerrainStreamingManager::getLODForDistance(float distance) const {
    for (uint32_t lod = 0; lod < config.lodLevels.size(); lod++) {
        if (distance >= config.lodLevels[lod].minDistance &&
            distance < config.lodLevels[lod].maxDistance) {
            return lod;
        }
    }
    // Beyond all LOD ranges, use coarsest
    return static_cast<uint32_t>(config.lodLevels.size()) - 1;
}

TerrainTile::Coord TerrainStreamingManager::worldToTileCoord(float worldX, float worldZ, uint32_t lodLevel) const {
    float tileSize = getTileSizeForLOD(lodLevel);
    return {
        static_cast<int32_t>(std::floor(worldX / tileSize)),
        static_cast<int32_t>(std::floor(worldZ / tileSize)),
        lodLevel
    };
}

TerrainTile* TerrainStreamingManager::getOrCreateTile(const TerrainTile::Coord& coord) {
    auto it = tiles.find(coord);
    if (it != tiles.end()) {
        return it->second.get();
    }

    // Create new tile
    auto tile = std::make_unique<TerrainTile>();
    tile->init(coord, config.tileConfig);
    TerrainTile* tilePtr = tile.get();
    tiles[coord] = std::move(tile);

    return tilePtr;
}

void TerrainStreamingManager::requestTileLoad(TerrainTile* tile, float distance, uint64_t frameNumber) {
    // Check if already loading or loaded
    TileLoadState state = tile->getLoadState();
    if (state == TileLoadState::Loading || state == TileLoadState::Loaded) {
        return;
    }

    // Check if already in loading set
    {
        std::lock_guard<std::mutex> lock(loadingTilesMutex);
        if (loadingTiles.count(tile->getCoord()) > 0) {
            return;
        }
        loadingTiles.insert(tile->getCoord());
    }

    tile->setLoadState(TileLoadState::Loading);

    // Priority: higher LOD (lower number) gets higher priority, then closer distance
    // LOD 0 is most important, LOD 3 least
    float lodPriorityMultiplier = 1.0f + tile->getLODLevel() * 0.5f;
    LoadPriority priority{distance * lodPriorityMultiplier, 1.0f, frameNumber};

    submitWork([this, tile]() {
        // Load height data on background thread
        if (tile->loadHeightData()) {
            // Queue for GPU upload on main thread
            std::lock_guard<std::mutex> lock(pendingUploadMutex);
            pendingGPUUpload.push_back(tile);
        } else {
            tile->setLoadState(TileLoadState::Unloaded);

            std::lock_guard<std::mutex> loadLock(loadingTilesMutex);
            loadingTiles.erase(tile->getCoord());
        }
    }, priority);
}

void TerrainStreamingManager::updateTileRequests(const glm::vec3& cameraPos, uint64_t frameNumber) {
    // Process each LOD level
    for (uint32_t lod = 0; lod < config.lodLevels.size(); lod++) {
        const auto& lodConfig = config.lodLevels[lod];
        updateTileRequestsForLOD(cameraPos, frameNumber, lod,
                                  lodConfig.minDistance, lodConfig.maxDistance);
    }
}

void TerrainStreamingManager::updateTileRequestsForLOD(const glm::vec3& cameraPos, uint64_t frameNumber,
                                                        uint32_t lodLevel, float minDist, float maxDist) {
    float tileSize = getTileSizeForLOD(lodLevel);

    // Calculate tile radius to check
    int radiusTiles = static_cast<int>(std::ceil(maxDist / tileSize)) + 1;

    // Get camera tile coordinate for this LOD
    TerrainTile::Coord camCoord = worldToTileCoord(cameraPos.x, cameraPos.z, lodLevel);

    // Collect tiles that need loading
    struct TileRequest {
        TerrainTile::Coord coord;
        float distance;
    };
    std::vector<TileRequest> requests;

    for (int dz = -radiusTiles; dz <= radiusTiles; dz++) {
        for (int dx = -radiusTiles; dx <= radiusTiles; dx++) {
            TerrainTile::Coord coord{camCoord.x + dx, camCoord.z + dz, lodLevel};

            // Calculate distance to tile center
            float tileCenterX = (coord.x + 0.5f) * tileSize;
            float tileCenterZ = (coord.z + 0.5f) * tileSize;
            float distance = std::sqrt(
                (tileCenterX - cameraPos.x) * (tileCenterX - cameraPos.x) +
                (tileCenterZ - cameraPos.z) * (tileCenterZ - cameraPos.z)
            );

            // Only load if within this LOD's range
            if (distance >= minDist && distance < maxDist) {
                requests.push_back({coord, distance});
            }
        }
    }

    // Sort by distance (closer first)
    std::sort(requests.begin(), requests.end(),
              [](const TileRequest& a, const TileRequest& b) {
                  return a.distance < b.distance;
              });

    // Request loading (respecting per-frame limit and budget)
    uint32_t loadRequests = 0;
    for (const auto& req : requests) {
        if (loadRequests >= config.budget.maxLoadRequestsPerFrame) {
            break;
        }

        // Check budget
        if (currentGPUMemory.load() > config.budget.targetGPUMemory) {
            break;
        }

        TerrainTile* tile = getOrCreateTile(req.coord);
        if (tile->getLoadState() == TileLoadState::Unloaded) {
            requestTileLoad(tile, req.distance, frameNumber);
            loadRequests++;
        }
    }
}

bool TerrainStreamingManager::hasHigherLODCoverage(float worldX, float worldZ, uint32_t currentLOD) const {
    // Check if any higher detail (lower number) LOD tile covers this position
    for (uint32_t lod = 0; lod < currentLOD; lod++) {
        TerrainTile::Coord coord = worldToTileCoord(worldX, worldZ, lod);
        auto it = tiles.find(coord);
        if (it != tiles.end() && it->second->getLoadState() == TileLoadState::Loaded) {
            return true;
        }
    }
    return false;
}

void TerrainStreamingManager::evictTiles(const glm::vec3& cameraPos, uint64_t frameNumber) {
    // Collect tiles for potential eviction
    struct EvictionCandidate {
        TerrainTile* tile;
        float distance;
        uint32_t lod;
        uint64_t lastAccess;
    };
    std::vector<EvictionCandidate> candidates;

    for (auto& [coord, tile] : tiles) {
        if (tile->getLoadState() != TileLoadState::Loaded) {
            continue;
        }

        float distance = tile->getDistanceToCamera(cameraPos);
        uint32_t lod = tile->getLODLevel();

        // Get the LOD config for this tile
        float unloadDist = (lod < config.lodLevels.size())
            ? config.lodLevels[lod].maxDistance + config.lodLevels[lod].unloadMargin
            : config.lodLevels.back().maxDistance;

        // Evict if beyond this LOD's range (with hysteresis)
        if (distance > unloadDist) {
            candidates.push_back({tile.get(), distance, lod, tile->getLastAccessFrame()});
        }
        // Also consider for eviction if over budget
        else if (currentGPUMemory.load() > config.budget.maxGPUMemory) {
            candidates.push_back({tile.get(), distance, lod, tile->getLastAccessFrame()});
        }
    }

    if (candidates.empty()) {
        return;
    }

    // Sort by eviction priority:
    // 1. Lower LOD (coarser) tiles first when over budget
    // 2. Tiles outside their valid range first
    // 3. Furthest tiles first
    // 4. LRU
    std::sort(candidates.begin(), candidates.end(),
              [this](const EvictionCandidate& a, const EvictionCandidate& b) {
                  // Prefer evicting coarser tiles (higher LOD number) when budget constrained
                  if (a.lod != b.lod) {
                      return a.lod > b.lod;  // Higher LOD (coarser) first
                  }

                  // Then by distance (further first)
                  if (a.distance != b.distance) {
                      return a.distance > b.distance;
                  }

                  // Then LRU
                  return a.lastAccess < b.lastAccess;
              });

    // Evict tiles (respecting per-frame limit)
    uint32_t evicted = 0;
    for (const auto& candidate : candidates) {
        if (evicted >= config.budget.maxUnloadsPerFrame) {
            break;
        }

        // Stop if we're under budget
        if (currentGPUMemory.load() <= config.budget.targetGPUMemory) {
            // But continue if tile is outside its valid range
            uint32_t lod = candidate.lod;
            float unloadDist = (lod < config.lodLevels.size())
                ? config.lodLevels[lod].maxDistance + config.lodLevels[lod].unloadMargin
                : config.lodLevels.back().maxDistance;

            if (candidate.distance <= unloadDist) {
                break;
            }
        }

        TerrainTile* tile = candidate.tile;
        size_t memUsage = tile->getGPUMemoryUsage();

        tile->setLoadState(TileLoadState::Unloading);
        tile->destroyGPUResources(device, allocator);
        removeGPUMemory(memUsage);
        tile->reset();

        evicted++;
    }
}

void TerrainStreamingManager::updateVisibleTiles(const glm::vec3& cameraPos) {
    visibleTiles.clear();

    for (auto& [coord, tile] : tiles) {
        if (tile->getLoadState() != TileLoadState::Loaded) {
            continue;
        }

        uint32_t lod = tile->getLODLevel();

        // Skip this tile if a higher-detail tile covers this area
        glm::vec2 center = tile->getWorldCenter();
        if (lod > 0 && hasHigherLODCoverage(center.x, center.y, lod)) {
            continue;
        }

        tile->markAccessed(0);  // Will be updated with actual frame number
        visibleTiles.push_back(tile.get());
    }

    // Sort by LOD (higher detail first) then by distance
    std::sort(visibleTiles.begin(), visibleTiles.end(),
              [&cameraPos](const TerrainTile* a, const TerrainTile* b) {
                  if (a->getLODLevel() != b->getLODLevel()) {
                      return a->getLODLevel() < b->getLODLevel();  // Higher detail first
                  }
                  return a->getDistanceToCamera(cameraPos) < b->getDistanceToCamera(cameraPos);
              });
}

std::vector<TerrainTile*> TerrainStreamingManager::getLoadedTiles() const {
    std::vector<TerrainTile*> result;
    for (const auto& [coord, tile] : tiles) {
        if (tile->getLoadState() == TileLoadState::Loaded) {
            result.push_back(tile.get());
        }
    }
    return result;
}

float TerrainStreamingManager::getHeightAt(float worldX, float worldZ) const {
    // Find the best (highest detail) loaded tile at this position
    TerrainTile* bestTile = nullptr;

    for (uint32_t lod = 0; lod < config.lodLevels.size(); lod++) {
        TerrainTile::Coord coord = worldToTileCoord(worldX, worldZ, lod);
        auto it = tiles.find(coord);
        if (it != tiles.end() && it->second->getLoadState() == TileLoadState::Loaded) {
            bestTile = it->second.get();
            break;  // Found highest detail, stop
        }
    }

    if (!bestTile) {
        return 0.0f;
    }

    // Convert to local coordinates
    glm::vec2 worldMin = bestTile->getWorldMin();
    float localX = worldX - worldMin.x;
    float localZ = worldZ - worldMin.y;

    return bestTile->getHeightAt(localX, localZ);
}

bool TerrainStreamingManager::hasTileAt(float worldX, float worldZ) const {
    // Check all LOD levels for a loaded tile
    for (uint32_t lod = 0; lod < config.lodLevels.size(); lod++) {
        TerrainTile::Coord coord = worldToTileCoord(worldX, worldZ, lod);
        auto it = tiles.find(coord);
        if (it != tiles.end() && it->second->getLoadState() == TileLoadState::Loaded) {
            return true;
        }
    }
    return false;
}

TerrainTile* TerrainStreamingManager::getTileAt(float worldX, float worldZ) const {
    // Return highest detail loaded tile at this position
    for (uint32_t lod = 0; lod < config.lodLevels.size(); lod++) {
        TerrainTile::Coord coord = worldToTileCoord(worldX, worldZ, lod);
        auto it = tiles.find(coord);
        if (it != tiles.end() && it->second->getLoadState() == TileLoadState::Loaded) {
            return it->second.get();
        }
    }
    return nullptr;
}

uint32_t TerrainStreamingManager::getLoadedTileCount() const {
    uint32_t count = 0;
    for (const auto& [coord, tile] : tiles) {
        if (tile->getLoadState() == TileLoadState::Loaded) {
            count++;
        }
    }
    return count;
}

uint32_t TerrainStreamingManager::getLoadingTileCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(loadingTilesMutex));
    return static_cast<uint32_t>(loadingTiles.size());
}
