#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class VulkanContext;
class LoadingRenderer;

namespace Loading {

/**
 * SystemInitTask - Represents a system initialization that can be split into
 * CPU work (background thread) and GPU work (main thread).
 *
 * The separation allows:
 * - CPU work: file loading, mesh generation, data processing
 * - GPU work: buffer uploads, texture creation, pipeline setup
 */
struct SystemInitTask {
    std::string id;                    // Unique identifier
    std::string displayName;           // Human-readable name for progress display
    std::vector<std::string> dependencies;  // IDs of tasks this depends on

    // CPU work - runs on background thread
    // Returns true on success, false on failure
    // Can be nullptr if no CPU work needed
    std::function<bool()> cpuWork;

    // GPU work - runs on main thread after cpuWork completes
    // Returns true on success, false on failure
    // Can be nullptr if no GPU work needed
    std::function<bool()> gpuWork;

    // Progress weight (relative to other tasks, default 1.0)
    float weight = 1.0f;
};

/**
 * Progress information for loading screen
 */
struct SystemLoadProgress {
    std::string currentPhase;
    uint32_t completedTasks = 0;
    uint32_t totalTasks = 0;
    float progress = 0.0f;          // 0.0 to 1.0
    bool hasError = false;
    std::string errorMessage;
};

/**
 * AsyncSystemLoader - Orchestrates async system initialization during startup
 *
 * Design:
 * - Tasks declare dependencies on other tasks
 * - CPU work runs on background threads when dependencies are satisfied
 * - GPU work runs on main thread after CPU work completes
 * - Main thread polls for completions and can render loading screen between polls
 *
 * Usage:
 *   AsyncSystemLoader loader;
 *   loader.init(vulkanContext, loadingRenderer);
 *
 *   // Add tasks with dependencies
 *   loader.addTask({.id = "terrain", .cpuWork = [...], .gpuWork = [...]});
 *   loader.addTask({.id = "scene", .dependencies = {"terrain"}, ...});
 *
 *   // Start async loading
 *   loader.start();
 *
 *   // Poll loop (allows rendering loading screen)
 *   while (!loader.isComplete()) {
 *       loader.pollCompletions();  // Process completed CPU work, run GPU work
 *       loadingRenderer->render();
 *       SDL_PumpEvents();
 *   }
 */
class AsyncSystemLoader {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit AsyncSystemLoader(ConstructToken) {}

    struct InitInfo {
        VulkanContext* vulkanContext = nullptr;
        LoadingRenderer* loadingRenderer = nullptr;  // Optional, for progress display
        uint32_t workerCount = 0;  // 0 = auto (hardware concurrency - 1)
    };

    /**
     * Factory: Create and initialize the loader
     */
    static std::unique_ptr<AsyncSystemLoader> create(const InitInfo& info);

    ~AsyncSystemLoader();

    // Non-copyable
    AsyncSystemLoader(const AsyncSystemLoader&) = delete;
    AsyncSystemLoader& operator=(const AsyncSystemLoader&) = delete;

    /**
     * Add a task to be loaded
     * Must be called before start()
     */
    void addTask(SystemInitTask task);

    /**
     * Start async loading
     * Begins executing tasks whose dependencies are satisfied
     */
    void start();

    /**
     * Poll for completed CPU work and execute GPU work
     * Must be called from main thread
     * Returns number of tasks that completed GPU work this call
     */
    uint32_t pollCompletions();

    /**
     * Check if all tasks are complete (both CPU and GPU work)
     */
    bool isComplete() const;

    /**
     * Check if any errors occurred
     */
    bool hasError() const;

    /**
     * Get error message if hasError() is true
     */
    std::string getErrorMessage() const;

    /**
     * Get current progress
     */
    SystemLoadProgress getProgress() const;

    /**
     * Run the loading loop - blocks until all tasks complete
     * Renders loading screen frames between processing
     */
    void runLoadingLoop();

    /**
     * Shutdown and release resources
     */
    void shutdown();

private:
    bool init(const InitInfo& info);
    void workerLoop();
    void scheduleReadyTasks();
    bool areDependenciesSatisfied(const std::string& taskId) const;

    // Task storage
    std::unordered_map<std::string, SystemInitTask> tasks_;
    std::vector<std::string> taskOrder_;  // Submission order for determinism

    // Task state tracking
    std::unordered_set<std::string> pendingTasks_;      // Not yet started
    std::unordered_set<std::string> cpuRunningTasks_;   // CPU work in progress
    std::unordered_set<std::string> cpuCompleteTasks_;  // CPU done, awaiting GPU work
    std::unordered_set<std::string> completeTasks_;     // Fully complete

    // Worker threads
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    // Work queue for CPU tasks
    mutable std::mutex queueMutex_;
    std::queue<std::string> cpuWorkQueue_;  // Task IDs ready for CPU work
    std::condition_variable queueCondition_;

    // Completed CPU tasks ready for GPU work (main thread consumption)
    mutable std::mutex completedMutex_;
    std::queue<std::string> cpuCompletedQueue_;

    // Progress tracking
    std::atomic<float> totalWeight_{0.0f};
    std::atomic<float> completedWeight_{0.0f};
    mutable std::mutex progressMutex_;
    std::string currentPhase_;

    // Error handling
    std::atomic<bool> hasError_{false};
    mutable std::mutex errorMutex_;
    std::string errorMessage_;

    // Loading renderer (optional)
    LoadingRenderer* loadingRenderer_ = nullptr;
};

} // namespace Loading
