#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "PhysicsSystem.h"

class TerrainTileCache;
struct TileCoord;

struct PhysicsTileEntry {
    int32_t tileX;
    int32_t tileZ;
    uint32_t lod;
    PhysicsBodyID bodyID;
    float worldMinX;
    float worldMinZ;
    float worldMaxX;
    float worldMaxZ;
};

class PhysicsTerrainTileManager {
public:
    struct Config {
        float loadRadius = 1000.0f;
        float unloadRadius = 1200.0f;
        uint32_t maxTilesPerFrame = 2;
        float terrainSize = 16384.0f;
        float heightScale = 235.0f;
    };

    PhysicsTerrainTileManager() = default;
    ~PhysicsTerrainTileManager() = default;

    bool init(PhysicsWorld& physics, TerrainTileCache& tileCache, const Config& config);
    void update(const glm::vec3& playerPosition);
    void cleanup();

    uint32_t getLoadedTileCount() const { return static_cast<uint32_t>(loadedTiles_.size()); }
    const Config& getConfig() const { return config_; }

private:
    uint64_t makeTileKey(int32_t tileX, int32_t tileZ, uint32_t lod) const;
    bool loadPhysicsTile(int32_t tileX, int32_t tileZ, uint32_t lod);
    void unloadPhysicsTile(uint64_t tileKey);

    struct TileRequest {
        int32_t tileX;
        int32_t tileZ;
        uint32_t lod;
    };
    std::vector<TileRequest> calculateRequiredTiles(const glm::vec3& position) const;

    PhysicsWorld* physics_ = nullptr;
    TerrainTileCache* tileCache_ = nullptr;
    Config config_;

    std::unordered_map<uint64_t, PhysicsTileEntry> loadedTiles_;
};
