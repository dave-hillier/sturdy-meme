#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>

/**
 * GPU Profiler using Vulkan timestamp queries.
 *
 * Measures GPU execution time for individual render passes and compute dispatches.
 * Uses double-buffered query pools to avoid pipeline stalls.
 *
 * Usage:
 *   profiler.beginFrame(cmd, frameIndex);
 *   profiler.beginZone(cmd, "ShadowPass");
 *   // ... shadow pass commands ...
 *   profiler.endZone(cmd, "ShadowPass");
 *   profiler.endFrame(cmd, frameIndex);
 *   // Results available next frame via getResults()
 */
class GpuProfiler {
public:
    struct TimingResult {
        std::string name;
        float gpuTimeMs;       // GPU time in milliseconds
        float percentOfFrame;  // Percentage of total frame GPU time
    };

    struct FrameStats {
        float totalGpuTimeMs;
        std::vector<TimingResult> zones;
    };

    GpuProfiler() = default;
    ~GpuProfiler() = default;

    // Non-copyable
    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    /**
     * Initialize the profiler with Vulkan handles.
     * @param device Vulkan logical device
     * @param physicalDevice For querying timestamp period
     * @param framesInFlight Number of frames in flight (for query pool double-buffering)
     * @param maxZones Maximum number of profiling zones per frame
     */
    bool init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t framesInFlight, uint32_t maxZones = 32);
    void shutdown();

    /**
     * Call at the start of frame command buffer recording.
     * Resets query pool for this frame and writes initial timestamp.
     */
    void beginFrame(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * Call at the end of frame command buffer recording.
     * Writes final timestamp and triggers result collection from previous frame.
     */
    void endFrame(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * Begin a named profiling zone.
     * Writes a timestamp at the top of the GPU pipeline.
     */
    void beginZone(VkCommandBuffer cmd, const char* zoneName);

    /**
     * End a named profiling zone.
     * Writes a timestamp at the bottom of the GPU pipeline.
     */
    void endZone(VkCommandBuffer cmd, const char* zoneName);

    /**
     * Get profiling results from the previous frame.
     * Results are only valid after at least 2 frames have been rendered.
     */
    const FrameStats& getResults() const { return lastFrameStats; }

    /**
     * Check if profiling is enabled.
     */
    bool isEnabled() const { return enabled; }
    void setEnabled(bool e) { enabled = e; }

    /**
     * Get the list of available zone names (for GUI display).
     */
    const std::vector<std::string>& getZoneNames() const { return zoneNames; }

private:
    static constexpr uint32_t QUERIES_PER_ZONE = 2;  // Start + end timestamp

    struct ZoneInfo {
        uint32_t startQueryIndex;
        uint32_t endQueryIndex;
    };

    VkDevice device = VK_NULL_HANDLE;
    std::vector<VkQueryPool> queryPools;  // One per frame in flight

    float timestampPeriod = 0.0f;  // Nanoseconds per timestamp tick
    uint32_t maxZones = 0;
    uint32_t framesInFlight = 0;
    bool enabled = true;
    bool initialized = false;

    // Current frame state
    uint32_t currentQueryIndex = 0;
    uint32_t currentFrameIndex = 0;
    std::unordered_map<std::string, ZoneInfo> activeZones;
    std::vector<std::string> currentFrameZoneOrder;

    // Per-frame data for result collection
    std::unordered_map<uint32_t, uint32_t> frameQueryCounts;
    std::unordered_map<uint32_t, std::vector<std::string>> frameZoneOrders;

    // Results from previous frame
    FrameStats lastFrameStats;
    std::vector<std::string> zoneNames;

    // Frame start/end query indices
    uint32_t frameStartQuery = 0;
    uint32_t frameEndQuery = 0;

    void collectResults(uint32_t frameIndex);
};
