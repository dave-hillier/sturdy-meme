#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <string>
#include <optional>

/**
 * PipelineCache - Manages Vulkan pipeline cache with disk persistence
 *
 * Pipeline caches significantly reduce shader compilation time on subsequent
 * runs by storing driver-specific compiled pipeline data.
 *
 * Usage:
 *   PipelineCache cache;
 *   cache.init(raiiDevice, "pipeline_cache.bin");
 *   // Use cache.getCache() when creating pipelines
 *   cache.shutdown(); // Saves cache to disk
 */
class PipelineCache {
public:
    PipelineCache() = default;
    ~PipelineCache();

    // Non-copyable
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    /**
     * Initialize the pipeline cache
     * @param raiiDevice The Vulkan RAII device
     * @param cacheFilePath Path to the cache file (loaded if exists)
     * @return true on success
     */
    bool init(const vk::raii::Device& raiiDevice, const std::string& cacheFilePath = "pipeline_cache.bin");

    /**
     * Shutdown and save cache to disk
     */
    void shutdown();

    /**
     * Get the pipeline cache handle for use in pipeline creation
     */
    VkPipelineCache getCache() const { return pipelineCache_ ? **pipelineCache_ : VK_NULL_HANDLE; }

    /**
     * Save the current cache state to disk
     * Can be called periodically to avoid losing cache on crash
     * @return true on success
     */
    bool saveToFile();

private:
    bool loadFromFile();

    const vk::raii::Device* device_ = nullptr;
    std::optional<vk::raii::PipelineCache> pipelineCache_;
    std::string cacheFilePath_;
};
