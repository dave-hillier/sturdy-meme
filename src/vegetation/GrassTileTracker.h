#pragma once

#include "GrassTile.h"
#include "GrassConstants.h"
#include "GrassLODStrategy.h"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <memory>

/**
 * GrassTileTracker - Pure logic class for grass tile management
 *
 * This class handles:
 * - Determining which tiles should be active based on camera position
 * - LOD level decisions (using configurable LOD strategy)
 * - Load/unload requests (returns requests, doesn't execute them)
 * - Tile coordinate calculations
 *
 * NO Vulkan dependencies - can be unit tested independently.
 */
class GrassTileTracker {
public:
    using TileCoord = GrassTile::TileCoord;
    using TileCoordHash = GrassTile::TileCoordHash;

    /**
     * Request for tile loading or unloading
     */
    struct TileRequest {
        TileCoord coord;
        bool load;  // true = load, false = unload
        float priority;  // Higher priority = load first (distance-based)
    };

    /**
     * Result of an update call
     */
    struct UpdateResult {
        std::vector<TileRequest> loadRequests;    // Tiles to load
        std::vector<TileRequest> unloadRequests;  // Tiles to unload
        std::vector<TileCoord> activeTiles;       // All currently active tiles (sorted)
    };

    GrassTileTracker();

    /**
     * Set the LOD strategy (takes ownership)
     * If null, uses default strategy
     */
    void setLODStrategy(std::unique_ptr<IGrassLODStrategy> strategy);

    /**
     * Get the current LOD strategy
     */
    const IGrassLODStrategy* getLODStrategy() const { return lodStrategy_.get(); }

    /**
     * Update active tiles based on camera position
     * Returns load/unload requests and the current active tile set
     *
     * @param cameraPos Camera world position
     * @param currentFrame Current frame number (for unload safety)
     * @param framesInFlight Number of frames in flight (for unload safety)
     * @return UpdateResult with load/unload requests and active tiles
     */
    UpdateResult update(const glm::vec3& cameraPos, uint64_t currentFrame, uint32_t framesInFlight);

    /**
     * Check if a tile coordinate is currently active
     */
    bool isTileActive(const TileCoord& coord) const;

    /**
     * Get all active tiles at a specific LOD level
     */
    std::vector<TileCoord> getActiveTilesAtLod(uint32_t lod) const;

    /**
     * Get the current camera tile (LOD 0)
     */
    TileCoord getCurrentCameraTile() const { return currentCameraTile_; }

    /**
     * Mark a tile as loaded (adds to tracking set)
     */
    void markTileLoaded(const TileCoord& coord, uint64_t frameNumber);

    /**
     * Mark a tile as unloaded (removes from tracking set)
     */
    void markTileUnloaded(const TileCoord& coord);

    /**
     * Get last used frame for a tile (for unload decisions)
     */
    uint64_t getTileLastUsedFrame(const TileCoord& coord) const;

    /**
     * Check if a tile can be safely unloaded (not used by GPU)
     */
    bool canUnloadTile(const TileCoord& coord, uint64_t currentFrame, uint32_t framesInFlight) const;

    /**
     * Calculate which tile coordinate contains a world position at a given LOD level
     */
    static TileCoord worldToTileCoord(const glm::vec2& worldPos, uint32_t lod);

    /**
     * Calculate priority for a tile (higher = closer to camera = load first)
     */
    static float calculateTilePriority(const TileCoord& coord, const glm::vec2& cameraXZ);

private:
    /**
     * Check if a world position is covered by higher LOD (more detailed) tiles
     */
    bool isCoveredByHigherLod(const glm::vec2& worldPos, uint32_t currentLod,
                               const glm::vec2& cameraXZ) const;

    /**
     * Get desired tiles for a specific LOD level
     */
    std::vector<TileCoord> getDesiredTilesForLod(const glm::vec2& cameraXZ, uint32_t lod) const;

    /**
     * Calculate unload radius for a specific LOD level
     */
    float getUnloadRadiusForLod(uint32_t lod) const;

    /**
     * Calculate tile coordinate using current LOD strategy
     */
    TileCoord worldToTileCoordWithStrategy(const glm::vec2& worldPos, uint32_t lod) const;

    // Tracking data for loaded tiles
    struct TileInfo {
        uint64_t lastUsedFrame = 0;
    };

    std::unordered_map<TileCoord, TileInfo, TileCoordHash> loadedTiles_;
    std::unordered_set<TileCoord, TileCoordHash> activeTileSet_;
    TileCoord currentCameraTile_{0, 0, 0};

    // LOD strategy (owned)
    std::unique_ptr<IGrassLODStrategy> lodStrategy_;
};

// Inline implementations

inline GrassTileTracker::TileCoord GrassTileTracker::worldToTileCoord(const glm::vec2& worldPos, uint32_t lod) {
    float tileSize = GrassConstants::getTileSizeForLod(lod);
    return {
        static_cast<int>(std::floor(worldPos.x / tileSize)),
        static_cast<int>(std::floor(worldPos.y / tileSize)),
        lod
    };
}

inline float GrassTileTracker::calculateTilePriority(const TileCoord& coord, const glm::vec2& cameraXZ) {
    // Higher priority = closer to camera
    // LOD 0 tiles get highest base priority, LOD 2 lowest
    float tileSize = GrassConstants::getTileSizeForLod(coord.lod);
    glm::vec2 tileCenter(
        static_cast<float>(coord.x) * tileSize + tileSize * 0.5f,
        static_cast<float>(coord.z) * tileSize + tileSize * 0.5f
    );
    float distSq = glm::dot(tileCenter - cameraXZ, tileCenter - cameraXZ);

    // Priority: base (10000 for LOD0, 1000 for LOD1, 100 for LOD2) minus distance
    float basePriority = 10000.0f / (1.0f + static_cast<float>(coord.lod));
    return basePriority - std::sqrt(distSq);
}

inline bool GrassTileTracker::isTileActive(const TileCoord& coord) const {
    return activeTileSet_.find(coord) != activeTileSet_.end();
}

inline void GrassTileTracker::markTileLoaded(const TileCoord& coord, uint64_t frameNumber) {
    loadedTiles_[coord].lastUsedFrame = frameNumber;
}

inline void GrassTileTracker::markTileUnloaded(const TileCoord& coord) {
    loadedTiles_.erase(coord);
    activeTileSet_.erase(coord);
}

inline uint64_t GrassTileTracker::getTileLastUsedFrame(const TileCoord& coord) const {
    auto it = loadedTiles_.find(coord);
    return (it != loadedTiles_.end()) ? it->second.lastUsedFrame : 0;
}

inline bool GrassTileTracker::canUnloadTile(const TileCoord& coord, uint64_t currentFrame,
                                             uint32_t framesInFlight) const {
    uint64_t lastUsed = getTileLastUsedFrame(coord);
    return (currentFrame - lastUsed) > framesInFlight;
}
