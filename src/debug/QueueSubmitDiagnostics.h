#pragma once

#include <vulkan/vulkan.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

/**
 * QueueSubmitDiagnostics - Tracks metrics to diagnose high queue submit times.
 *
 * Common causes of high vkQueueSubmit CPU time:
 * 1. Validation layers enabled (adds significant overhead)
 * 2. GPU not finished with previous frame (implicit wait in driver)
 * 3. Large command buffer (driver validation/processing)
 * 4. Many pipeline/descriptor bindings (driver state tracking)
 * 5. Resource hazards requiring driver-side synchronization
 */
struct QueueSubmitDiagnostics {
    // === Frame Timing ===
    // Was the fence already signaled before we waited?
    // If false, CPU was blocked waiting for GPU = GPU-bound
    bool fenceWasAlreadySignaled = false;

    // Time spent waiting for fence (ms)
    float fenceWaitTimeMs = 0.0f;

    // Time spent in vkQueueSubmit call itself (ms)
    float queueSubmitTimeMs = 0.0f;

    // === Detailed Timing Breakdown ===
    // Time spent recording commands (CPU side)
    float commandRecordTimeMs = 0.0f;

    // Time from frame start to submit (total CPU frame time)
    float frameToSubmitTimeMs = 0.0f;

    // Time spent in vkAcquireNextImageKHR
    float acquireImageTimeMs = 0.0f;

    // Time spent in vkQueuePresentKHR
    float presentTimeMs = 0.0f;

    // === Command Buffer Stats (atomic for thread-safety) ===
    std::atomic<uint32_t> drawCallCount{0};
    std::atomic<uint32_t> dispatchCount{0};
    std::atomic<uint32_t> pipelineBindCount{0};
    std::atomic<uint32_t> descriptorSetBindCount{0};
    std::atomic<uint32_t> pushConstantCount{0};
    std::atomic<uint32_t> renderPassCount{0};
    std::atomic<uint32_t> pipelineBarrierCount{0};

    // === Bandwidth/Memory Stats (atomic for thread-safety) ===
    std::atomic<uint64_t> uboUpdateBytes{0};        // Total UBO data written this frame
    std::atomic<uint64_t> ssboUpdateBytes{0};       // Total SSBO data written this frame
    std::atomic<uint64_t> pushConstantBytes{0};     // Total push constant data
    std::atomic<uint32_t> bufferBarrierCount{0};    // Buffer memory barriers
    std::atomic<uint32_t> imageBarrierCount{0};     // Image memory barriers

    // === Per-pass breakdown ===
    static constexpr size_t MAX_PASS_STATS = 32;
    struct PassStats {
        const char* name = nullptr;
        uint32_t drawCalls = 0;
        uint32_t dispatches = 0;
        float recordTimeMs = 0.0f;
    };
    PassStats passStats[MAX_PASS_STATS];
    std::atomic<uint32_t> passCount{0};

    // === Validation Layer Status ===
    bool validationLayersEnabled = false;

    // === Derived Metrics ===
    uint32_t totalCommandCount() const {
        return drawCallCount.load(std::memory_order_relaxed) +
               dispatchCount.load(std::memory_order_relaxed) +
               pipelineBindCount.load(std::memory_order_relaxed) +
               descriptorSetBindCount.load(std::memory_order_relaxed) +
               pushConstantCount.load(std::memory_order_relaxed) +
               renderPassCount.load(std::memory_order_relaxed) +
               pipelineBarrierCount.load(std::memory_order_relaxed);
    }

