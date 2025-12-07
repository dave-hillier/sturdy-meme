#include "TerrainPhysicsTiles.h"
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

    // Get LOD grid dimensions from tile cache
    if (tileCache) {
        lod0TilesX = tileCache->getTilesX();
        lod0TilesZ = tileCache->getTilesZ();
        // LOD3 has 8x fewer tiles in each dimension (2^3 = 8)
        lod3TilesX = std::max(1u, lod0TilesX >> 3);
        lod3TilesZ = std::max(1u, lod0TilesZ >> 3);
    }

    SDL_Log("TerrainPhysicsTiles initialized: LOD0 %ux%u tiles, LOD3 %ux%u tiles",
            lod0TilesX, lod0TilesZ, lod3TilesX, lod3TilesZ);
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

void TerrainPhysicsTiles::getDesiredTiles(const glm::vec3& playerPos, float highDetailRadius,
                                           std::vector<std::pair<TileCoord, uint32_t>>& outTiles) const {
    outTiles.clear();

    // 1. Find LOD0 tiles within highDetailRadius of player
    float halfTerrain = terrainSize * 0.5f;
    float lod0TileSize = terrainSize / lod0TilesX;

    // Calculate the range of LOD0 tiles that could be within radius
    int minTileX = static_cast<int>((playerPos.x - highDetailRadius + halfTerrain) / lod0TileSize);
    int maxTileX = static_cast<int>((playerPos.x + highDetailRadius + halfTerrain) / lod0TileSize);
    int minTileZ = static_cast<int>((playerPos.z - highDetailRadius + halfTerrain) / lod0TileSize);
    int maxTileZ = static_cast<int>((playerPos.z + highDetailRadius + halfTerrain) / lod0TileSize);

    // Clamp to valid tile range
    minTileX = std::max(0, minTileX);
    maxTileX = std::min(static_cast<int>(lod0TilesX) - 1, maxTileX);
    minTileZ = std::max(0, minTileZ);
    maxTileZ = std::min(static_cast<int>(lod0TilesZ) - 1, maxTileZ);

    // Add LOD0 tiles that are actually within the radius
    std::vector<TileCoord> lod0Tiles;
    for (int tz = minTileZ; tz <= maxTileZ; tz++) {
        for (int tx = minTileX; tx <= maxTileX; tx++) {
            TileCoord coord{tx, tz};

            // Check if tile center is within radius
            float tileCenterX, tileCenterZ, tileMaxX, tileMaxZ;
            getTileWorldBounds(coord, HIGH_DETAIL_LOD, tileCenterX, tileCenterZ, tileMaxX, tileMaxZ);
            tileCenterX = (tileCenterX + tileMaxX) * 0.5f;
            tileCenterZ = (tileCenterZ + tileMaxZ) * 0.5f;

            float dx = tileCenterX - playerPos.x;
            float dz = tileCenterZ - playerPos.z;
            float distSq = dx * dx + dz * dz;

            if (distSq <= highDetailRadius * highDetailRadius) {
                outTiles.push_back({coord, HIGH_DETAIL_LOD});
                lod0Tiles.push_back(coord);
            }
        }
    }

    // 2. Add LOD3 tiles for the entire terrain (except where LOD0 covers)
    float lod3TileSize = terrainSize / lod3TilesX;

    for (uint32_t tz = 0; tz < lod3TilesZ; tz++) {
        for (uint32_t tx = 0; tx < lod3TilesX; tx++) {
            TileCoord lod3Coord{static_cast<int32_t>(tx), static_cast<int32_t>(tz)};

            // Check if this LOD3 tile overlaps with any LOD0 tile
            float lod3MinX, lod3MinZ, lod3MaxX, lod3MaxZ;
            getTileWorldBounds(lod3Coord, LOW_DETAIL_LOD, lod3MinX, lod3MinZ, lod3MaxX, lod3MaxZ);

            bool overlapsLod0 = false;
            for (const auto& lod0Coord : lod0Tiles) {
                float lod0MinX, lod0MinZ, lod0MaxX, lod0MaxZ;
                getTileWorldBounds(lod0Coord, HIGH_DETAIL_LOD, lod0MinX, lod0MinZ, lod0MaxX, lod0MaxZ);

                // Check for overlap (any intersection)
                if (lod3MaxX > lod0MinX && lod3MinX < lod0MaxX &&
                    lod3MaxZ > lod0MinZ && lod3MinZ < lod0MaxZ) {
                    overlapsLod0 = true;
                    break;
                }
            }

            // Only add LOD3 tile if it doesn't overlap any LOD0 tile
            if (!overlapsLod0) {
                outTiles.push_back({lod3Coord, LOW_DETAIL_LOD});
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

    // Request tile to be loaded (this ensures cpuData is available)
    if (!tileCache->requestTileLoad(coord, lod)) {
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

void TerrainPhysicsTiles::update(const glm::vec3& playerPos, float highDetailRadius) {
    if (!physics || !tileCache) return;

    // Get the set of tiles we want to have physics for
    std::vector<std::pair<TileCoord, uint32_t>> desiredTiles;
    getDesiredTiles(playerPos, highDetailRadius, desiredTiles);

    // Build a set of desired keys for fast lookup
    std::unordered_map<uint64_t, bool> desiredKeys;
    for (const auto& [coord, lod] : desiredTiles) {
        desiredKeys[makeTileKey(coord, lod)] = true;
    }

    // Remove physics for tiles no longer needed
    std::vector<uint64_t> toRemove;
    for (const auto& [key, tile] : physicsTiles) {
        if (desiredKeys.find(key) == desiredKeys.end()) {
            toRemove.push_back(key);
        }
    }
    for (uint64_t key : toRemove) {
        destroyPhysicsForTile(key);
    }

    // Add physics for new tiles (limit to a few per frame to avoid hitches)
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
