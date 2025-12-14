#pragma once

#include "PhysicsSystem.h"
#include "TerrainTileCache.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

// Manages tile-based physics terrain that loads high-fidelity collision
// only near the player position, with hysteresis to prevent thrashing.
class PhysicsTerrain {
public:
    struct Config {
        float loadRadius = 512.0f;      // Distance to load tiles
        float unloadRadius = 768.0f;    // Distance to unload tiles (hysteresis)
        // Note: tileSize, terrainSize, and heightScale are obtained from TerrainTileCache
    };

    PhysicsTerrain() = default;
    ~PhysicsTerrain() = default;

    // Initialize with physics world and terrain tile cache
    void init(PhysicsWorld* physics, TerrainTileCache* tileCache, const Config& config);

    // Update tiles based on player position
    // Call this each frame with the current player/camera position
    void update(const glm::vec3& playerPos);

    // Get the number of currently loaded physics tiles
    size_t getLoadedTileCount() const { return loadedPhysicsTiles.size(); }

    // Get config
    const Config& getConfig() const { return config; }

private:
    // A physics tile with its body ID and world bounds
    struct PhysicsTile {
        TileCoord coord;
        PhysicsBodyID bodyId = INVALID_BODY_ID;
        float worldMinX = 0.0f;
        float worldMinZ = 0.0f;
        float worldMaxX = 0.0f;
        float worldMaxZ = 0.0f;
    };

    // Convert world position to tile coordinate
    TileCoord worldToTileCoord(float worldX, float worldZ) const;

    // Get the center position of a tile in world coordinates
    glm::vec2 getTileCenter(TileCoord coord) const;

    // Load a physics tile from the terrain tile cache
    bool loadTile(TileCoord coord);

    // Unload a physics tile
    void unloadTile(TileCoord coord);

    // Make a unique key for tile lookup
    uint64_t makeTileKey(TileCoord coord) const;

    PhysicsWorld* physicsWorld = nullptr;
    TerrainTileCache* terrainTileCache = nullptr;
    Config config;

    // Currently loaded physics tiles
    std::unordered_map<uint64_t, PhysicsTile> loadedPhysicsTiles;

    // Track last update position for hysteresis
    glm::vec3 lastUpdatePos{0.0f};
    bool hasUpdatedOnce = false;
};
