#pragma once

#include <vulkan/vulkan.hpp>
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

    // === Command Buffer Stats ===
    uint32_t drawCallCount = 0;
    uint32_t dispatchCount = 0;           // Compute dispatches
    uint32_t pipelineBindCount = 0;
    uint32_t descriptorSetBindCount = 0;
    uint32_t pushConstantCount = 0;
    uint32_t renderPassCount = 0;
    uint32_t pipelineBarrierCount = 0;

    // === Validation Layer Status ===
    bool validationLayersEnabled = false;

    // === Derived Metrics ===
    uint32_t totalCommandCount() const {
        return drawCallCount + dispatchCount + pipelineBindCount +
               descriptorSetBindCount + pushConstantCount +
               renderPassCount + pipelineBarrierCount;
    }

    // Reset for new frame
    void reset() {
        fenceWasAlreadySignaled = false;
        fenceWaitTimeMs = 0.0f;
        queueSubmitTimeMs = 0.0f;
        drawCallCount = 0;
        dispatchCount = 0;
        pipelineBindCount = 0;
        descriptorSetBindCount = 0;
        pushConstantCount = 0;
        renderPassCount = 0;
        pipelineBarrierCount = 0;
        // validationLayersEnabled persists
    }

    // Get diagnostic summary string
    std::string getSummary() const {
        std::string result;
        result.reserve(256);

        // Fence status
        if (fenceWasAlreadySignaled) {
            result += "Fence: signaled (GPU idle)\n";
        } else {
            result += "Fence: waited " + std::to_string(fenceWaitTimeMs) + "ms (GPU-bound)\n";
        }

        // Submit time
        result += "Submit: " + std::to_string(queueSubmitTimeMs) + "ms\n";

        // Command counts
        result += "Draws: " + std::to_string(drawCallCount);
        result += " Dispatches: " + std::to_string(dispatchCount);
        result += " Binds: " + std::to_string(pipelineBindCount + descriptorSetBindCount);
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
 * CommandCounter - RAII wrapper to track Vulkan commands during recording.
 *
 * Wraps a VkCommandBuffer and counts commands as they're recorded.
 * Use this instead of raw command buffer when you want diagnostics.
 */
class CommandCounter {
public:
    explicit CommandCounter(QueueSubmitDiagnostics& diag) : diag_(diag) {}

    // Call these after the corresponding Vulkan commands
    void recordDraw() { diag_.drawCallCount++; }
    void recordDrawIndexed() { diag_.drawCallCount++; }
    void recordDrawIndirect() { diag_.drawCallCount++; }
    void recordDrawIndexedIndirect() { diag_.drawCallCount++; }
    void recordDispatch() { diag_.dispatchCount++; }
    void recordDispatchIndirect() { diag_.dispatchCount++; }
    void recordBindPipeline() { diag_.pipelineBindCount++; }
    void recordBindDescriptorSets() { diag_.descriptorSetBindCount++; }
    void recordPushConstants() { diag_.pushConstantCount++; }
    void recordBeginRenderPass() { diag_.renderPassCount++; }
    void recordPipelineBarrier() { diag_.pipelineBarrierCount++; }

    // Bulk increment for systems that batch commands
    void recordDrawCalls(uint32_t count) { diag_.drawCallCount += count; }
    void recordDispatches(uint32_t count) { diag_.dispatchCount += count; }

private:
    QueueSubmitDiagnostics& diag_;
};
