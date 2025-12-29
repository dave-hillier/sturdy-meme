#pragma once

#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>

/**
 * Data structures for flamegraph visualization of profiling data.
 *
 * Supports:
 * - Init profiler: Single capture with hierarchical phases
 * - CPU/GPU profiler: Ring buffer of 10 captures for historical viewing
 */

struct FlamegraphEntry {
    std::string name;
    float timeMs;
    float startOffsetMs;  // Offset from start of frame/init
    int depth;            // Stack depth (0 = root level)
    bool isWaitZone;      // For CPU profiler: indicates wait zone

    // Color hint based on zone type (for rendering)
    enum class ColorHint {
        Default,
        Wait,       // CPU wait zones (cyan)
        Shadow,     // Shadow-related passes
        Water,      // Water-related passes
        Terrain,    // Terrain-related passes
        PostProcess // Post-processing passes
    };
    ColorHint colorHint = ColorHint::Default;
};

struct FlamegraphCapture {
    float totalTimeMs = 0.0f;
    std::vector<FlamegraphEntry> entries;
    uint64_t frameNumber = 0;  // Frame when captured (for CPU/GPU)

    bool isEmpty() const { return entries.empty(); }

    /**
     * Build from CPU profiler results (flat zones -> depth 0)
     */
    static FlamegraphCapture fromCpuStats(
        float totalCpuTimeMs,
        const std::vector<std::tuple<std::string, float, float, bool>>& zones,
        uint64_t frameNum)
    {
        FlamegraphCapture capture;
        capture.totalTimeMs = totalCpuTimeMs;
        capture.frameNumber = frameNum;

        float offset = 0.0f;
        for (const auto& [name, timeMs, pct, isWait] : zones) {
            FlamegraphEntry entry;
            entry.name = name;
            entry.timeMs = timeMs;
            entry.startOffsetMs = offset;
            entry.depth = 0;
            entry.isWaitZone = isWait;
            entry.colorHint = isWait ? FlamegraphEntry::ColorHint::Wait : FlamegraphEntry::ColorHint::Default;

            capture.entries.push_back(entry);
            offset += timeMs;
        }

        return capture;
    }

    /**
     * Build from GPU profiler results (flat zones -> depth 0)
     */
    static FlamegraphCapture fromGpuStats(
        float totalGpuTimeMs,
        const std::vector<std::tuple<std::string, float, float>>& zones,
        uint64_t frameNum)
    {
        FlamegraphCapture capture;
        capture.totalTimeMs = totalGpuTimeMs;
        capture.frameNumber = frameNum;

        float offset = 0.0f;
        for (const auto& [name, timeMs, pct] : zones) {
            FlamegraphEntry entry;
            entry.name = name;
            entry.timeMs = timeMs;
            entry.startOffsetMs = offset;
            entry.depth = 0;
            entry.isWaitZone = false;

            // Assign color hints based on name prefix
            if (name.find("Shadow") != std::string::npos) {
                entry.colorHint = FlamegraphEntry::ColorHint::Shadow;
            } else if (name.find("Water") != std::string::npos) {
                entry.colorHint = FlamegraphEntry::ColorHint::Water;
            } else if (name.find("Terrain") != std::string::npos) {
                entry.colorHint = FlamegraphEntry::ColorHint::Terrain;
            } else if (name.find("Post") != std::string::npos ||
                       name.find("Bloom") != std::string::npos ||
                       name.find("Tone") != std::string::npos) {
                entry.colorHint = FlamegraphEntry::ColorHint::PostProcess;
            }

            capture.entries.push_back(entry);
            offset += timeMs;
        }

        return capture;
    }

    /**
     * Build from init profiler results (already has depth)
     */
    static FlamegraphCapture fromInitResults(
        float totalTimeMs,
        const std::vector<std::tuple<std::string, float, float, int>>& phases)
    {
        FlamegraphCapture capture;
        capture.totalTimeMs = totalTimeMs;
        capture.frameNumber = 0;

        // For init, we need to calculate proper offsets per depth level
        // Track running offset at each depth level
        std::vector<float> offsetAtDepth(16, 0.0f);  // Up to 16 nesting levels

        for (const auto& [name, timeMs, pct, depth] : phases) {
            FlamegraphEntry entry;
            entry.name = name;
            entry.timeMs = timeMs;
            entry.depth = depth;
            entry.isWaitZone = false;
            entry.colorHint = FlamegraphEntry::ColorHint::Default;

            // Start offset is the current offset at this depth
            entry.startOffsetMs = offsetAtDepth[depth];

            // Advance offset at this depth
            offsetAtDepth[depth] += timeMs;

            // Reset deeper levels when we go back up
            for (size_t d = depth + 1; d < offsetAtDepth.size(); ++d) {
                offsetAtDepth[d] = offsetAtDepth[depth];
            }

            capture.entries.push_back(entry);
        }

        return capture;
    }
};

/**
 * Ring buffer for storing flamegraph capture history.
 */
template<size_t N>
class FlamegraphHistory {
public:
    void push(FlamegraphCapture capture) {
        buffer_[writeIndex_] = std::move(capture);
        writeIndex_ = (writeIndex_ + 1) % N;
        count_ = std::min(count_ + 1, N);
    }

    /**
     * Get capture by index (0 = most recent, 1 = second most recent, etc.)
     */
    const FlamegraphCapture* get(size_t index) const {
        if (index >= count_) return nullptr;
        size_t actualIndex = (writeIndex_ + N - 1 - index) % N;
        return &buffer_[actualIndex];
    }

    /**
     * Get the most recent capture.
     */
    const FlamegraphCapture* latest() const {
        return get(0);
    }

    size_t count() const { return count_; }
    size_t capacity() const { return N; }

    void clear() {
        count_ = 0;
        writeIndex_ = 0;
    }

private:
    std::array<FlamegraphCapture, N> buffer_;
    size_t writeIndex_ = 0;
    size_t count_ = 0;
};

// Type aliases for the profiler flamegraph histories
using CpuFlamegraphHistory = FlamegraphHistory<10>;
using GpuFlamegraphHistory = FlamegraphHistory<10>;
