#include "TerrainPhysicsTiles.h"
#include "DebugLineSystem.h"
#include <SDL3/SDL_log.h>
#include <cmath>
#include <algorithm>

void TerrainPhysicsTiles::init(PhysicsWorld* physicsWorld, TerrainTileCache* cache,
                                float size, float scale, float minAlt) {
    physics = physicsWorld;
    tileCache = cache;
    terrainSize = size;
    heightScale = scale;
    minAltitude = minAlt;

    // Get LOD0 grid dimensions from tile cache
    if (tileCache) {
        lod0TilesX = tileCache->getTilesX();
        lod0TilesZ = tileCache->getTilesZ();
    }

    float tileSize = terrainSize / lod0TilesX;
    SDL_Log("TerrainPhysicsTiles initialized: LOD0 %ux%u tiles (%.0fm each), physics limited to 512m radius (~4 tiles)",
            lod0TilesX, lod0TilesZ, tileSize);
}

void TerrainPhysicsTiles::destroy() {
    if (!physics) return;

    // Remove all physics bodies
    for (auto& [key, tile] : physicsTiles) {
        if (tile.bodyID != INVALID_BODY_ID) {
            physics->removeBody(tile.bodyID);
        }
    }
    physicsTiles.clear();

    SDL_Log("TerrainPhysicsTiles destroyed");
}

uint64_t TerrainPhysicsTiles::makeTileKey(TileCoord coord, uint32_t lod) const {
    // Same key format as TerrainTileCache
    return (static_cast<uint64_t>(lod) << 48) |
           (static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) << 24) |
           static_cast<uint64_t>(static_cast<uint32_t>(coord.z));
}

void TerrainPhysicsTiles::getTileWorldBounds(TileCoord coord, uint32_t lod,
                                              float& outMinX, float& outMinZ,
                                              float& outMaxX, float& outMaxZ) const {
    // Calculate tiles per axis at this LOD level
    uint32_t lodTilesX = lod0TilesX >> lod;
    uint32_t lodTilesZ = lod0TilesZ >> lod;
    if (lodTilesX == 0) lodTilesX = 1;
    if (lodTilesZ == 0) lodTilesZ = 1;

    float tileWorldSizeX = terrainSize / lodTilesX;
    float tileWorldSizeZ = terrainSize / lodTilesZ;

    // Terrain is centered at origin, so offset by half terrain size
    outMinX = (static_cast<float>(coord.x) / lodTilesX - 0.5f) * terrainSize;
    outMinZ = (static_cast<float>(coord.z) / lodTilesZ - 0.5f) * terrainSize;
    outMaxX = outMinX + tileWorldSizeX;
    outMaxZ = outMinZ + tileWorldSizeZ;
}

void TerrainPhysicsTiles::getDesiredTiles(const glm::vec3& playerPos, float radius,
                                           std::vector<std::pair<TileCoord, uint32_t>>& outTiles) const {
    outTiles.clear();

    // Only use LOD0 tiles within the specified radius
    // With 512m radius and ~512m tiles, this typically loads ~4 tiles
    float halfTerrain = terrainSize * 0.5f;
    float lod0TileSize = terrainSize / lod0TilesX;

    // Calculate the range of LOD0 tiles that could be within radius
    int minTileX = static_cast<int>((playerPos.x - radius + halfTerrain) / lod0TileSize);
    int maxTileX = static_cast<int>((playerPos.x + radius + halfTerrain) / lod0TileSize);
    int minTileZ = static_cast<int>((playerPos.z - radius + halfTerrain) / lod0TileSize);
    int maxTileZ = static_cast<int>((playerPos.z + radius + halfTerrain) / lod0TileSize);

    // Clamp to valid tile range
    minTileX = std::max(0, minTileX);
    maxTileX = std::min(static_cast<int>(lod0TilesX) - 1, maxTileX);
    minTileZ = std::max(0, minTileZ);
    maxTileZ = std::min(static_cast<int>(lod0TilesZ) - 1, maxTileZ);

    // Add LOD0 tiles that are actually within the radius
    for (int tz = minTileZ; tz <= maxTileZ; tz++) {
        for (int tx = minTileX; tx <= maxTileX; tx++) {
            TileCoord coord{tx, tz};

            // Check if tile center is within radius
            float tileMinX, tileMinZ, tileMaxX, tileMaxZ;
            getTileWorldBounds(coord, HIGH_DETAIL_LOD, tileMinX, tileMinZ, tileMaxX, tileMaxZ);
            float tileCenterX = (tileMinX + tileMaxX) * 0.5f;
            float tileCenterZ = (tileMinZ + tileMaxZ) * 0.5f;

            float dx = tileCenterX - playerPos.x;
            float dz = tileCenterZ - playerPos.z;
            float distSq = dx * dx + dz * dz;

            if (distSq <= radius * radius) {
                outTiles.push_back({coord, HIGH_DETAIL_LOD});
            }
        }
    }
}

