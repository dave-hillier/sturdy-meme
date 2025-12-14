#pragma once

#include "VirtualTextureTypes.h"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_set>

namespace VirtualTexture {

/**
 * Async tile loader for virtual texture system.
 *
 * Manages a worker thread pool that loads tile images from disk.
 * Tiles are queued for loading and callbacks are invoked when ready.
 */
class VirtualTextureTileLoader {
public:
    using TileLoadedCallback = std::function<void(const LoadedTile&)>;

    VirtualTextureTileLoader() = default;
    ~VirtualTextureTileLoader();

    // Non-copyable
    VirtualTextureTileLoader(const VirtualTextureTileLoader&) = delete;
    VirtualTextureTileLoader& operator=(const VirtualTextureTileLoader&) = delete;

    /**
     * Initialize the tile loader
     * @param basePath Base path to tile directory (e.g., "assets/tiles")
     * @param workerCount Number of worker threads
     * @return true on success
     */
    bool init(const std::string& basePath, uint32_t workerCount = 2);

    /**
     * Shutdown the loader and wait for workers to finish
     */
    void shutdown();

    /**
     * Queue a tile for loading
     * @param id The tile to load
     * @param priority Lower value = higher priority
     */
    void queueTile(TileId id, int priority = 0);

    /**
     * Queue multiple tiles for loading
     */
    void queueTiles(const std::vector<TileId>& ids, int priority = 0);

    /**
     * Check if a tile is already queued or loading
     */
    bool isQueued(TileId id) const;

    /**
     * Cancel a pending tile load (if not yet started)
     */
    void cancelTile(TileId id);

    /**
     * Clear all pending tile loads
     */
    void clearQueue();

    /**
     * Get loaded tiles that are ready for upload
     * Transfers ownership of pixel data to caller
     */
    std::vector<LoadedTile> getLoadedTiles();

    /**
     * Set callback for when tiles finish loading
     * Callback is invoked from worker thread, use for signaling only
     */
    void setLoadedCallback(TileLoadedCallback callback);

    /**
     * Get statistics
     */
    uint32_t getPendingCount() const;
    uint32_t getLoadedCount() const;
    uint64_t getTotalBytesLoaded() const { return totalBytesLoaded.load(); }

private:
    struct LoadRequest {
        TileId id;
        int priority;

        bool operator<(const LoadRequest& other) const {
            // Higher priority value = lower priority in queue
            return priority > other.priority;
        }
    };

    std::string basePath;
    std::vector<std::thread> workers;
    std::atomic<bool> running{false};

    // Request queue
    mutable std::mutex queueMutex;
    std::priority_queue<LoadRequest> requestQueue;
    std::unordered_set<uint32_t> queuedTiles; // For quick lookup
    std::condition_variable queueCondition;

    // Loaded tiles ready for upload
    mutable std::mutex loadedMutex;
    std::vector<LoadedTile> loadedTiles;

    TileLoadedCallback loadedCallback;
    std::atomic<uint64_t> totalBytesLoaded{0};

    void workerLoop();
    bool loadTileFromDisk(TileId id, LoadedTile& tile);
    std::string getTilePath(TileId id) const;
};

} // namespace VirtualTexture
