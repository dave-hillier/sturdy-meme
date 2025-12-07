#pragma once

#include "TerrainTileCache.h"
#include "PhysicsSystem.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <cstdint>

// Physics tile tracking - stores physics body for each terrain tile
struct PhysicsTile {
    TileCoord coord;
    uint32_t lod = 0;
    PhysicsBodyID bodyID = INVALID_BODY_ID;
};

// Manages streaming terrain physics collision tiles
// Creates Jolt heightfield bodies for tiles near the player (high detail)
// and coarse LOD tiles for distant terrain coverage
class TerrainPhysicsTiles {
public:
    TerrainPhysicsTiles() = default;
    ~TerrainPhysicsTiles() = default;

    // Initialize with physics world and terrain tile cache
    void init(PhysicsWorld* physics, TerrainTileCache* tileCache,
              float terrainSize, float heightScale, float minAltitude);

    // Cleanup all physics bodies
    void destroy();

    // Update physics tiles based on player position
    // Loads LOD0 tiles within highDetailRadius, LOD3 tiles for rest of terrain
    void update(const glm::vec3& playerPos, float highDetailRadius = 1000.0f);

    // Get count of active physics tiles
    uint32_t getActivePhysicsTileCount() const { return static_cast<uint32_t>(physicsTiles.size()); }

private:
    // Create/destroy physics body for a tile
    void createPhysicsForTile(TileCoord coord, uint32_t lod);
    void destroyPhysicsForTile(uint64_t key);

    // Make unique key for tile lookup (same as TerrainTileCache)
    uint64_t makeTileKey(TileCoord coord, uint32_t lod) const;

    // Calculate tile world bounds for a given LOD
    void getTileWorldBounds(TileCoord coord, uint32_t lod,
                            float& outMinX, float& outMinZ,
                            float& outMaxX, float& outMaxZ) const;

    // Get tiles that should have physics at given player position
    void getDesiredTiles(const glm::vec3& playerPos, float highDetailRadius,
                         std::vector<std::pair<TileCoord, uint32_t>>& outTiles) const;

    // Track physics bodies by tile key
    std::unordered_map<uint64_t, PhysicsTile> physicsTiles;

    // References (not owned)
    PhysicsWorld* physics = nullptr;
    TerrainTileCache* tileCache = nullptr;

    // Terrain parameters
    float terrainSize = 16384.0f;
    float heightScale = 235.0f;
    float minAltitude = -15.0f;

    // LOD grid dimensions
    uint32_t lod0TilesX = 32;  // LOD0: 32x32 tiles
    uint32_t lod0TilesZ = 32;
    uint32_t lod3TilesX = 4;   // LOD3: 4x4 tiles
    uint32_t lod3TilesZ = 4;

    // Physics tile LOD levels
    static constexpr uint32_t HIGH_DETAIL_LOD = 0;  // LOD0 for near player
    static constexpr uint32_t LOW_DETAIL_LOD = 3;   // LOD3 for distant coverage
};
