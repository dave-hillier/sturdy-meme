#pragma once

#include "TerrainTileCache.h"
#include "PhysicsSystem.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <cstdint>

// Forward declaration
class DebugLineSystem;

// Physics tile tracking - stores physics body for each terrain tile
struct PhysicsTile {
    TileCoord coord;
    uint32_t lod = 0;
    PhysicsBodyID bodyID = INVALID_BODY_ID;
};

// Manages streaming terrain physics collision tiles
// Creates Jolt heightfield bodies for LOD0 tiles near the player
// Limited to a configurable radius (default 512m) for ~4 active tiles
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
    // Only loads LOD0 tiles within the specified radius (~4 tiles at 512m)
    void update(const glm::vec3& playerPos, float radius = 512.0f);

    // Preload all physics tiles needed for a position (blocking, no per-frame limit)
    // Call this before spawning character to ensure collision is ready
    void preloadTilesAt(const glm::vec3& playerPos, float radius = 512.0f);

    // Get count of active physics tiles
    uint32_t getActivePhysicsTileCount() const { return static_cast<uint32_t>(physicsTiles.size()); }

    // Debug visualization - draw tile bounds as wireframe boxes (much faster than Jolt's heightfield rendering)
    void drawTileBounds(DebugLineSystem& debugLines) const;

    // Access to tile bounds for iteration
    const std::unordered_map<uint64_t, PhysicsTile>& getPhysicsTiles() const { return physicsTiles; }

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

    // Get LOD0 tiles within radius of player position
    void getDesiredTiles(const glm::vec3& playerPos, float radius,
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

    // LOD0 grid dimensions (only LOD0 is used for physics)
    uint32_t lod0TilesX = 32;  // LOD0: 32x32 tiles
    uint32_t lod0TilesZ = 32;

    // Physics only uses LOD0 (highest detail)
    static constexpr uint32_t HIGH_DETAIL_LOD = 0;
};
