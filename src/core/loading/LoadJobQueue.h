#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <variant>

/**
 * LoadJobQueue - Generic async job queue for startup loading
 *
 * Design:
 * - Worker threads execute jobs that produce CPU-side staged data
 * - Main thread polls for completed jobs and performs GPU uploads
 * - Jobs are prioritized (lower value = higher priority)
 * - Progress tracking for loading screen updates
 *
 * Pattern matches VirtualTextureTileLoader but generalized for any job type.
 */

namespace Loading {

/**
 * Base class for staged resources (CPU-side data ready for GPU upload)
 */
struct StagedResource {
    virtual ~StagedResource() = default;
    virtual size_t getMemorySize() const = 0;
    virtual const char* getTypeName() const = 0;
};

/**
 * Staged texture data
 */
struct StagedTexture : public StagedResource {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 4;
    bool srgb = false;
    std::string name;

    size_t getMemorySize() const override { return pixels.size(); }
    const char* getTypeName() const override { return "Texture"; }
};

/**
 * Staged mesh data (vertices + indices)
 */
struct StagedMesh : public StagedResource {
    std::vector<uint8_t> vertexData;
    std::vector<uint8_t> indexData;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
    std::string name;

    size_t getMemorySize() const override { return vertexData.size() + indexData.size(); }
    const char* getTypeName() const override { return "Mesh"; }
};

/**
 * Staged heightmap data
 */
struct StagedHeightmap : public StagedResource {
    std::vector<uint16_t> heights;  // 16-bit height values
    uint32_t width = 0;
    uint32_t height = 0;
    std::string name;

    size_t getMemorySize() const override { return heights.size() * sizeof(uint16_t); }
    const char* getTypeName() const override { return "Heightmap"; }
};

/**
 * Staged generic buffer data
 */
struct StagedBuffer : public StagedResource {
    std::vector<uint8_t> data;
    std::string name;

    size_t getMemorySize() const override { return data.size(); }
    const char* getTypeName() const override { return "Buffer"; }
};

/**
 * Staged tree mesh data (for threaded tree generation)
 * Contains CPU-side geometry ready for GPU upload
 */
struct StagedTreeMesh : public StagedResource {
    // Branch mesh geometry
    std::vector<uint8_t> branchVertexData;  // Vertex data as raw bytes
    std::vector<uint32_t> branchIndices;
    uint32_t branchVertexCount = 0;
    uint32_t branchVertexStride = 0;

    // Leaf instance data (32 bytes per instance: vec4 positionAndSize + vec4 orientation)
    std::vector<uint8_t> leafInstanceData;
    uint32_t leafInstanceCount = 0;

    // Tree placement info
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    float rotation = 0.0f;
    float scale = 1.0f;

    // Tree options index (references pre-loaded options)
    uint32_t optionsIndex = 0;

    // For impostor archetype assignment
    uint32_t archetypeIndex = 0;

    std::string name;

    size_t getMemorySize() const override {
        return branchVertexData.size() + branchIndices.size() * sizeof(uint32_t) + leafInstanceData.size();
    }
    const char* getTypeName() const override { return "TreeMesh"; }
};

/**
 * Result of a completed load job
 */
struct LoadJobResult {
    std::string jobId;
    std::string phase;  // For progress display (e.g., "Terrain", "Textures")
    std::unique_ptr<StagedResource> resource;
    bool success = false;
    std::string error;
};

/**
 * A job to be executed by the worker thread
 */
struct LoadJob {
    std::string id;
    std::string phase;
    int priority = 0;  // Lower = higher priority
    std::function<std::unique_ptr<StagedResource>()> execute;

    bool operator<(const LoadJob& other) const {
        return priority > other.priority;  // Min-heap for priority queue
    }
};

/**
 * Progress information for loading screen
 */
struct LoadProgress {
    std::string currentPhase;
    std::string currentJob;
    uint32_t completedJobs = 0;
    uint32_t totalJobs = 0;
    uint64_t bytesLoaded = 0;

    float getProgress() const {
        return totalJobs > 0 ? static_cast<float>(completedJobs) / totalJobs : 0.0f;
    }
};

/**
 * LoadJobQueue - Thread-safe job queue with worker pool
 */
class LoadJobQueue {
public:
    /**
     * Factory: Create and start the job queue with worker threads
     */
    static std::unique_ptr<LoadJobQueue> create(uint32_t workerCount = 2);

    ~LoadJobQueue();

    // Non-copyable, non-movable
    LoadJobQueue(const LoadJobQueue&) = delete;
    LoadJobQueue& operator=(const LoadJobQueue&) = delete;
    LoadJobQueue(LoadJobQueue&&) = delete;
    LoadJobQueue& operator=(LoadJobQueue&&) = delete;

    /**
     * Submit a job to the queue
     */
    void submit(LoadJob job);

    /**
     * Submit multiple jobs at once
     */
    void submitBatch(std::vector<LoadJob> jobs);

    /**
     * Set total expected job count (for progress calculation)
     */
    void setTotalJobs(uint32_t count);

    /**
     * Get completed job results (transfers ownership)
     * Call this from main thread to get staged resources for GPU upload
     */
    std::vector<LoadJobResult> getCompletedJobs();

    /**
     * Check if all jobs are complete
     */
    bool isComplete() const;

    /**
     * Get current progress (thread-safe)
     */
    LoadProgress getProgress() const;

    /**
     * Wait for all jobs to complete (blocks)
     */
    void waitForAll();

    /**
     * Cancel all pending jobs and stop workers
     */
    void shutdown();

private:
    LoadJobQueue() = default;
    bool init(uint32_t workerCount);
    void workerLoop();

    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    // Job queue (protected by queueMutex_)
    mutable std::mutex queueMutex_;
    std::priority_queue<LoadJob> jobQueue_;
    std::condition_variable queueCondition_;

    // Completed results (protected by resultsMutex_)
    mutable std::mutex resultsMutex_;
    std::vector<LoadJobResult> completedResults_;

    // Progress tracking (atomic for lock-free reads)
    std::atomic<uint32_t> totalJobs_{0};
    std::atomic<uint32_t> completedJobs_{0};
    std::atomic<uint64_t> bytesLoaded_{0};
    mutable std::mutex progressMutex_;
    std::string currentPhase_;
    std::string currentJob_;
};

} // namespace Loading
