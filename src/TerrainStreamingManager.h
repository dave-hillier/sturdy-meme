#pragma once

#include "StreamingManager.h"
#include "TerrainTile.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

// LOD level configuration
struct LODLevelConfig {
    float minDistance = 0.0f;      // Minimum distance for this LOD (inner boundary)
    float maxDistance = 512.0f;    // Maximum distance for this LOD (outer boundary)
    float unloadMargin = 64.0f;    // Hysteresis margin for unloading
};

// Configuration for terrain streaming
struct TerrainStreamingConfig {
    TerrainTileConfig tileConfig;                 // Per-tile configuration

    // LOD level ranges (distance from camera where each LOD is used)
    // LOD 0 = highest detail (closest), LOD N = lowest detail (farthest)
    // Default: LOD 0: 0-512m, LOD 1: 512-2048m, LOD 2: 2048-8192m, LOD 3: 8192m+
    std::vector<LODLevelConfig> lodLevels = {
        {0.0f, 512.0f, 64.0f},       // LOD 0: high detail, near
        {512.0f, 2048.0f, 128.0f},   // LOD 1: medium detail
        {2048.0f, 8192.0f, 256.0f},  // LOD 2: low detail
        {8192.0f, 32768.0f, 512.0f}  // LOD 3: very low detail, far
    };

    uint32_t maxLoadedTiles = 128;                // Maximum number of loaded tiles (across all LODs)
    StreamingBudget budget = {
        .maxGPUMemory = 256 * 1024 * 1024,        // 256 MB for terrain (more for multi-LOD)
        .targetGPUMemory = 200 * 1024 * 1024,
        .maxConcurrentLoads = 4,
        .maxLoadRequestsPerFrame = 4,
        .maxUnloadsPerFrame = 4
    };
};

// Manages streaming of terrain tiles based on camera position
class TerrainStreamingManager : public StreamingManager {
public:
    TerrainStreamingManager() = default;
    ~TerrainStreamingManager() override;

    // Initialize with terrain-specific configuration
    bool init(const StreamingManager::InitInfo& baseInfo,
              const TerrainStreamingConfig& terrainConfig);

    // Shutdown and cleanup
    void shutdown() override;

    // Update streaming state based on camera position
    void update(const glm::vec3& cameraPos, uint64_t frameNumber) override;

    // Get tiles that are loaded and visible
    const std::vector<TerrainTile*>& getVisibleTiles() const { return visibleTiles; }

    // Get all loaded tiles (for rendering)
    std::vector<TerrainTile*> getLoadedTiles() const;

    // Get height at world position (queries appropriate tile)
    float getHeightAt(float worldX, float worldZ) const;

    // Check if a tile exists at the given world position
    bool hasTileAt(float worldX, float worldZ) const;

    // Get tile at world position (may be null if not loaded)
    TerrainTile* getTileAt(float worldX, float worldZ) const;

    // Statistics
    uint32_t getLoadedTileCount() const;
    uint32_t getLoadingTileCount() const;

    // Configuration access
    const TerrainStreamingConfig& getConfig() const { return config; }

protected:
    // Process completed loads (called from main thread)
    uint32_t processCompletedLoads() override;

private:
    // Convert world position to tile coordinate for a specific LOD level
    TerrainTile::Coord worldToTileCoord(float worldX, float worldZ, uint32_t lodLevel) const;

    // Get tile size for a specific LOD level
    float getTileSizeForLOD(uint32_t lodLevel) const;

    // Get appropriate LOD level for a given distance
    uint32_t getLODForDistance(float distance) const;

    // Get or create tile at coordinate
    TerrainTile* getOrCreateTile(const TerrainTile::Coord& coord);

    // Request tile to be loaded
    void requestTileLoad(TerrainTile* tile, float distance, uint64_t frameNumber);

    // Determine which tiles should be loaded based on camera (for all LOD levels)
    void updateTileRequests(const glm::vec3& cameraPos, uint64_t frameNumber);

    // Update tile requests for a specific LOD level
    void updateTileRequestsForLOD(const glm::vec3& cameraPos, uint64_t frameNumber,
                                   uint32_t lodLevel, float minDist, float maxDist);

    // Check if a higher-detail LOD tile covers this position
    bool hasHigherLODCoverage(float worldX, float worldZ, uint32_t currentLOD) const;

    // Unload tiles that are too far or over budget
    void evictTiles(const glm::vec3& cameraPos, uint64_t frameNumber);

    // Update visible tiles list (excluding tiles covered by higher LOD)
    void updateVisibleTiles(const glm::vec3& cameraPos);

    // Configuration
    TerrainStreamingConfig config;

    // All tiles (pooled, keyed by coordinate)
    std::unordered_map<TerrainTile::Coord, std::unique_ptr<TerrainTile>, TileCoordHash> tiles;

    // Currently visible tiles (updated each frame)
    std::vector<TerrainTile*> visibleTiles;

    // Tiles pending GPU resource creation (loaded on background thread, need GPU upload)
    std::vector<TerrainTile*> pendingGPUUpload;
    std::mutex pendingUploadMutex;

    // Track tiles currently being loaded (to avoid duplicate requests)
    std::unordered_set<TerrainTile::Coord, TileCoordHash> loadingTiles;
    std::mutex loadingTilesMutex;

    // Last camera position (for incremental updates)
    glm::vec3 lastCameraPos = glm::vec3(0.0f);
    bool hasLastCameraPos = false;
};
