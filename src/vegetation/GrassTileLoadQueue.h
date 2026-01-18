#pragma once

#include "GrassTile.h"
#include <glm/glm.hpp>
#include <vector>
#include <queue>
#include <unordered_set>
#include <optional>

/**
 * GrassTileLoadQueue - Priority queue for async tile loading
 *
 * This class manages the loading of grass tiles with:
 * - Priority-based loading (closer/more visible tiles first)
 * - Per-frame budget limiting to prevent hitches
 * - Teleportation detection for queue clearing
 * - Cancel support for tiles that are no longer needed
 *
 * NO Vulkan dependencies - can be unit tested independently.
 */
class GrassTileLoadQueue {
public:
    using TileCoord = GrassTile::TileCoord;
    using TileCoordHash = GrassTile::TileCoordHash;

    /**
     * Load request with priority information
     */
    struct LoadRequest {
        TileCoord coord;
        float priority;  // Higher = load first

        bool operator<(const LoadRequest& other) const {
            // For std::priority_queue, lower priority value = lower in queue
            // We want higher priority first, so invert
            return priority < other.priority;
        }
    };

    /**
     * Configuration for the load queue
     */
    struct Config {
        uint32_t maxLoadsPerFrame = 2;       // Max tiles to load per frame
        float teleportThreshold = 500.0f;     // Distance to detect teleportation
        bool clearOnTeleport = true;          // Clear queue on teleport detection
    };

    GrassTileLoadQueue() = default;

    /**
     * Set configuration
     */
    void setConfig(const Config& config) { config_ = config; }
    const Config& getConfig() const { return config_; }
    Config& getConfig() { return config_; }

    /**
     * Get max loads per frame
     */
    uint32_t getMaxLoadsPerFrame() const { return config_.maxLoadsPerFrame; }

    /**
     * Enqueue a tile for loading with priority
     * @param coord Tile coordinate
     * @param priority Higher = load first (typically distance-based)
     */
    void enqueue(const TileCoord& coord, float priority);

    /**
     * Enqueue multiple tiles at once
     */
    void enqueueMultiple(const std::vector<LoadRequest>& requests);

    /**
     * Cancel a pending load request
     */
    void cancel(const TileCoord& coord);

    /**
     * Get the next tile(s) to load this frame
     * Respects maxLoadsPerFrame budget
     * @return Vector of tiles to load (up to maxLoadsPerFrame)
     */
    std::vector<TileCoord> dequeueForFrame();

    /**
     * Check if a tile is pending load
     */
    bool isPending(const TileCoord& coord) const;

    /**
     * Get number of pending loads
     */
    size_t getPendingCount() const { return pendingSet_.size(); }

    /**
     * Clear all pending loads
     */
    void clear();

    /**
     * Update with camera position for teleport detection
     * @param cameraPos Current camera position
     * @return true if teleport was detected and queue was cleared
     */
    bool updateCameraPosition(const glm::vec3& cameraPos);

    /**
     * Re-prioritize all pending tiles based on new camera position
     * Call this after camera movement to ensure closest tiles load first
     */
    void reprioritize(const glm::vec2& cameraXZ);

    /**
     * Get current load budget (how many more can be loaded this frame)
     */
    uint32_t getRemainingBudget() const { return remainingBudget_; }

    /**
     * Reset frame budget (call at start of each frame)
     */
    void resetFrameBudget() { remainingBudget_ = config_.maxLoadsPerFrame; }

private:
    Config config_;

    // Priority queue for pending loads
    std::priority_queue<LoadRequest> loadQueue_;

    // Set for O(1) lookup of pending tiles
    std::unordered_set<TileCoord, TileCoordHash> pendingSet_;

    // Set of cancelled tiles (checked during dequeue)
    std::unordered_set<TileCoord, TileCoordHash> cancelledSet_;

    // Last camera position for teleport detection
    glm::vec3 lastCameraPos_{0.0f};
    bool hasCameraPos_ = false;

