#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include "Flamegraph.h"

/**
 * CPU Profiler for measuring CPU-side frame time breakdown.
 *
 * Uses high-resolution clock to measure time spent in various CPU operations
 * like culling, uniform updates, command buffer recording, etc.
 *
 * Zones prefixed with "Wait:" are tracked separately as GPU sync points
 * (time where CPU is idle waiting for GPU). This helps diagnose performance
 * bottlenecks and identify CPU vs GPU bound scenarios.
 *
 * Usage:
 *   profiler.beginFrame();
 *   {
 *       CpuProfiler::ScopedZone zone(profiler, "UniformUpdate");
 *       // ... update uniforms ...
 *   }
 *   {
 *       CpuProfiler::ScopedZone waitZone(profiler, "Wait:FenceWait");
 *       // ... wait for GPU fence ...
 *   }
 *   profiler.endFrame();
 */
class CpuProfiler {
public:
    struct TimingResult {
        std::string name;
        float cpuTimeMs;       // CPU time in milliseconds
        float percentOfFrame;  // Percentage of total frame CPU time
        bool isWaitZone;       // True if this zone represents waiting for GPU
    };

    struct FrameStats {
        float totalCpuTimeMs;
        float workTimeMs;      // Time doing actual CPU work (excludes wait zones)
        float waitTimeMs;      // Time waiting for GPU/sync operations
        float overheadTimeMs;  // Unaccounted time (profiling overhead, untracked work)
        std::vector<TimingResult> zones;
    };

    /**
     * RAII helper for scoped CPU timing zones.
     */
    class ScopedZone {
    public:
        ScopedZone(CpuProfiler& profiler, const char* zoneName)
            : profiler(profiler), name(zoneName) {
            profiler.beginZone(name);
        }

        ~ScopedZone() {
            profiler.endZone(name);
        }

        // Non-copyable, non-movable
        ScopedZone(const ScopedZone&) = delete;
        ScopedZone& operator=(const ScopedZone&) = delete;
        ScopedZone(ScopedZone&&) = delete;
        ScopedZone& operator=(ScopedZone&&) = delete;

    private:
        CpuProfiler& profiler;
        const char* name;
    };

    CpuProfiler() = default;
    ~CpuProfiler() = default;

    /**
     * Call at the start of CPU-side frame processing.
     */
    void beginFrame();

    /**
     * Call at the end of CPU-side frame processing.
     */
    void endFrame();

    /**
     * Begin a named profiling zone.
     */
    void beginZone(const char* zoneName);

    /**
     * End a named profiling zone.
     */
    void endZone(const char* zoneName);

    /**
     * Get profiling results from the last completed frame.
     */
    const FrameStats& getResults() const { return lastFrameStats; }

    /**
     * Check if profiling is enabled.
     */
    bool isEnabled() const { return enabled; }
    void setEnabled(bool e) { enabled = e; }

    /**
     * Get the list of zone names (for GUI display).
     */
    const std::vector<std::string>& getZoneNames() const { return zoneNames; }

    /**
     * Get smoothed frame stats (averaged over multiple frames).
     */
    const FrameStats& getSmoothedResults() const { return smoothedStats; }

    /**
     * Get the flamegraph capture from the last completed frame.
     * Returns empty capture if flamegraph wasn't enabled during the frame.
     */
    const FlamegraphCapture& getFlamegraphCapture() const { return lastFlamegraph; }

    /**
     * Enable/disable flamegraph capture (separate from profiling).
     */
    void setFlamegraphEnabled(bool e) { flamegraphEnabled = e; }
    bool isFlamegraphEnabled() const { return flamegraphEnabled; }

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    struct ZoneData {
        TimePoint startTime;
        float accumulatedMs = 0.0f;
    };

    bool enabled = true;

    // Current frame state
    TimePoint frameStartTime;
    std::unordered_map<std::string, ZoneData> activeZones;
    std::vector<std::string> currentFrameZoneOrder;

    // Results
    FrameStats lastFrameStats;
    FrameStats smoothedStats;
    std::vector<std::string> zoneNames;

    // Smoothing factor (0.0 = no smoothing, 1.0 = infinite smoothing)
    static constexpr float SMOOTHING_FACTOR = 0.9f;

    // Flamegraph capture
    bool flamegraphEnabled = true;
    FlamegraphBuilder flamegraphBuilder;
    FlamegraphCapture lastFlamegraph;
    uint64_t frameNumber = 0;
};

// Macro for convenient scoped profiling
#define CPU_PROFILE_ZONE(profiler, name) \
    CpuProfiler::ScopedZone _cpuZone##__LINE__(profiler, name)
