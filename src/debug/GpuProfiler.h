#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <optional>
#include <atomic>
#include <cstdint>
#include <memory>

/**
 * GPU Profiler using Vulkan timestamp queries.
 *
 * Measures GPU execution time for individual render passes and compute dispatches.
 * Uses double-buffered query pools to avoid pipeline stalls.
 *
 * Usage:
 *   auto profiler = GpuProfiler::create(device, physicalDevice, framesInFlight);
 *   if (!profiler) { handle error; }
 *   profiler->beginFrame(cmd, frameIndex);
 *   profiler->beginZone(cmd, "ShadowPass");
 *   // ... shadow pass commands ...
 *   profiler->endZone(cmd, "ShadowPass");
 *   profiler->endFrame(cmd, frameIndex);
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

    /**
     * Factory: Create a GPU profiler.
     * Returns nullopt if initialization fails (fatal error).
     * Note: If timestamps are unsupported, returns a valid but disabled profiler.
     */
    static std::optional<GpuProfiler> create(VkDevice device, VkPhysicalDevice physicalDevice,
                                              uint32_t framesInFlight, uint32_t maxZones = 64);

    // Destructor handles cleanup
    ~GpuProfiler();

    // Move-only (owns Vulkan resources)
    GpuProfiler(GpuProfiler&& other) noexcept;
    GpuProfiler& operator=(GpuProfiler&& other) noexcept;
    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

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
     * Get smoothed profiling results (averaged over multiple frames).
     * More stable for display, handles zones that appear/disappear.
     */
    const FrameStats& getSmoothedResults() const { return smoothedStats; }

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
    GpuProfiler();  // Private: use factory

    bool initInternal(VkDevice device, VkPhysicalDevice physicalDevice,
                      uint32_t framesInFlight, uint32_t maxZones);
    void cleanup();

    static constexpr uint32_t QUERIES_PER_ZONE = 2;  // Start + end timestamp

    // Lock-free zone recording slot
    struct ZoneSlot {
        std::atomic<uint32_t> startQueryIndex{UINT32_MAX};  // UINT32_MAX = unused
        std::atomic<uint32_t> endQueryIndex{UINT32_MAX};
        const char* name = nullptr;  // Set atomically with startQueryIndex
    };

    VkDevice device = VK_NULL_HANDLE;
    std::vector<VkQueryPool> queryPools;  // One per frame in flight

    float timestampPeriod = 0.0f;  // Nanoseconds per timestamp tick
    uint32_t maxZones = 0;
    uint32_t framesInFlight = 0;
    bool enabled = true;

    // Current frame state - lock-free zone tracking
    std::atomic<uint32_t> currentQueryIndex{0};
    std::atomic<uint32_t> currentZoneSlot{0};  // Next available slot in current frame's zoneSlots
    uint32_t currentFrameIndex = 0;

    // Per-frame zone slot storage (one array per frame in flight)
    std::vector<std::unique_ptr<ZoneSlot[]>> zoneSlots_;  // [frameIndex][slotIndex]

    // Per-frame data for result collection (indexed by frameIndex, sized to framesInFlight)
    std::vector<uint32_t> frameQueryCounts;
    std::vector<uint32_t> frameZoneCounts;  // Number of zones recorded per frame

    // Results from previous frame
    FrameStats lastFrameStats;
    FrameStats smoothedStats;
    std::unordered_map<std::string, float> smoothedZoneTimes;  // Per-zone smoothed times
    std::vector<std::string> zoneNames;

    // Smoothing factor (0.0 = no smoothing, 1.0 = infinite smoothing)
    static constexpr float SMOOTHING_FACTOR = 0.9f;
    float smoothedFrameTimeMs = 0.0f;

    // Frame start/end query indices
    uint32_t frameStartQuery = 0;
    uint32_t frameEndQuery = 0;

    void collectResults(uint32_t frameIndex);
};
