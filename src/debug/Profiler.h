#pragma once

#include "GpuProfiler.h"
#include "CpuProfiler.h"
#include "InitProfiler.h"
#include "Flamegraph.h"
#include "interfaces/IProfilerControl.h"
#include <memory>
#include <optional>

/**
 * Unified Profiler combining GPU timestamp queries and CPU timing.
 *
 * Provides a single interface for frame profiling with both GPU and CPU breakdown.
 * Results are accessible for GUI display.
 *
 * Usage:
 *   auto profiler = Profiler::create(device, physicalDevice, framesInFlight);
 *   // GPU may be disabled if init fails, but CPU profiling always works
 */
class Profiler : public IProfilerControl {
public:
    /**
     * Factory: Create a profiler instance.
     * Always returns valid profiler - GPU may be disabled if init fails,
     * but CPU profiling will still work.
     */
    static std::unique_ptr<Profiler> create(VkDevice device, VkPhysicalDevice physicalDevice,
                                             uint32_t framesInFlight) {
        auto profiler = std::unique_ptr<Profiler>(new Profiler());
        auto gpu = GpuProfiler::create(device, physicalDevice, framesInFlight);
        if (gpu) {
            profiler->gpuProfiler_ = std::move(*gpu);
        }
        // CPU profiling always works, so we return valid profiler even if GPU fails
        return profiler;
    }

    ~Profiler() {
        gpuProfiler_.reset();
    }

    // Move-only (owns GPU resources)
    Profiler(Profiler&& other) noexcept = default;
    Profiler& operator=(Profiler&& other) noexcept = default;
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    /**
     * Begin CPU frame profiling (call at very start of frame, before any CPU zones).
     */
    void beginCpuFrame() {
        cpuProfiler.beginFrame();
    }