void TerrainPhysicsTiles::createPhysicsForTile(TileCoord coord, uint32_t lod) {
    if (!physics || !tileCache) return;

    uint64_t key = makeTileKey(coord, lod);

    // Already have physics for this tile?
    if (physicsTiles.find(key) != physicsTiles.end()) {
        return;
    }

    // Request tile CPU data to be loaded (this ensures cpuData is available)
    // Use loadTileCPUOnly for physics - doesn't require GPU resources
    if (!tileCache->loadTileCPUOnly(coord, lod)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TerrainPhysicsTiles: Failed to load tile (%d, %d) LOD%u",
                    coord.x, coord.z, lod);
        return;
    }

    // Get the loaded tile
    const TerrainTile* tile = tileCache->getLoadedTile(coord, lod);
    if (!tile || tile->cpuData.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TerrainPhysicsTiles: Tile (%d, %d) LOD%u has no CPU data",
                    coord.x, coord.z, lod);
        return;
    }

    // Get world bounds for this tile
    float minX, minZ, maxX, maxZ;
    getTileWorldBounds(coord, lod, minX, minZ, maxX, maxZ);
    float tileWorldSize = maxX - minX;  // Should equal maxZ - minZ

    // Create physics heightfield for this tile
    PhysicsBodyID bodyID = physics->createTerrainTile(
        tile->cpuData.data(),
        tileCache->getTileResolution(),
        minX, minZ,
        tileWorldSize,
        heightScale,
        minAltitude
    );

    if (bodyID != INVALID_BODY_ID) {
        PhysicsTile physTile;
        physTile.coord = coord;
        physTile.lod = lod;
        physTile.bodyID = bodyID;
        physicsTiles[key] = physTile;

        SDL_Log("TerrainPhysicsTiles: Created physics for tile (%d, %d) LOD%u at (%.0f, %.0f)",
                coord.x, coord.z, lod, minX, minZ);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainPhysicsTiles: Failed to create physics body for tile (%d, %d) LOD%u",
                     coord.x, coord.z, lod);
    }
}

void TerrainPhysicsTiles::destroyPhysicsForTile(uint64_t key) {
    auto it = physicsTiles.find(key);
    if (it == physicsTiles.end()) return;

    if (physics && it->second.bodyID != INVALID_BODY_ID) {
        physics->removeBody(it->second.bodyID);
    }

    SDL_Log("TerrainPhysicsTiles: Removed physics for tile (%d, %d) LOD%u",
            it->second.coord.x, it->second.coord.z, it->second.lod);

    physicsTiles.erase(it);
}

void TerrainPhysicsTiles::preloadTilesAt(const glm::vec3& playerPos, float highDetailRadius) {
    if (!physics || !tileCache) return;

    // Get all tiles that should have physics
    std::vector<std::pair<TileCoord, uint32_t>> desiredTiles;
    getDesiredTiles(playerPos, highDetailRadius, desiredTiles);

    // Create physics for ALL desired tiles immediately (no per-frame limit)
    for (const auto& [coord, lod] : desiredTiles) {
        uint64_t key = makeTileKey(coord, lod);
        if (physicsTiles.find(key) == physicsTiles.end()) {
            createPhysicsForTile(coord, lod);
        }
    }

    SDL_Log("TerrainPhysicsTiles: Preloaded %u physics tiles", getActivePhysicsTileCount());
}

void TerrainPhysicsTiles::update(const glm::vec3& playerPos, float radius) {
    if (!physics || !tileCache) return;

    // Use hysteresis to prevent tile flickering at boundary
    // Load tiles at radius, unload at radius + hysteresis margin
    static constexpr float HYSTERESIS_MARGIN = 128.0f;
    float unloadRadius = radius + HYSTERESIS_MARGIN;

    // Get tiles we want to load (within radius)
    std::vector<std::pair<TileCoord, uint32_t>> desiredTiles;
    getDesiredTiles(playerPos, radius, desiredTiles);

    // Build a set of desired keys for fast lookup
    std::unordered_map<uint64_t, bool> desiredKeys;
    for (const auto& [coord, lod] : desiredTiles) {
        desiredKeys[makeTileKey(coord, lod)] = true;
    }

    // Get tiles within unload radius (tiles to keep)
    std::vector<std::pair<TileCoord, uint32_t>> keepTiles;
    getDesiredTiles(playerPos, unloadRadius, keepTiles);

    std::unordered_map<uint64_t, bool> keepKeys;
    for (const auto& [coord, lod] : keepTiles) {
        keepKeys[makeTileKey(coord, lod)] = true;
    }

    // Remove physics for tiles outside unload radius
    std::vector<uint64_t> toRemove;
    for (const auto& [key, tile] : physicsTiles) {
        if (keepKeys.find(key) == keepKeys.end()) {
            toRemove.push_back(key);
        }
    }
    for (uint64_t key : toRemove) {
        destroyPhysicsForTile(key);
    }

    // Add physics for new tiles within load radius (limit to a few per frame)
    int tilesCreatedThisFrame = 0;
    static constexpr int MAX_TILES_PER_FRAME = 2;

    for (const auto& [coord, lod] : desiredTiles) {
        uint64_t key = makeTileKey(coord, lod);
        if (physicsTiles.find(key) == physicsTiles.end()) {
            createPhysicsForTile(coord, lod);
            tilesCreatedThisFrame++;
            if (tilesCreatedThisFrame >= MAX_TILES_PER_FRAME) {
                break;  // Spread creation across frames
            }
        }
    }
}

void TerrainPhysicsTiles::drawTileBounds(DebugLineSystem& debugLines) const {
    // Green for physics tiles (all LOD0)
    const glm::vec4 tileColor{0.0f, 1.0f, 0.0f, 1.0f};

    for (const auto& [key, tile] : physicsTiles) {
        float minX, minZ, maxX, maxZ;
        getTileWorldBounds(tile.coord, tile.lod, minX, minZ, maxX, maxZ);

        // Use heightScale range for Y bounds (approximate terrain height range)
        float minY = minAltitude;
        float maxY = minAltitude + heightScale;

        debugLines.addBox(glm::vec3(minX, minY, minZ), glm::vec3(maxX, maxY, maxZ), tileColor);
    }
}
