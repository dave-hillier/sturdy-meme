#pragma once

// ============================================================================
// FrameExecutor.h - Frame loop execution with callback-based frame building
// ============================================================================
//
// FrameExecutor owns the per-frame execution loop:
//   1. Frame synchronization (wait for previous frame via timeline semaphore)
//   2. Swapchain image acquisition
//   3. Invoke caller's frame builder callback (records commands)
//   4. Queue submission with timeline semaphore signaling
//   5. Swapchain presentation
//
// The Renderer builds per-frame data and records commands via the callback,
// while FrameExecutor handles all the synchronization and submission mechanics.
//
// Usage:
//   FrameExecutor executor;
//   executor.init({vulkanContext, &frameSync});
//
//   // In render loop:
//   FrameResult result = executor.execute(
//       [&](const FrameBuildContext& ctx) -> std::optional<FrameBuildResult> {
//           // ... update UBOs, record commands ...
//           return FrameBuildResult{commandBuffer};
//       },
//       &diagnostics, &profiler);
//
//   // Post-frame housekeeping (buffer set advancement, etc.)
//   executor.advance();
//

#include "TripleBuffering.h"
#include <vulkan/vulkan.hpp>
#include <functional>
#include <optional>

class VulkanContext;
struct QueueSubmitDiagnostics;
class Profiler;

// Result of frame operations
enum class FrameResult {
    Success,            // Frame rendered successfully
    SwapchainOutOfDate, // Swapchain needs recreation
    SurfaceLost,        // Surface lost (macOS screen lock)
    DeviceLost,         // Device lost
    AcquireFailed,      // Failed to acquire swapchain image
    SubmitFailed,       // Failed to submit command buffer
    Skipped             // Frame skipped (minimized, suspended)
};

// Context provided to the frame builder callback
struct FrameBuildContext {
    uint32_t imageIndex;    // Acquired swapchain image index
    uint32_t frameIndex;    // Frame-in-flight index (0..N-1) for buffer selection
};

// Result returned by the frame builder callback
struct FrameBuildResult {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
};

class FrameExecutor {
public:
    FrameExecutor() = default;
    ~FrameExecutor() = default;

    // Non-copyable, movable
    FrameExecutor(const FrameExecutor&) = delete;
    FrameExecutor& operator=(const FrameExecutor&) = delete;
    FrameExecutor(FrameExecutor&&) noexcept = default;
    FrameExecutor& operator=(FrameExecutor&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    struct InitParams {
        VulkanContext* vulkanContext = nullptr;
        TripleBuffering* frameSync = nullptr;   // Non-owning (owned by Renderer)
    };

    bool init(const InitParams& params);
    void destroy();
    bool isInitialized() const { return vulkanContext_ != nullptr && frameSync_ != nullptr && frameSync_->isInitialized(); }

    // =========================================================================
    // High-level frame execution
    // =========================================================================

    // Execute a complete frame: sync → acquire → build → submit → present.
    // The builder callback records commands and returns the command buffer.
    // Does NOT advance frame sync — caller must call advance() after any
    // post-frame housekeeping (buffer set advancement, profiler end, etc.).
    using FrameBuilder = std::function<std::optional<FrameBuildResult>(const FrameBuildContext&)>;
    FrameResult execute(const FrameBuilder& builder,
                        QueueSubmitDiagnostics* diagnostics = nullptr,
                        Profiler* profiler = nullptr);

    // Advance to next frame slot. Call after post-frame housekeeping.
    void advance() { frameSync_->advance(); }

    // =========================================================================
    // Low-level frame phases (for callers needing manual control)
    // =========================================================================

    struct FrameBeginResult {
        bool success = false;
        uint32_t imageIndex = 0;
        FrameResult error = FrameResult::Success;
    };

    FrameBeginResult beginFrame();
    FrameBeginResult beginFrame(QueueSubmitDiagnostics& diagnostics, Profiler& profiler);
    FrameResult submitAndPresent(VkCommandBuffer cmd, uint32_t imageIndex, QueueSubmitDiagnostics* diagnostics = nullptr);

    // =========================================================================
    // Synchronization access
    // =========================================================================

    TripleBuffering& getFrameSync() { return *frameSync_; }
    const TripleBuffering& getFrameSync() const { return *frameSync_; }

    uint32_t currentFrameIndex() const { return frameSync_->currentIndex(); }

    void waitForPreviousFrame() { frameSync_->waitForPreviousFrame(); }
    void waitForAllFrames() { frameSync_->waitForAllFrames(); }
    bool isCurrentFrameReady() const { return frameSync_->isCurrentFrameComplete(); }

    // =========================================================================
    // Resize handling
    // =========================================================================

    void notifyResizeNeeded() { resizeNeeded_ = true; }
    bool isResizeNeeded() const { return resizeNeeded_; }
    void clearResizeFlag() { resizeNeeded_ = false; }

    void notifyWindowSuspended() { windowSuspended_ = true; }
    void notifyWindowRestored() {
        windowSuspended_ = false;
        resizeNeeded_ = true;
    }
    bool isWindowSuspended() const { return windowSuspended_; }

private:
    FrameBeginResult acquireSwapchainImage();
    FrameResult submitCommandBuffer(VkCommandBuffer cmd, QueueSubmitDiagnostics* diagnostics);
    FrameResult present(uint32_t imageIndex, QueueSubmitDiagnostics* diagnostics);

    VulkanContext* vulkanContext_ = nullptr;
    TripleBuffering* frameSync_ = nullptr;  // Non-owning reference (owned by Renderer)

    bool resizeNeeded_ = false;
    bool windowSuspended_ = false;

    uint32_t currentImageIndex_ = 0;
};