    // Non-atomic getters for GUI display
    uint32_t getDrawCallCount() const { return drawCallCount.load(std::memory_order_relaxed); }
    uint32_t getDispatchCount() const { return dispatchCount.load(std::memory_order_relaxed); }
    uint32_t getPipelineBindCount() const { return pipelineBindCount.load(std::memory_order_relaxed); }
    uint32_t getDescriptorSetBindCount() const { return descriptorSetBindCount.load(std::memory_order_relaxed); }
    uint32_t getPushConstantCount() const { return pushConstantCount.load(std::memory_order_relaxed); }
    uint32_t getRenderPassCount() const { return renderPassCount.load(std::memory_order_relaxed); }
    uint32_t getPipelineBarrierCount() const { return pipelineBarrierCount.load(std::memory_order_relaxed); }
    uint32_t getPassCount() const { return passCount.load(std::memory_order_relaxed); }

    // Bandwidth getters
    uint64_t getUboUpdateBytes() const { return uboUpdateBytes.load(std::memory_order_relaxed); }
    uint64_t getSsboUpdateBytes() const { return ssboUpdateBytes.load(std::memory_order_relaxed); }
    uint64_t getPushConstantBytes() const { return pushConstantBytes.load(std::memory_order_relaxed); }
    uint32_t getBufferBarrierCount() const { return bufferBarrierCount.load(std::memory_order_relaxed); }
    uint32_t getImageBarrierCount() const { return imageBarrierCount.load(std::memory_order_relaxed); }
    uint64_t getTotalBandwidthBytes() const {
        return getUboUpdateBytes() + getSsboUpdateBytes() + getPushConstantBytes();
    }

    // Record stats for a pass
    void recordPassStats(const char* name, uint32_t draws, uint32_t dispatches, float timeMs) {
        uint32_t idx = passCount.fetch_add(1, std::memory_order_relaxed);
        if (idx < MAX_PASS_STATS) {
            passStats[idx] = {name, draws, dispatches, timeMs};
        }
    }

    // Reset for new frame
    void reset() {
        fenceWasAlreadySignaled = false;
        fenceWaitTimeMs = 0.0f;
        queueSubmitTimeMs = 0.0f;
        commandRecordTimeMs = 0.0f;
        frameToSubmitTimeMs = 0.0f;
        acquireImageTimeMs = 0.0f;
        presentTimeMs = 0.0f;
        drawCallCount.store(0, std::memory_order_relaxed);
        dispatchCount.store(0, std::memory_order_relaxed);
        pipelineBindCount.store(0, std::memory_order_relaxed);
        descriptorSetBindCount.store(0, std::memory_order_relaxed);
        pushConstantCount.store(0, std::memory_order_relaxed);
        renderPassCount.store(0, std::memory_order_relaxed);
        pipelineBarrierCount.store(0, std::memory_order_relaxed);
        uboUpdateBytes.store(0, std::memory_order_relaxed);
        ssboUpdateBytes.store(0, std::memory_order_relaxed);
        pushConstantBytes.store(0, std::memory_order_relaxed);
        bufferBarrierCount.store(0, std::memory_order_relaxed);
        imageBarrierCount.store(0, std::memory_order_relaxed);
        passCount.store(0, std::memory_order_relaxed);
        // validationLayersEnabled persists
    }

    // Get diagnostic summary string
    std::string getSummary() const {
        std::string result;
        result.reserve(512);

        // Fence status
        if (fenceWasAlreadySignaled) {
            result += "Fence: signaled (GPU idle)\n";
        } else {
            result += "Fence: waited " + std::to_string(fenceWaitTimeMs) + "ms (GPU-bound)\n";
        }

        // Timing breakdown
        result += "Timing: record=" + std::to_string(commandRecordTimeMs) + "ms";
        result += " submit=" + std::to_string(queueSubmitTimeMs) + "ms";
        result += " acquire=" + std::to_string(acquireImageTimeMs) + "ms";
        result += " present=" + std::to_string(presentTimeMs) + "ms\n";

        // Command counts
        result += "Draws: " + std::to_string(getDrawCallCount());
        result += " Dispatches: " + std::to_string(getDispatchCount());
        result += " Binds: " + std::to_string(getPipelineBindCount() + getDescriptorSetBindCount());
        result += " Total: " + std::to_string(totalCommandCount()) + "\n";

        // Warnings
        if (validationLayersEnabled) {
            result += "WARNING: Validation layers enabled!\n";
        }
        if (queueSubmitTimeMs > 1.0f && !validationLayersEnabled) {
            result += "WARNING: High submit time without validation - check driver?\n";
        }

        return result;
    }
};

