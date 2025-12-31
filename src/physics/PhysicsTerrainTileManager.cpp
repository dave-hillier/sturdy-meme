#include "PhysicsTerrainTileManager.h"
#include "terrain/TerrainTileCache.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

bool PhysicsTerrainTileManager::init(PhysicsWorld& physics, TerrainTileCache& tileCache, const Config& config) {
    physics_ = &physics;
    tileCache_ = &tileCache;
    config_ = config;

    SDL_Log("PhysicsTerrainTileManager: Initialized with loadRadius=%.0f, unloadRadius=%.0f",
            config_.loadRadius, config_.unloadRadius);
    return true;
}

void PhysicsTerrainTileManager::cleanup() {
    for (auto& [key, entry] : loadedTiles_) {
        physics_->removeBody(entry.bodyID);
    }
    loadedTiles_.clear();
    SDL_Log("PhysicsTerrainTileManager: Cleaned up all physics tiles");
}

uint64_t PhysicsTerrainTileManager::makeTileKey(int32_t tileX, int32_t tileZ, uint32_t lod) const {
    return (static_cast<uint64_t>(lod) << 48) |
           (static_cast<uint64_t>(static_cast<uint32_t>(tileX)) << 24) |
           static_cast<uint64_t>(static_cast<uint32_t>(tileZ));
}

std::vector<PhysicsTerrainTileManager::TileRequest> PhysicsTerrainTileManager::calculateRequiredTiles(
    const glm::vec3& position) const {

    std::vector<TileRequest> result;

    const uint32_t lod = 0;
    const uint32_t tilesX = tileCache_->getTilesX();
    const uint32_t tilesZ = tileCache_->getTilesZ();
    const float tileWorldSize = config_.terrainSize / static_cast<float>(tilesX);

    // Calculate tile range covering loadRadius circle
    // World coordinates: terrain centered at origin, ranging from -terrainSize/2 to +terrainSize/2
    float minWorldX = position.x - config_.loadRadius;
    float maxWorldX = position.x + config_.loadRadius;
    float minWorldZ = position.z - config_.loadRadius;
    float maxWorldZ = position.z + config_.loadRadius;

    // Convert world to tile coordinates
    // tile 0 covers [-terrainSize/2, -terrainSize/2 + tileWorldSize]
    auto worldToTile = [this, tilesX](float worldCoord) -> int32_t {
        float normalized = (worldCoord / config_.terrainSize) + 0.5f;
        return static_cast<int32_t>(std::floor(normalized * tilesX));
    };

    int32_t minTileX = worldToTile(minWorldX);
    int32_t maxTileX = worldToTile(maxWorldX);
    int32_t minTileZ = worldToTile(minWorldZ);
    int32_t maxTileZ = worldToTile(maxWorldZ);

    // Clamp to valid range
    minTileX = std::max(0, minTileX);
    maxTileX = std::min(static_cast<int32_t>(tilesX - 1), maxTileX);
    minTileZ = std::max(0, minTileZ);
    maxTileZ = std::min(static_cast<int32_t>(tilesZ - 1), maxTileZ);

    // Check each tile in range
    for (int32_t tz = minTileZ; tz <= maxTileZ; tz++) {
        for (int32_t tx = minTileX; tx <= maxTileX; tx++) {
            // Calculate tile center in world coordinates
            float tileCenterX = ((static_cast<float>(tx) + 0.5f) / tilesX - 0.5f) * config_.terrainSize;
            float tileCenterZ = ((static_cast<float>(tz) + 0.5f) / tilesZ - 0.5f) * config_.terrainSize;

            // Distance check (2D)
            float dx = position.x - tileCenterX;
            float dz = position.z - tileCenterZ;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist < config_.loadRadius) {
                result.push_back({tx, tz, lod});
            }
        }
    }

    return result;
}