    // Remaining budget for this frame
    uint32_t remainingBudget_ = 0;
};

// Inline implementations

inline void GrassTileLoadQueue::enqueue(const TileCoord& coord, float priority) {
    // Skip if already pending
    if (pendingSet_.find(coord) != pendingSet_.end()) {
        return;
    }

    loadQueue_.push({coord, priority});
    pendingSet_.insert(coord);
}

inline void GrassTileLoadQueue::enqueueMultiple(const std::vector<LoadRequest>& requests) {
    for (const auto& req : requests) {
        enqueue(req.coord, req.priority);
    }
}

inline void GrassTileLoadQueue::cancel(const TileCoord& coord) {
    // Can't remove from priority_queue directly, mark as cancelled
    if (pendingSet_.find(coord) != pendingSet_.end()) {
        cancelledSet_.insert(coord);
        pendingSet_.erase(coord);
    }
}

inline bool GrassTileLoadQueue::isPending(const TileCoord& coord) const {
    return pendingSet_.find(coord) != pendingSet_.end();
}

inline void GrassTileLoadQueue::clear() {
    // Clear the priority queue
    while (!loadQueue_.empty()) {
        loadQueue_.pop();
    }
    pendingSet_.clear();
    cancelledSet_.clear();
}

inline std::vector<GrassTileLoadQueue::TileCoord> GrassTileLoadQueue::dequeueForFrame() {
    std::vector<TileCoord> result;

    while (!loadQueue_.empty() && remainingBudget_ > 0) {
        LoadRequest req = loadQueue_.top();
        loadQueue_.pop();

        // Skip cancelled tiles
        if (cancelledSet_.find(req.coord) != cancelledSet_.end()) {
            cancelledSet_.erase(req.coord);
            continue;
        }

        // Skip tiles no longer pending (already loaded or cancelled)
        if (pendingSet_.find(req.coord) == pendingSet_.end()) {
            continue;
        }

        result.push_back(req.coord);
        pendingSet_.erase(req.coord);
        --remainingBudget_;
    }

    return result;
}

inline bool GrassTileLoadQueue::updateCameraPosition(const glm::vec3& cameraPos) {
    if (!hasCameraPos_) {
        lastCameraPos_ = cameraPos;
        hasCameraPos_ = true;
        return false;
    }

    glm::vec3 diff = cameraPos - lastCameraPos_;
    float distSq = glm::dot(diff, diff);
    float thresholdSq = config_.teleportThreshold * config_.teleportThreshold;

    lastCameraPos_ = cameraPos;

    if (distSq > thresholdSq && config_.clearOnTeleport) {
        clear();
        return true;  // Teleport detected
    }

    return false;
}

inline void GrassTileLoadQueue::reprioritize(const glm::vec2& cameraXZ) {
    // Rebuild the queue with new priorities
    std::vector<LoadRequest> pending;
    pending.reserve(loadQueue_.size());

    while (!loadQueue_.empty()) {
        LoadRequest req = loadQueue_.top();
        loadQueue_.pop();

        // Skip cancelled tiles
        if (cancelledSet_.find(req.coord) != cancelledSet_.end()) {
            continue;
        }

        // Recalculate priority based on new camera position
        float tileSize = GrassConstants::getTileSizeForLod(req.coord.lod);
        glm::vec2 tileCenter(
            static_cast<float>(req.coord.x) * tileSize + tileSize * 0.5f,
            static_cast<float>(req.coord.z) * tileSize + tileSize * 0.5f
        );
        float distSq = glm::dot(tileCenter - cameraXZ, tileCenter - cameraXZ);

        // Priority: base (10000 for LOD0, etc) minus distance
        float basePriority = 10000.0f / (1.0f + static_cast<float>(req.coord.lod));
        req.priority = basePriority - std::sqrt(distSq);

        pending.push_back(req);
    }

    // Rebuild queue
    cancelledSet_.clear();
    for (const auto& req : pending) {
        loadQueue_.push(req);
    }
}
