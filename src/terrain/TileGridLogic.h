#pragma once

// Pure terrain tile grid logic - no Vulkan dependencies
// Extracted for testability from TerrainTileCache

#include <cstdint>
#include <cmath>
#include <vector>

namespace TileGrid {

// Tile coordinate in the grid
struct TileCoord {
    int32_t x = 0;
    int32_t z = 0;

    bool operator==(const TileCoord& other) const {
        return x == other.x && z == other.z;
    }

    bool operator!=(const TileCoord& other) const {
        return !(*this == other);
    }
};

// Hash function for TileCoord to use in unordered_map
struct TileCoordHash {
    size_t operator()(const TileCoord& coord) const {
        return std::hash<int64_t>()(static_cast<int64_t>(coord.x) << 32 | static_cast<uint32_t>(coord.z));
    }
};

// Circular terrain hole definition
struct TerrainHole {
    float centerX = 0.0f;
    float centerZ = 0.0f;
    float radius = 0.0f;
};

// LOD distance thresholds (in world units)
struct LODThresholds {
    float lod0Max = 1000.0f;   // < 1km: LOD0 (highest detail)
    float lod1Max = 2000.0f;   // 1-2km: LOD1
    float lod2Max = 4000.0f;   // 2-4km: LOD2
    float lod3Max = 8000.0f;   // 4-8km: LOD3 (lowest detail)
    uint32_t numLODLevels = 4;
};

// Grid configuration
struct GridConfig {
    float terrainSize = 16384.0f;       // Total terrain size in world units
    uint32_t tilesX = 32;               // Number of tiles at LOD0
    uint32_t tilesZ = 32;               // Number of tiles at LOD0
    uint32_t numLODLevels = 4;
    LODThresholds lodThresholds;
};

// Convert world position to tile coordinate at a given LOD
inline TileCoord worldToTileCoord(float worldX, float worldZ, uint32_t lod, const GridConfig& config) {
    // Tile count at this LOD (halves for each LOD level)
    uint32_t tilesAtLOD_X = config.tilesX >> lod;
    uint32_t tilesAtLOD_Z = config.tilesZ >> lod;

    if (tilesAtLOD_X == 0) tilesAtLOD_X = 1;
    if (tilesAtLOD_Z == 0) tilesAtLOD_Z = 1;

    float tileSizeX = config.terrainSize / static_cast<float>(tilesAtLOD_X);
    float tileSizeZ = config.terrainSize / static_cast<float>(tilesAtLOD_Z);

    TileCoord coord;
    coord.x = static_cast<int32_t>(std::floor(worldX / tileSizeX));
    coord.z = static_cast<int32_t>(std::floor(worldZ / tileSizeZ));

    // Clamp to valid range
    if (coord.x < 0) coord.x = 0;
    if (coord.z < 0) coord.z = 0;
    if (coord.x >= static_cast<int32_t>(tilesAtLOD_X)) coord.x = tilesAtLOD_X - 1;
    if (coord.z >= static_cast<int32_t>(tilesAtLOD_Z)) coord.z = tilesAtLOD_Z - 1;

    return coord;
}

// Get tile world bounds from tile coordinate and LOD
inline void getTileWorldBounds(TileCoord coord, uint32_t lod, const GridConfig& config,
                                float& minX, float& minZ, float& maxX, float& maxZ) {
    uint32_t tilesAtLOD_X = config.tilesX >> lod;
    uint32_t tilesAtLOD_Z = config.tilesZ >> lod;

    if (tilesAtLOD_X == 0) tilesAtLOD_X = 1;
    if (tilesAtLOD_Z == 0) tilesAtLOD_Z = 1;

    float tileSizeX = config.terrainSize / static_cast<float>(tilesAtLOD_X);
    float tileSizeZ = config.terrainSize / static_cast<float>(tilesAtLOD_Z);

    minX = coord.x * tileSizeX;
    minZ = coord.z * tileSizeZ;
    maxX = minX + tileSizeX;
    maxZ = minZ + tileSizeZ;
}

// Get the center of a tile in world coordinates
inline void getTileCenter(TileCoord coord, uint32_t lod, const GridConfig& config,
                          float& centerX, float& centerZ) {
    float minX, minZ, maxX, maxZ;
    getTileWorldBounds(coord, lod, config, minX, minZ, maxX, maxZ);
    centerX = (minX + maxX) * 0.5f;
    centerZ = (minZ + maxZ) * 0.5f;
}

// Get appropriate LOD level for distance from camera
inline uint32_t getLODForDistance(float distance, const LODThresholds& thresholds) {
    if (distance < thresholds.lod0Max) return 0;
    if (distance < thresholds.lod1Max) return 1;
    if (distance < thresholds.lod2Max) return 2;
    if (distance < thresholds.lod3Max) return 3;
    // Beyond lod3Max, use the coarsest LOD
    return thresholds.numLODLevels > 0 ? thresholds.numLODLevels - 1 : 0;
}

// Calculate distance from a point to the nearest edge of a tile
inline float distanceToTile(float worldX, float worldZ, TileCoord coord, uint32_t lod, const GridConfig& config) {
    float minX, minZ, maxX, maxZ;
    getTileWorldBounds(coord, lod, config, minX, minZ, maxX, maxZ);

    // Clamp point to tile bounds
    float clampedX = std::max(minX, std::min(worldX, maxX));
    float clampedZ = std::max(minZ, std::min(worldZ, maxZ));

    // Distance from point to clamped point (0 if inside tile)
    float dx = worldX - clampedX;
    float dz = worldZ - clampedZ;

    return std::sqrt(dx * dx + dz * dz);
}

// Make a unique 64-bit key for tile lookup (coord + LOD)
inline uint64_t makeTileKey(TileCoord coord, uint32_t lod) {
    // Pack: lod (8 bits) | x (28 bits) | z (28 bits)
    uint64_t key = static_cast<uint64_t>(lod & 0xFF) << 56;
    key |= static_cast<uint64_t>(coord.x & 0x0FFFFFFF) << 28;
    key |= static_cast<uint64_t>(coord.z & 0x0FFFFFFF);
    return key;
}

// Extract tile coordinate and LOD from key
inline void unpackTileKey(uint64_t key, TileCoord& coord, uint32_t& lod) {
    lod = static_cast<uint32_t>((key >> 56) & 0xFF);
    coord.x = static_cast<int32_t>((key >> 28) & 0x0FFFFFFF);
    coord.z = static_cast<int32_t>(key & 0x0FFFFFFF);
}

// Check if a point is inside any hole (analytical test)
inline bool isPointInHole(float x, float z, const std::vector<TerrainHole>& holes) {
    for (const auto& hole : holes) {
        float dx = x - hole.centerX;
        float dz = z - hole.centerZ;
        if (dx * dx + dz * dz <= hole.radius * hole.radius) {
            return true;
        }
    }
    return false;
}

// Rasterize holes into a mask for a tile region
// Returns mask where 255 = hole, 0 = solid
// Inflates hole radius by texel size to ensure GPU bilinear sampling hits marked texels
inline std::vector<uint8_t> rasterizeHolesForTile(
    float tileMinX, float tileMinZ, float tileMaxX, float tileMaxZ,
    uint32_t resolution, const std::vector<TerrainHole>& holes) {

    std::vector<uint8_t> mask(resolution * resolution, 0);

    if (holes.empty()) {
        return mask;
    }

    float texelSizeX = (tileMaxX - tileMinX) / static_cast<float>(resolution);
    float texelSizeZ = (tileMaxZ - tileMinZ) / static_cast<float>(resolution);

    // Inflate radius by half a texel to account for GPU bilinear interpolation.
    // Without this, a hole smaller than the texel size would only mark ~1 texel,
    // and bilinear sampling at positions offset from texel center would dilute
    // the hole value below the 0.5 discard threshold.
    // Using half texel provides a good balance between accuracy and visibility.
    float texelSize = std::max(texelSizeX, texelSizeZ);
    float inflation = texelSize * 0.5f;

    // Create inflated holes for rasterization
    std::vector<TerrainHole> inflatedHoles;
    inflatedHoles.reserve(holes.size());
    for (const auto& hole : holes) {
        inflatedHoles.push_back({hole.centerX, hole.centerZ, hole.radius + inflation});
    }

    for (uint32_t row = 0; row < resolution; ++row) {
        for (uint32_t col = 0; col < resolution; ++col) {
            // Sample at texel center
            float worldX = tileMinX + (col + 0.5f) * texelSizeX;
            float worldZ = tileMinZ + (row + 0.5f) * texelSizeZ;

            if (isPointInHole(worldX, worldZ, inflatedHoles)) {
                mask[row * resolution + col] = 255;
            }
        }
    }

    return mask;
}

// Get the number of tiles at a given LOD level
inline uint32_t getTilesAtLOD(uint32_t lod, uint32_t baseTilesX, uint32_t baseTilesZ) {
    uint32_t tilesX = baseTilesX >> lod;
    uint32_t tilesZ = baseTilesZ >> lod;
    if (tilesX == 0) tilesX = 1;
    if (tilesZ == 0) tilesZ = 1;
    return tilesX * tilesZ;
}

// Get tile size in world units at a given LOD
inline float getTileSizeAtLOD(uint32_t lod, float terrainSize, uint32_t baseTiles) {
    uint32_t tilesAtLOD = baseTiles >> lod;
    if (tilesAtLOD == 0) tilesAtLOD = 1;
    return terrainSize / static_cast<float>(tilesAtLOD);
}

// Check if a tile coordinate is valid at a given LOD
inline bool isValidTileCoord(TileCoord coord, uint32_t lod, const GridConfig& config) {
    uint32_t tilesAtLOD_X = config.tilesX >> lod;
    uint32_t tilesAtLOD_Z = config.tilesZ >> lod;

    if (tilesAtLOD_X == 0) tilesAtLOD_X = 1;
    if (tilesAtLOD_Z == 0) tilesAtLOD_Z = 1;

    return coord.x >= 0 && coord.x < static_cast<int32_t>(tilesAtLOD_X) &&
           coord.z >= 0 && coord.z < static_cast<int32_t>(tilesAtLOD_Z);
}

// Get all tile coordinates within a radius at a given LOD
inline std::vector<TileCoord> getTilesInRadius(float centerX, float centerZ, float radius,
                                                uint32_t lod, const GridConfig& config) {
    std::vector<TileCoord> tiles;

    uint32_t tilesAtLOD_X = config.tilesX >> lod;
    uint32_t tilesAtLOD_Z = config.tilesZ >> lod;
    if (tilesAtLOD_X == 0) tilesAtLOD_X = 1;
    if (tilesAtLOD_Z == 0) tilesAtLOD_Z = 1;

    float tileSizeX = config.terrainSize / static_cast<float>(tilesAtLOD_X);
    float tileSizeZ = config.terrainSize / static_cast<float>(tilesAtLOD_Z);

    // Calculate tile range to check
    int32_t minTileX = static_cast<int32_t>(std::floor((centerX - radius) / tileSizeX));
    int32_t maxTileX = static_cast<int32_t>(std::floor((centerX + radius) / tileSizeX));
    int32_t minTileZ = static_cast<int32_t>(std::floor((centerZ - radius) / tileSizeZ));
    int32_t maxTileZ = static_cast<int32_t>(std::floor((centerZ + radius) / tileSizeZ));

    // Clamp to valid range
    minTileX = std::max(0, minTileX);
    maxTileX = std::min(static_cast<int32_t>(tilesAtLOD_X - 1), maxTileX);
    minTileZ = std::max(0, minTileZ);
    maxTileZ = std::min(static_cast<int32_t>(tilesAtLOD_Z - 1), maxTileZ);

    for (int32_t z = minTileZ; z <= maxTileZ; ++z) {
        for (int32_t x = minTileX; x <= maxTileX; ++x) {
            TileCoord coord{x, z};
            // Check if tile is actually within radius (not just bounding box)
            if (distanceToTile(centerX, centerZ, coord, lod, config) <= radius) {
                tiles.push_back(coord);
            }
        }
    }

    return tiles;
}

} // namespace TileGrid