    /**
     * Begin GPU frame profiling (call when command buffer is ready).
     */
    void beginGpuFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
        if (gpuProfiler_) {
            gpuProfiler_->beginFrame(cmd, frameIndex);
        }
    }

    /**
     * End CPU frame profiling (call at end of frame, after present).
     */
    void endCpuFrame() {
        cpuProfiler.endFrame();
    }

    /**
     * End GPU frame profiling (call after command buffer recording, before submit).
     */
    void endGpuFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
        if (gpuProfiler_) {
            gpuProfiler_->endFrame(cmd, frameIndex);
        }
    }

    /**
     * Legacy combined begin (for backwards compatibility).
     * Prefer using beginCpuFrame() + beginGpuFrame() separately.
     */
    void beginFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
        cpuProfiler.beginFrame();
        if (gpuProfiler_) {
            gpuProfiler_->beginFrame(cmd, frameIndex);
        }
    }

    /**
     * Legacy combined end (for backwards compatibility).
     */
    void endFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
        if (gpuProfiler_) {
            gpuProfiler_->endFrame(cmd, frameIndex);
        }
        cpuProfiler.endFrame();
    }

    /**
     * Begin a GPU profiling zone.
     */
    void beginGpuZone(VkCommandBuffer cmd, const char* zoneName) {
        if (gpuProfiler_) {
            gpuProfiler_->beginZone(cmd, zoneName);
        }
    }

    /**
     * End a GPU profiling zone.
     */
    void endGpuZone(VkCommandBuffer cmd, const char* zoneName) {
        if (gpuProfiler_) {
            gpuProfiler_->endZone(cmd, zoneName);
        }
    }

    /**
     * Begin a CPU profiling zone.
     */
    void beginCpuZone(const char* zoneName) {
        cpuProfiler.beginZone(zoneName);
    }

    /**
     * End a CPU profiling zone.
     */
    void endCpuZone(const char* zoneName) {
        cpuProfiler.endZone(zoneName);
    }

    /**
     * RAII helper for scoped CPU zones.
     */
    CpuProfiler::ScopedZone scopedCpuZone(const char* zoneName) {
        return CpuProfiler::ScopedZone(cpuProfiler, zoneName);
    }

    // Results access
    const GpuProfiler::FrameStats& getGpuResults() const {
        static GpuProfiler::FrameStats empty;
        return gpuProfiler_ ? gpuProfiler_->getResults() : empty;
    }
    const GpuProfiler::FrameStats& getSmoothedGpuResults() const {
        static GpuProfiler::FrameStats empty;
        return gpuProfiler_ ? gpuProfiler_->getSmoothedResults() : empty;
    }
    const CpuProfiler::FrameStats& getCpuResults() const { return cpuProfiler.getResults(); }
    const CpuProfiler::FrameStats& getSmoothedCpuResults() const { return cpuProfiler.getSmoothedResults(); }

    // Enable/disable
    bool isGpuProfilingEnabled() const { return gpuProfiler_ && gpuProfiler_->isEnabled(); }
    bool isCpuProfilingEnabled() const { return cpuProfiler.isEnabled(); }
    void setGpuProfilingEnabled(bool e) { if (gpuProfiler_) gpuProfiler_->setEnabled(e); }
    void setCpuProfilingEnabled(bool e) { cpuProfiler.setEnabled(e); }

    void setEnabled(bool e) {
        if (gpuProfiler_) gpuProfiler_->setEnabled(e);
        cpuProfiler.setEnabled(e);
    }

    bool isEnabled() const {
        return (gpuProfiler_ && gpuProfiler_->isEnabled()) || cpuProfiler.isEnabled();
    }

    // Direct access to profilers
    GpuProfiler& getGpuProfiler() { return *gpuProfiler_; }
    CpuProfiler& getCpuProfiler() { return cpuProfiler; }
    const GpuProfiler& getGpuProfiler() const { return *gpuProfiler_; }
    const CpuProfiler& getCpuProfiler() const { return cpuProfiler; }

    // IProfilerControl implementation
    Profiler& getProfiler() override { return *this; }
    const Profiler& getProfiler() const override { return *this; }

    // Flamegraph capture
    /**
     * Capture current CPU timing to flamegraph history.
     * CPU profiler tracks hierarchy, so we get a proper tree structure.
     * Call after endCpuFrame() to capture the completed frame.
     */
    void captureCpuFlamegraph() {
        if (!capturePaused_) {
            cpuFlamegraphHistory_.push(cpuProfiler.getFlamegraphCapture());
        }
    }

    /**
     * Capture current GPU timing to flamegraph history.
     * Infers hierarchy from zone names using ':' as separator.
     * E.g., "HDR:Sky" becomes a child of "HDR" if present, else a root.
     * Call after endGpuFrame() results are available.
     */
    void captureGpuFlamegraph() {
        if (!gpuProfiler_ || capturePaused_) return;

        const auto& stats = gpuProfiler_->getResults();
        FlamegraphCapture capture;
        capture.totalTimeMs = stats.totalGpuTimeMs;
        capture.frameNumber = frameNumber_;

        // Helper to assign color hints
        auto assignColorHint = [](FlamegraphNode& node, const std::string& name) {
            if (name.find("Shadow") != std::string::npos) {
                node.colorHint = FlamegraphColorHint::Shadow;
            } else if (name.find("Water") != std::string::npos) {
                node.colorHint = FlamegraphColorHint::Water;
            } else if (name.find("Terrain") != std::string::npos) {
                node.colorHint = FlamegraphColorHint::Terrain;
            } else if (name.find("Post") != std::string::npos ||
                       name.find("Bloom") != std::string::npos ||
                       name.find("Tone") != std::string::npos ||
                       name.find("HDR") != std::string::npos) {
                node.colorHint = FlamegraphColorHint::PostProcess;
            } else if (name.find("Atmosphere") != std::string::npos ||
                       name.find("Froxel") != std::string::npos ||
                       name.find("Sky") != std::string::npos) {
                node.colorHint = FlamegraphColorHint::Atmosphere;
            }
        };

        // Build hierarchy from zone names
        // Parent zones (no ':') become roots, child zones (with ':') nest under matching parent
        std::unordered_map<std::string, FlamegraphNode*> parentNodes;
        std::unordered_map<std::string, float> parentOffsets;

        float offset = 0.0f;
        for (const auto& zone : stats.zones) {
            FlamegraphNode node;
            node.name = zone.name;
            node.durationMs = zone.gpuTimeMs;
            node.isWaitZone = false;
            assignColorHint(node, zone.name);

            // Check if this is a child zone (contains ':')
            size_t colonPos = zone.name.find(':');
            if (colonPos != std::string::npos) {
                std::string parentName = zone.name.substr(0, colonPos);

                // Find existing parent node
                auto it = parentNodes.find(parentName);
                if (it != parentNodes.end()) {
                    // Add as child of parent, offset relative to parent start
                    node.startMs = parentOffsets[parentName];
                    parentOffsets[parentName] += zone.gpuTimeMs;
                    it->second->children.push_back(std::move(node));
                    continue;  // Don't add to roots
                }
            }

            // This is a root zone (no ':' or no matching parent)
            node.startMs = offset;
            offset += zone.gpuTimeMs;

            // If this could be a parent, track it
            if (colonPos == std::string::npos) {
                capture.roots.push_back(std::move(node));
                parentNodes[zone.name] = &capture.roots.back();
                parentOffsets[zone.name] = 0.0f;
            } else {
                capture.roots.push_back(std::move(node));
            }
        }

        gpuFlamegraphHistory_.push(std::move(capture));
    }

    /**
     * Capture init profiler results to flamegraph (single capture).
     * Call after InitProfiler::finalize().
     */
    void captureInitFlamegraph() {
        const auto& results = InitProfiler::get().getResults();
        std::vector<std::tuple<std::string, float, float, int>> phases;
        for (const auto& phase : results.phases) {
            phases.emplace_back(phase.name, phase.timeMs, phase.percentOfTotal, phase.depth);
        }
        initFlamegraph_ = buildInitFlamegraph(results.totalTimeMs, phases);
    }

    /**
     * Increment frame counter and auto-capture if interval reached.
     * Call once per frame after profiling data is complete.
     */
    void advanceFrame() {
        frameNumber_++;
        framesSinceCapture_++;

        // Auto-capture at interval
        if (flamegraphEnabled_ && framesSinceCapture_ >= captureInterval_) {
            captureCpuFlamegraph();
            captureGpuFlamegraph();
            framesSinceCapture_ = 0;
        }
    }

    /**
     * Force an immediate flamegraph capture.
     */
    void captureNow() {
        captureCpuFlamegraph();
        captureGpuFlamegraph();
        framesSinceCapture_ = 0;
    }

    /**
     * Get current frame number.
     */
    uint64_t getFrameNumber() const { return frameNumber_; }

    /**
     * Set the capture interval (capture every N frames).
     */
    void setCaptureInterval(uint32_t interval) { captureInterval_ = std::max(1u, interval); }
    uint32_t getCaptureInterval() const { return captureInterval_; }

    /**
     * Enable/disable flamegraph capture.
     */
    void setFlamegraphEnabled(bool enabled) { flamegraphEnabled_ = enabled; }
    bool isFlamegraphEnabled() const { return flamegraphEnabled_; }

    /**
     * Pause/resume flamegraph capture (for inspection).
     */
    void setCapturePaused(bool paused) { capturePaused_ = paused; }
    bool isCapturePaused() const { return capturePaused_; }

    // Flamegraph history access
    const CpuFlamegraphHistory& getCpuFlamegraphHistory() const { return cpuFlamegraphHistory_; }
    const GpuFlamegraphHistory& getGpuFlamegraphHistory() const { return gpuFlamegraphHistory_; }
    const FlamegraphCapture& getInitFlamegraph() const { return initFlamegraph_; }

