#pragma once

#include "GpuProfiler.h"
#include "CpuProfiler.h"
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
class Profiler {
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
     * Begin frame profiling (call after fence wait, before command buffer recording).
     */
    void beginFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
        cpuProfiler.beginFrame();
        if (gpuProfiler_) {
            gpuProfiler_->beginFrame(cmd, frameIndex);
        }
    }

    /**
     * End frame profiling (call after command buffer recording, before submit).
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

private:
    Profiler() = default;  // Private: use factory

    std::optional<GpuProfiler> gpuProfiler_;
    CpuProfiler cpuProfiler;
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