bool PhysicsTerrainTileManager::loadPhysicsTile(int32_t tileX, int32_t tileZ, uint32_t lod) {
    TileCoord coord{tileX, tileZ};

    // Load CPU data through tile cache
    if (!tileCache_->loadTileCPUOnly(coord, lod)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "PhysicsTerrainTileManager: Failed to load tile CPU data (%d, %d) LOD%u",
                    tileX, tileZ, lod);
        return false;
    }

    // Get tile data from cache
    const TerrainTile* tile = tileCache_->getLoadedTile(coord, lod);
    if (!tile || tile->cpuData.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PhysicsTerrainTileManager: Tile CPU data not available after load");
        return false;
    }

    // Calculate tile center in world coordinates
    float tileCenterX = (tile->worldMinX + tile->worldMaxX) * 0.5f;
    float tileCenterZ = (tile->worldMinZ + tile->worldMaxZ) * 0.5f;
    float tileWorldSize = tile->worldMaxX - tile->worldMinX;

    // Create Jolt heightfield body at tile position
    uint32_t sampleCount = tileCache_->getTileResolution();

    // Generate per-tile hole mask from tile cache
    std::vector<uint8_t> tileHoleMask = tileCache_->rasterizeHolesForTile(
        tile->worldMinX, tile->worldMinZ,
        tile->worldMaxX, tile->worldMaxZ,
        sampleCount);

    PhysicsBodyID bodyID = physics_->createTerrainHeightfieldAtPosition(
        tile->cpuData.data(),
        tileHoleMask.data(),
        sampleCount,
        tileWorldSize,
        config_.heightScale,
        glm::vec3(tileCenterX, 0.0f, tileCenterZ)
    );

    if (bodyID == INVALID_BODY_ID) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PhysicsTerrainTileManager: Failed to create physics heightfield for tile (%d, %d)",
                     tileX, tileZ);
        return false;
    }

    // Store tile entry
    uint64_t key = makeTileKey(tileX, tileZ, lod);
    PhysicsTileEntry entry;
    entry.tileX = tileX;
    entry.tileZ = tileZ;
    entry.lod = lod;
    entry.bodyID = bodyID;
    entry.worldMinX = tile->worldMinX;
    entry.worldMinZ = tile->worldMinZ;
    entry.worldMaxX = tile->worldMaxX;
    entry.worldMaxZ = tile->worldMaxZ;
    loadedTiles_[key] = entry;

    SDL_Log("PhysicsTerrainTileManager: Loaded tile (%d, %d) LOD%u at [%.0f,%.0f]-[%.0f,%.0f], bodyID=%u",
            tileX, tileZ, lod, tile->worldMinX, tile->worldMinZ, tile->worldMaxX, tile->worldMaxZ, bodyID);

    return true;
}

void PhysicsTerrainTileManager::unloadPhysicsTile(uint64_t tileKey) {
    auto it = loadedTiles_.find(tileKey);
    if (it == loadedTiles_.end()) {
        return;
    }

    const PhysicsTileEntry& entry = it->second;
    physics_->removeBody(entry.bodyID);

    SDL_Log("PhysicsTerrainTileManager: Unloaded tile (%d, %d) LOD%u, bodyID=%u",
            entry.tileX, entry.tileZ, entry.lod, entry.bodyID);

    loadedTiles_.erase(it);
}

void PhysicsTerrainTileManager::update(const glm::vec3& playerPosition) {
    // Calculate required tiles at player position
    auto requiredTiles = calculateRequiredTiles(playerPosition);

    // Find tiles to load (required but not loaded)
    std::vector<TileRequest> tilesToLoad;
    for (const auto& req : requiredTiles) {
        uint64_t key = makeTileKey(req.tileX, req.tileZ, req.lod);
        if (loadedTiles_.find(key) == loadedTiles_.end()) {
            tilesToLoad.push_back(req);
        }
    }

    // Find tiles to unload (loaded but beyond unloadRadius)
    std::vector<uint64_t> tilesToUnload;
    for (const auto& [key, entry] : loadedTiles_) {
        float tileCenterX = (entry.worldMinX + entry.worldMaxX) * 0.5f;
        float tileCenterZ = (entry.worldMinZ + entry.worldMaxZ) * 0.5f;
        float dx = playerPosition.x - tileCenterX;
        float dz = playerPosition.z - tileCenterZ;
        float dist = std::sqrt(dx * dx + dz * dz);

        if (dist > config_.unloadRadius) {
            tilesToUnload.push_back(key);
        }
    }

    // Load tiles (limited per frame to avoid hitches)
    uint32_t loadedThisFrame = 0;
    for (const auto& req : tilesToLoad) {
        if (loadedThisFrame >= config_.maxTilesPerFrame) break;
        if (loadPhysicsTile(req.tileX, req.tileZ, req.lod)) {
            loadedThisFrame++;
        }
    }

    // Unload tiles (limited per frame)
    uint32_t unloadedThisFrame = 0;
    for (uint64_t key : tilesToUnload) {
        if (unloadedThisFrame >= config_.maxTilesPerFrame) break;
        unloadPhysicsTile(key);
        unloadedThisFrame++;
    }
}
