#pragma once

#include "LoadJobQueue.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <functional>
#include <string>

class VulkanContext;
class LoadingRenderer;

namespace Loading {

/**
 * AsyncStartupLoader - Orchestrates async loading during startup
 *
 * This class coordinates between:
 * - Background worker threads (load data from disk to CPU staging)
 * - Main thread (GPU uploads + loading screen rendering)
 *
 * Usage:
 *   AsyncStartupLoader loader;
 *   loader.init(vulkanContext, loadingRenderer);
 *   loader.queueTextureLoad("diffuse", "textures/diffuse.png");
 *   loader.queueMeshLoad("character", "models/character.gltf");
 *   loader.runLoadingLoop();  // Blocks until all complete, renders loading screen
 *   // Now retrieve staged resources and upload to GPU
 */
class AsyncStartupLoader {
public:
    struct InitInfo {
        VulkanContext* vulkanContext = nullptr;
        LoadingRenderer* loadingRenderer = nullptr;  // Optional, for progress display
        std::string resourcePath;
        uint32_t workerCount = 2;
    };

    /**
     * Factory: Create and initialize the loader
     */
    static std::unique_ptr<AsyncStartupLoader> create(const InitInfo& info);

    ~AsyncStartupLoader();

    // Non-copyable
    AsyncStartupLoader(const AsyncStartupLoader&) = delete;
    AsyncStartupLoader& operator=(const AsyncStartupLoader&) = delete;

    /**
     * Queue a texture to be loaded from disk
     * @param id Unique identifier for this resource
     * @param path Path relative to resourcePath
     * @param srgb Whether to treat as sRGB
     * @param priority Lower = higher priority
     */
    void queueTextureLoad(const std::string& id, const std::string& path,
                          bool srgb = true, int priority = 0);

    /**
     * Queue a heightmap to be loaded
     */
    void queueHeightmapLoad(const std::string& id, const std::string& path,
                            int priority = 0);

    /**
     * Queue a generic file load (raw bytes)
     */
    void queueFileLoad(const std::string& id, const std::string& path,
                       const std::string& phase = "Data", int priority = 0);

    /**
     * Queue a custom job with user-provided execution function
     */
    void queueCustomJob(const std::string& id, const std::string& phase,
                        std::function<std::unique_ptr<StagedResource>()> execute,
                        int priority = 0);

    /**
     * Set a callback to be invoked when a job completes (on main thread)
     * Use this to perform GPU uploads immediately when data is ready
     */
    using JobCompleteCallback = std::function<void(LoadJobResult&)>;
    void setJobCompleteCallback(JobCompleteCallback callback);

    /**
     * Run the loading loop - blocks until all jobs complete
     * Renders loading screen frames between processing completed jobs
     */
    void runLoadingLoop();

    /**
     * Process any completed jobs without blocking
     * Returns number of jobs processed
     */
    uint32_t processCompletedJobs();

    /**
     * Check if loading is complete
     */
    bool isComplete() const;

    /**
     * Get current progress
     */
    LoadProgress getProgress() const;

    /**
     * Get all completed results (for deferred processing)
     */
    std::vector<LoadJobResult> getAllResults();

    /**
     * Shutdown and release resources
     */
    void shutdown();

private:
    AsyncStartupLoader() = default;
    bool init(const InitInfo& info);

    // Helper to build full path
    std::string buildPath(const std::string& relativePath) const;

    VulkanContext* vulkanContext_ = nullptr;
    LoadingRenderer* loadingRenderer_ = nullptr;
    std::string resourcePath_;

    std::unique_ptr<LoadJobQueue> jobQueue_;
    JobCompleteCallback jobCompleteCallback_;

    uint32_t queuedJobCount_ = 0;

    // Collected results for deferred processing
    std::vector<LoadJobResult> collectedResults_;
};

} // namespace Loading
