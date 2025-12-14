#include "PhysicsTerrain.h"
#include <SDL3/SDL_log.h>
#include <cmath>
#include <algorithm>

void PhysicsTerrain::init(PhysicsWorld* physics, TerrainTileCache* tileCache, const Config& cfg) {
    physicsWorld = physics;
    terrainTileCache = tileCache;
    config = cfg;

    if (tileCache) {
        float actualTileSize = tileCache->getTerrainSize() / tileCache->getTilesX();
        SDL_Log("PhysicsTerrain initialized: tileSize=%.0f (%ux%u tiles), loadRadius=%.0f, unloadRadius=%.0f",
                actualTileSize, tileCache->getTilesX(), tileCache->getTilesZ(),
                config.loadRadius, config.unloadRadius);
        SDL_Log("PhysicsTerrain: using heightScale=%.1f (tile cache has %.1f)",
                config.heightScale, tileCache->getHeightScale());
    }
}

void PhysicsTerrain::update(const glm::vec3& playerPos) {
    if (!physicsWorld || !terrainTileCache) return;

    // Get current tile coordinate
    TileCoord currentTile = worldToTileCoord(playerPos.x, playerPos.z);

    // Calculate actual tile size from cache
    float terrainSize = terrainTileCache->getTerrainSize();
    float actualTileSize = terrainSize / terrainTileCache->getTilesX();

    // Calculate how many tiles we need to check in each direction
    int tileRadius = static_cast<int>(std::ceil(config.loadRadius / actualTileSize)) + 1;

    // Collect tiles that should be loaded
    std::vector<TileCoord> tilesToLoad;

    for (int dz = -tileRadius; dz <= tileRadius; dz++) {
        for (int dx = -tileRadius; dx <= tileRadius; dx++) {
            TileCoord coord{currentTile.x + dx, currentTile.z + dz};

            // Check if tile center is within load radius
            glm::vec2 tileCenter = getTileCenter(coord);
            float distSq = (tileCenter.x - playerPos.x) * (tileCenter.x - playerPos.x) +
                          (tileCenter.y - playerPos.z) * (tileCenter.y - playerPos.z);

            if (distSq <= config.loadRadius * config.loadRadius) {
                uint64_t key = makeTileKey(coord);
                if (loadedPhysicsTiles.find(key) == loadedPhysicsTiles.end()) {
                    tilesToLoad.push_back(coord);
                }
            }
        }
    }

    // Load new tiles (limit per frame to avoid hitches)
    constexpr int MAX_LOADS_PER_FRAME = 2;
    int loadsThisFrame = 0;
    for (const auto& coord : tilesToLoad) {
        if (loadsThisFrame >= MAX_LOADS_PER_FRAME) break;
        if (loadTile(coord)) {
            loadsThisFrame++;
        }
    }

    // Collect tiles to unload (beyond unload radius)
    std::vector<TileCoord> tilesToUnload;
    for (const auto& [key, tile] : loadedPhysicsTiles) {
        glm::vec2 tileCenter = getTileCenter(tile.coord);
        float distSq = (tileCenter.x - playerPos.x) * (tileCenter.x - playerPos.x) +
                      (tileCenter.y - playerPos.z) * (tileCenter.y - playerPos.z);

        if (distSq > config.unloadRadius * config.unloadRadius) {
            tilesToUnload.push_back(tile.coord);
        }
    }

    // Unload distant tiles
    for (const auto& coord : tilesToUnload) {
        unloadTile(coord);
    }

    lastUpdatePos = playerPos;
    hasUpdatedOnce = true;
}

TileCoord PhysicsTerrain::worldToTileCoord(float worldX, float worldZ) const {
    // Use the same coordinate calculation as TerrainTileCache for LOD 0
    // Convert world position to normalized [0, 1]
    float terrainSize = terrainTileCache->getTerrainSize();
    float normX = (worldX / terrainSize) + 0.5f;
    float normZ = (worldZ / terrainSize) + 0.5f;

    // Clamp to valid range
    normX = std::clamp(normX, 0.0f, 0.9999f);
    normZ = std::clamp(normZ, 0.0f, 0.9999f);

    // Use tile cache's tile counts for LOD 0
    uint32_t tilesX = terrainTileCache->getTilesX();
    uint32_t tilesZ = terrainTileCache->getTilesZ();

    return TileCoord{
        static_cast<int32_t>(normX * tilesX),
        static_cast<int32_t>(normZ * tilesZ)
    };
}

