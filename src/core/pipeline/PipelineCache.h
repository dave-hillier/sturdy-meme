#pragma once

#include <vulkan/vulkan.hpp>
#include <string>

/**
 * PipelineCache - Manages Vulkan pipeline cache with disk persistence
 *
 * Pipeline caches significantly reduce shader compilation time on subsequent
 * runs by storing driver-specific compiled pipeline data.
 *
 * Usage:
 *   PipelineCache cache;
 *   cache.init(device, "pipeline_cache.bin");
 *   // Use cache.getCache() when creating pipelines
 *   cache.shutdown(); // Saves cache to disk
 */
class PipelineCache {
public:
    PipelineCache() = default;
    ~PipelineCache() = default;

    // Non-copyable
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    /**
     * Initialize the pipeline cache
     * @param device The Vulkan device
     * @param cacheFilePath Path to the cache file (loaded if exists)
     * @return true on success
     */
    bool init(VkDevice device, const std::string& cacheFilePath = "pipeline_cache.bin");

    /**
     * Shutdown and save cache to disk
     */
    void shutdown();

    /**
     * Get the pipeline cache handle for use in pipeline creation
     */
    VkPipelineCache getCache() const { return pipelineCache; }

    /**
     * Save the current cache state to disk
     * Can be called periodically to avoid losing cache on crash
     * @return true on success
     */
    bool saveToFile();

private:
    bool loadFromFile();

    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    std::string cacheFilePath;
};