/**
 * CommandCounter - Thread-safe wrapper to track Vulkan commands during recording.
 *
 * Use this to count commands as they're recorded. Thread-safe for parallel
 * command buffer recording.
 */
class CommandCounter {
public:
    explicit CommandCounter(QueueSubmitDiagnostics& diag) : diag_(&diag) {}
    CommandCounter(QueueSubmitDiagnostics* diag) : diag_(diag) {}

    // Check if diagnostics are available
    bool isEnabled() const { return diag_ != nullptr; }

    // Call these after the corresponding Vulkan commands
    void recordDraw() { if (diag_) diag_->drawCallCount.fetch_add(1, std::memory_order_relaxed); }
    void recordDrawIndexed() { if (diag_) diag_->drawCallCount.fetch_add(1, std::memory_order_relaxed); }
    void recordDrawIndirect() { if (diag_) diag_->drawCallCount.fetch_add(1, std::memory_order_relaxed); }
    void recordDrawIndexedIndirect() { if (diag_) diag_->drawCallCount.fetch_add(1, std::memory_order_relaxed); }
    void recordDispatch() { if (diag_) diag_->dispatchCount.fetch_add(1, std::memory_order_relaxed); }
    void recordDispatchIndirect() { if (diag_) diag_->dispatchCount.fetch_add(1, std::memory_order_relaxed); }
    void recordBindPipeline() { if (diag_) diag_->pipelineBindCount.fetch_add(1, std::memory_order_relaxed); }
    void recordBindDescriptorSets() { if (diag_) diag_->descriptorSetBindCount.fetch_add(1, std::memory_order_relaxed); }
    void recordPushConstants() { if (diag_) diag_->pushConstantCount.fetch_add(1, std::memory_order_relaxed); }
    void recordBeginRenderPass() { if (diag_) diag_->renderPassCount.fetch_add(1, std::memory_order_relaxed); }
    void recordPipelineBarrier() { if (diag_) diag_->pipelineBarrierCount.fetch_add(1, std::memory_order_relaxed); }

    // Bulk increment for systems that batch commands
    void recordDrawCalls(uint32_t count) {
        if (diag_) diag_->drawCallCount.fetch_add(count, std::memory_order_relaxed);
    }
    void recordDispatches(uint32_t count) {
        if (diag_) diag_->dispatchCount.fetch_add(count, std::memory_order_relaxed);
    }

    // Bandwidth tracking
    void recordUboUpdate(uint64_t bytes) {
        if (diag_) diag_->uboUpdateBytes.fetch_add(bytes, std::memory_order_relaxed);
    }
    void recordSsboUpdate(uint64_t bytes) {
        if (diag_) diag_->ssboUpdateBytes.fetch_add(bytes, std::memory_order_relaxed);
    }
    void recordPushConstantUpdate(uint64_t bytes) {
        if (diag_) {
            diag_->pushConstantCount.fetch_add(1, std::memory_order_relaxed);
            diag_->pushConstantBytes.fetch_add(bytes, std::memory_order_relaxed);
        }
    }
    void recordBufferBarrier() {
        if (diag_) diag_->bufferBarrierCount.fetch_add(1, std::memory_order_relaxed);
    }
    void recordImageBarrier() {
        if (diag_) diag_->imageBarrierCount.fetch_add(1, std::memory_order_relaxed);
    }
    void recordBarriers(uint32_t bufferCount, uint32_t imageCount) {
        if (diag_) {
            diag_->pipelineBarrierCount.fetch_add(1, std::memory_order_relaxed);
            diag_->bufferBarrierCount.fetch_add(bufferCount, std::memory_order_relaxed);
            diag_->imageBarrierCount.fetch_add(imageCount, std::memory_order_relaxed);
        }
    }

private:
    QueueSubmitDiagnostics* diag_;
};