glm::vec2 PhysicsTerrain::getTileCenter(TileCoord coord) const {
    // Use the same formula as TerrainTileCache for consistency
    float terrainSize = terrainTileCache->getTerrainSize();
    uint32_t tilesX = terrainTileCache->getTilesX();
    uint32_t tilesZ = terrainTileCache->getTilesZ();

    // Tile center = ((coord + 0.5) / numTiles - 0.5) * terrainSize
    float centerX = ((static_cast<float>(coord.x) + 0.5f) / tilesX - 0.5f) * terrainSize;
    float centerZ = ((static_cast<float>(coord.z) + 0.5f) / tilesZ - 0.5f) * terrainSize;
    return glm::vec2(centerX, centerZ);
}

bool PhysicsTerrain::loadTile(TileCoord coord) {
    // Check bounds using tile cache's tile counts
    int maxTilesX = static_cast<int>(terrainTileCache->getTilesX());
    int maxTilesZ = static_cast<int>(terrainTileCache->getTilesZ());
    if (coord.x < 0 || coord.x >= maxTilesX || coord.z < 0 || coord.z >= maxTilesZ) {
        return false;
    }

    // Request CPU data from tile cache (LOD 0 for highest resolution)
    if (!terrainTileCache->loadTileCPUOnly(coord, 0)) {
        return false;
    }

    // Get the loaded tile data
    const TerrainTile* tile = terrainTileCache->getLoadedTile(coord, 0);
    if (!tile || tile->cpuData.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "PhysicsTerrain: No CPU data for tile (%d, %d)", coord.x, coord.z);
        return false;
    }

    // Use the tile's pre-computed world bounds for consistency with rendering
    float worldMinX = tile->worldMinX;
    float worldMinZ = tile->worldMinZ;
    float worldMaxX = tile->worldMaxX;
    float worldMaxZ = tile->worldMaxZ;
    float tileWorldSize = worldMaxX - worldMinX;  // Actual tile size from cache

    // Create physics heightfield for this tile
    // Use config.heightScale which comes from TerrainSystem (same as rendering)
    uint32_t resolution = terrainTileCache->getTileResolution();

    PhysicsBodyID bodyId = physicsWorld->createTileHeightfield(
        tile->cpuData.data(),
        resolution,
        tileWorldSize,
        config.heightScale,
        worldMinX,
        worldMinZ
    );

    if (bodyId == INVALID_BODY_ID) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "PhysicsTerrain: Failed to create heightfield for tile (%d, %d)",
                     coord.x, coord.z);
        return false;
    }

    // Store the physics tile
    PhysicsTile physicsTile;
    physicsTile.coord = coord;
    physicsTile.bodyId = bodyId;
    physicsTile.worldMinX = worldMinX;
    physicsTile.worldMinZ = worldMinZ;
    physicsTile.worldMaxX = worldMaxX;
    physicsTile.worldMaxZ = worldMaxZ;

    uint64_t key = makeTileKey(coord);
    loadedPhysicsTiles[key] = physicsTile;

    SDL_Log("PhysicsTerrain: Loaded tile (%d, %d) at world (%.0f, %.0f) - (%.0f, %.0f)",
            coord.x, coord.z, worldMinX, worldMinZ, worldMaxX, worldMaxZ);

    return true;
}

void PhysicsTerrain::unloadTile(TileCoord coord) {
    uint64_t key = makeTileKey(coord);
    auto it = loadedPhysicsTiles.find(key);
    if (it == loadedPhysicsTiles.end()) return;

    // Remove the physics body
    if (it->second.bodyId != INVALID_BODY_ID) {
        physicsWorld->removeBody(it->second.bodyId);
    }

    SDL_Log("PhysicsTerrain: Unloaded tile (%d, %d)", coord.x, coord.z);

    loadedPhysicsTiles.erase(it);
}

uint64_t PhysicsTerrain::makeTileKey(TileCoord coord) const {
    return static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) << 32 |
           static_cast<uint64_t>(static_cast<uint32_t>(coord.z));
}