private:
    Profiler() = default;  // Private: use factory

    std::optional<GpuProfiler> gpuProfiler_;
    CpuProfiler cpuProfiler;

    // Flamegraph capture storage
    CpuFlamegraphHistory cpuFlamegraphHistory_;
    GpuFlamegraphHistory gpuFlamegraphHistory_;
    FlamegraphCapture initFlamegraph_;
    uint64_t frameNumber_ = 0;

    // Flamegraph capture settings
    uint32_t captureInterval_ = 30;      // Capture every N frames
    uint32_t framesSinceCapture_ = 0;
    bool flamegraphEnabled_ = true;
    bool capturePaused_ = false;         // Pause capture for inspection
};

/**
 * RAII helper for GPU profiling zones.
 */
class ScopedGpuZone {
public:
    ScopedGpuZone(Profiler& profiler, VkCommandBuffer cmd, const char* zoneName)
        : profiler(profiler), cmd(cmd), name(zoneName) {
        profiler.beginGpuZone(cmd, name);
    }

    ~ScopedGpuZone() {
        profiler.endGpuZone(cmd, name);
    }

    ScopedGpuZone(const ScopedGpuZone&) = delete;
    ScopedGpuZone& operator=(const ScopedGpuZone&) = delete;

private:
    Profiler& profiler;
    VkCommandBuffer cmd;
    const char* name;
};

// Macros for convenient profiling
#define PROFILE_GPU_ZONE(profiler, cmd, name) \
    ScopedGpuZone _gpuZone##__LINE__(profiler, cmd, name)

#define PROFILE_CPU_ZONE(profiler, name) \
    CpuProfiler::ScopedZone _cpuZone##__LINE__(profiler.getCpuProfiler(), name)
