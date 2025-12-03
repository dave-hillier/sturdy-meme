#pragma once

#include "GpuProfiler.h"
#include "CpuProfiler.h"

/**
 * Unified Profiler combining GPU timestamp queries and CPU timing.
 *
 * Provides a single interface for frame profiling with both GPU and CPU breakdown.
 * Results are accessible for GUI display.
 */
class Profiler {
public:
    Profiler() = default;
    ~Profiler() = default;

    /**
     * Initialize the profiler.
     */
    bool init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t framesInFlight) {
        return gpuProfiler.init(device, physicalDevice, framesInFlight);
    }

    void shutdown() {
        gpuProfiler.shutdown();
    }

    /**
     * Begin frame profiling (call after fence wait, before command buffer recording).
     */
    void beginFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
        cpuProfiler.beginFrame();
        gpuProfiler.beginFrame(cmd, frameIndex);
    }

    /**
     * End frame profiling (call after command buffer recording, before submit).
     */
    void endFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
        gpuProfiler.endFrame(cmd, frameIndex);
        cpuProfiler.endFrame();
    }

    /**
     * Begin a GPU profiling zone.
     */
    void beginGpuZone(VkCommandBuffer cmd, const char* zoneName) {
        gpuProfiler.beginZone(cmd, zoneName);
    }

    /**
     * End a GPU profiling zone.
     */
    void endGpuZone(VkCommandBuffer cmd, const char* zoneName) {
        gpuProfiler.endZone(cmd, zoneName);
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
    const GpuProfiler::FrameStats& getGpuResults() const { return gpuProfiler.getResults(); }
    const CpuProfiler::FrameStats& getCpuResults() const { return cpuProfiler.getResults(); }
    const CpuProfiler::FrameStats& getSmoothedCpuResults() const { return cpuProfiler.getSmoothedResults(); }

    // Enable/disable
    bool isGpuProfilingEnabled() const { return gpuProfiler.isEnabled(); }
    bool isCpuProfilingEnabled() const { return cpuProfiler.isEnabled(); }
    void setGpuProfilingEnabled(bool e) { gpuProfiler.setEnabled(e); }
    void setCpuProfilingEnabled(bool e) { cpuProfiler.setEnabled(e); }

    void setEnabled(bool e) {
        gpuProfiler.setEnabled(e);
        cpuProfiler.setEnabled(e);
    }

    bool isEnabled() const {
        return gpuProfiler.isEnabled() || cpuProfiler.isEnabled();
    }

    // Direct access to profilers
    GpuProfiler& getGpuProfiler() { return gpuProfiler; }
    CpuProfiler& getCpuProfiler() { return cpuProfiler; }
    const GpuProfiler& getGpuProfiler() const { return gpuProfiler; }
    const CpuProfiler& getCpuProfiler() const { return cpuProfiler; }

private:
    GpuProfiler gpuProfiler;
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
