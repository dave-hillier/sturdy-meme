#pragma once

#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>
#include <stack>

/**
 * Data structures for flamegraph visualization of profiling data.
 *
 * A flamegraph shows hierarchical timing data where:
 * - Parent zones are at the bottom, children stacked on top
 * - Each bar's width is proportional to its duration
 * - Children are positioned within the horizontal span of their parent
 */

// Color hint based on zone type (for rendering)
enum class FlamegraphColorHint {
    Default,
    Wait,       // CPU wait zones (cyan)
    Shadow,     // Shadow-related passes
    Water,      // Water-related passes
    Terrain,    // Terrain-related passes
    PostProcess // Post-processing passes
};

/**
 * A node in the flamegraph tree representing a profiled zone.
 */
struct FlamegraphNode {
    std::string name;
    float startMs = 0.0f;      // Start time relative to frame/init start
    float durationMs = 0.0f;   // Duration of this zone
    FlamegraphColorHint colorHint = FlamegraphColorHint::Default;
    bool isWaitZone = false;
    std::vector<FlamegraphNode> children;

    /**
     * Get the end time of this node.
     */
    float endMs() const { return startMs + durationMs; }

    /**
     * Calculate the maximum depth of this subtree.
     */
    int maxDepth() const {
        int max = 0;
        for (const auto& child : children) {
            max = std::max(max, child.maxDepth() + 1);
        }
        return max;
    }
};

/**
 * A complete flamegraph capture for one frame.
 */
struct FlamegraphCapture {
    float totalTimeMs = 0.0f;
    uint64_t frameNumber = 0;
    std::vector<FlamegraphNode> roots;  // Top-level zones

    bool isEmpty() const { return roots.empty(); }

    /**
     * Get the maximum depth of the flamegraph.
     */
    int maxDepth() const {
        int max = 0;
        for (const auto& root : roots) {
            max = std::max(max, root.maxDepth() + 1);
        }
        return max;
    }
};

/**
 * Helper class to build a flamegraph capture from profiling events.
 * Tracks zone hierarchy during a frame and produces a FlamegraphCapture.
 */
class FlamegraphBuilder {
public:
    void beginFrame() {
        capture_ = FlamegraphCapture{};
        while (!activeStack_.empty()) activeStack_.pop();
        frameStarted_ = true;
    }

    void beginZone(const char* name, float timestampMs, bool isWaitZone = false) {
        if (!frameStarted_) return;

        FlamegraphNode node;
        node.name = name;
        node.startMs = timestampMs;
        node.isWaitZone = isWaitZone;
        node.colorHint = getColorHint(name, isWaitZone);

        activeStack_.push(std::move(node));
    }

    void endZone(const char* name, float timestampMs) {
        if (!frameStarted_ || activeStack_.empty()) return;

        FlamegraphNode node = std::move(activeStack_.top());
        activeStack_.pop();

        // Verify name matches (should match in well-formed profiling)
        if (node.name != name) {
            // Mismatched zone - try to recover by keeping the name we started with
        }

        node.durationMs = timestampMs - node.startMs;

        if (activeStack_.empty()) {
            // This is a root node
            capture_.roots.push_back(std::move(node));
        } else {
            // This is a child of the current top
            activeStack_.top().children.push_back(std::move(node));
        }
    }

    FlamegraphCapture endFrame(float totalTimeMs, uint64_t frameNumber) {
        capture_.totalTimeMs = totalTimeMs;
        capture_.frameNumber = frameNumber;
        frameStarted_ = false;

        // Move out the capture
        FlamegraphCapture result = std::move(capture_);
        capture_ = FlamegraphCapture{};
        return result;
    }

    bool isActive() const { return frameStarted_; }

private:
    static FlamegraphColorHint getColorHint(const char* name, bool isWaitZone) {
        if (isWaitZone) return FlamegraphColorHint::Wait;

        std::string n(name);
        if (n.find("Shadow") != std::string::npos) return FlamegraphColorHint::Shadow;
        if (n.find("Water") != std::string::npos) return FlamegraphColorHint::Water;
        if (n.find("Terrain") != std::string::npos) return FlamegraphColorHint::Terrain;
        if (n.find("Post") != std::string::npos ||
            n.find("Bloom") != std::string::npos ||
            n.find("Tone") != std::string::npos) return FlamegraphColorHint::PostProcess;

        return FlamegraphColorHint::Default;
    }

    FlamegraphCapture capture_;
    std::stack<FlamegraphNode> activeStack_;
    bool frameStarted_ = false;
};

/**
 * Build a FlamegraphCapture from init profiler results.
 * Init profiler already tracks depth, so we can reconstruct the hierarchy.
 */
inline FlamegraphCapture buildInitFlamegraph(
    float totalTimeMs,
    const std::vector<std::tuple<std::string, float, float, int>>& phases)
{
    FlamegraphCapture capture;
    capture.totalTimeMs = totalTimeMs;
    capture.frameNumber = 0;

    // Use a stack to track parent nodes at each depth
    std::vector<FlamegraphNode*> parentStack;

    // Track end times at each depth to calculate start offsets
    std::vector<float> endTimeAtDepth(16, 0.0f);

    for (const auto& [name, timeMs, pct, depth] : phases) {
        FlamegraphNode node;
        node.name = name;
        node.durationMs = timeMs;
        node.isWaitZone = false;
        node.colorHint = FlamegraphColorHint::Default;

        // Calculate start time based on when previous sibling at this depth ended
        node.startMs = endTimeAtDepth[depth];
        endTimeAtDepth[depth] = node.startMs + timeMs;

        // Reset deeper levels (children of this node start at 0 relative to this node's start)
        for (size_t d = depth + 1; d < endTimeAtDepth.size(); ++d) {
            endTimeAtDepth[d] = node.startMs;
        }

        if (depth == 0) {
            // Root node
            capture.roots.push_back(std::move(node));
            parentStack.resize(1);
            parentStack[0] = &capture.roots.back();
        } else {
            // Child node - parent is at depth-1
            if (static_cast<size_t>(depth) <= parentStack.size() && parentStack[depth - 1]) {
                parentStack[depth - 1]->children.push_back(std::move(node));
                parentStack.resize(depth + 1);
                parentStack[depth] = &parentStack[depth - 1]->children.back();
            }
        }
    }

    return capture;
}

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