/**
 * Global diagnostics pointer for command counting.
 * Set this at the start of command recording to enable automatic counting
 * in subsystems that don't have direct access to diagnostics.
 *
 * Thread-local to support parallel command recording.
 */
inline thread_local QueueSubmitDiagnostics* g_currentDiagnostics = nullptr;

/**
 * RAII helper to set the global diagnostics pointer for a scope.
 */
class ScopedDiagnostics {
public:
    explicit ScopedDiagnostics(QueueSubmitDiagnostics* diag)
        : prev_(g_currentDiagnostics) {
        g_currentDiagnostics = diag;
    }
    ~ScopedDiagnostics() {
        g_currentDiagnostics = prev_;
    }
    ScopedDiagnostics(const ScopedDiagnostics&) = delete;
    ScopedDiagnostics& operator=(const ScopedDiagnostics&) = delete;
private:
    QueueSubmitDiagnostics* prev_;
};

// Helper macros for common command counting
#define DIAG_RECORD_DRAW() do { if (g_currentDiagnostics) g_currentDiagnostics->drawCallCount.fetch_add(1, std::memory_order_relaxed); } while(0)
#define DIAG_RECORD_DRAWS(n) do { if (g_currentDiagnostics) g_currentDiagnostics->drawCallCount.fetch_add(n, std::memory_order_relaxed); } while(0)
#define DIAG_RECORD_DISPATCH() do { if (g_currentDiagnostics) g_currentDiagnostics->dispatchCount.fetch_add(1, std::memory_order_relaxed); } while(0)
#define DIAG_RECORD_DISPATCHES(n) do { if (g_currentDiagnostics) g_currentDiagnostics->dispatchCount.fetch_add(n, std::memory_order_relaxed); } while(0)
#define DIAG_RECORD_BIND_PIPELINE() do { if (g_currentDiagnostics) g_currentDiagnostics->pipelineBindCount.fetch_add(1, std::memory_order_relaxed); } while(0)
#define DIAG_RECORD_BIND_DESCRIPTOR() do { if (g_currentDiagnostics) g_currentDiagnostics->descriptorSetBindCount.fetch_add(1, std::memory_order_relaxed); } while(0)
#define DIAG_RECORD_BARRIER() do { if (g_currentDiagnostics) g_currentDiagnostics->pipelineBarrierCount.fetch_add(1, std::memory_order_relaxed); } while(0)

/**
 * ScopedPassStats - RAII helper to track per-pass timing and command counts.
 */
class ScopedPassStats {
public:
    ScopedPassStats(QueueSubmitDiagnostics* diag, const char* passName)
        : diag_(diag)
        , passName_(passName)
        , startTime_(std::chrono::high_resolution_clock::now())
        , startDraws_(diag ? diag->getDrawCallCount() : 0)
        , startDispatches_(diag ? diag->getDispatchCount() : 0)
    {}

    ~ScopedPassStats() {
        if (diag_) {
            auto endTime = std::chrono::high_resolution_clock::now();
            float timeMs = std::chrono::duration<float, std::milli>(endTime - startTime_).count();
            uint32_t draws = diag_->getDrawCallCount() - startDraws_;
            uint32_t dispatches = diag_->getDispatchCount() - startDispatches_;
            diag_->recordPassStats(passName_, draws, dispatches, timeMs);
        }
    }

    // Non-copyable
    ScopedPassStats(const ScopedPassStats&) = delete;
    ScopedPassStats& operator=(const ScopedPassStats&) = delete;

private:
    QueueSubmitDiagnostics* diag_;
    const char* passName_;
    std::chrono::high_resolution_clock::time_point startTime_;
    uint32_t startDraws_;
    uint32_t startDispatches_;
};
