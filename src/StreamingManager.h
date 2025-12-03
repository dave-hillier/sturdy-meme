#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <cstdint>

// Budget configuration for streaming systems
struct StreamingBudget {
    size_t maxGPUMemory = 256 * 1024 * 1024;    // 256 MB default
    size_t targetGPUMemory = 200 * 1024 * 1024; // Target to leave headroom
    uint32_t maxConcurrentLoads = 4;            // Max parallel load operations
    uint32_t maxLoadRequestsPerFrame = 2;       // Max new loads started per frame
    uint32_t maxUnloadsPerFrame = 4;            // Max unloads per frame
};

// Priority for loading (lower = higher priority)
struct LoadPriority {
    float distance;          // Distance to camera
    float importance;        // Multiplier (1.0 = normal, lower = more important)
    uint64_t requestFrame;   // Frame when requested (for tie-breaking)

    bool operator<(const LoadPriority& other) const {
        // Lower priority value = earlier in queue
        float myPriority = distance * importance;
        float otherPriority = other.distance * other.importance;
        if (myPriority != otherPriority) {
            return myPriority > otherPriority;  // Inverted for priority_queue
        }
        return requestFrame > other.requestFrame;
    }
};

// Base class for streaming managers
// Provides thread pool and priority queue infrastructure
class StreamingManager {
public:
    StreamingManager() = default;
    virtual ~StreamingManager();

    // Prevent copying
    StreamingManager(const StreamingManager&) = delete;
    StreamingManager& operator=(const StreamingManager&) = delete;

    // Initialize streaming manager with Vulkan context
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
        uint32_t numWorkerThreads = 2;
        StreamingBudget budget;
    };

    virtual bool init(const InitInfo& info);
    virtual void shutdown();

    // Update streaming state (call once per frame)
    virtual void update(const glm::vec3& cameraPos, uint64_t frameNumber) = 0;

    // Get current memory usage
    size_t getCurrentGPUMemoryUsage() const { return currentGPUMemory.load(); }
    size_t getMaxGPUMemoryBudget() const { return budget.maxGPUMemory; }

    // Check if over budget
    bool isOverBudget() const { return currentGPUMemory.load() > budget.maxGPUMemory; }

    // Get loading statistics
    uint32_t getPendingLoadCount() const;
    uint32_t getActiveLoadCount() const { return activeLoads.load(); }

protected:
    // Worker thread function
    void workerThreadFunc();

    // Add memory to tracking (call when GPU resource created)
    void addGPUMemory(size_t bytes) { currentGPUMemory.fetch_add(bytes); }

    // Remove memory from tracking (call when GPU resource destroyed)
    void removeGPUMemory(size_t bytes) { currentGPUMemory.fetch_sub(bytes); }

    // Submit work to background thread pool
    using WorkItem = std::function<void()>;
    void submitWork(WorkItem work, LoadPriority priority);

    // Process completed GPU uploads on main thread
    // Returns number of items processed
    virtual uint32_t processCompletedLoads() = 0;

    // Vulkan context
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Budget
    StreamingBudget budget;
    std::atomic<size_t> currentGPUMemory{0};

    // Thread pool
    std::vector<std::thread> workerThreads;
    std::atomic<bool> shutdownRequested{false};

    // Work queue (priority queue of work items)
    struct PrioritizedWork {
        WorkItem work;
        LoadPriority priority;

        bool operator<(const PrioritizedWork& other) const {
            return priority < other.priority;
        }
    };

    std::priority_queue<PrioritizedWork> workQueue;
    std::mutex workQueueMutex;
    std::condition_variable workQueueCV;
    std::atomic<uint32_t> activeLoads{0};
};
