#pragma once

// ============================================================================
// TripleBuffering.h - Frame synchronization using FrameBuffered template
// ============================================================================
//
// This header provides a specialized TripleBuffering class that uses the
// generic FrameBuffered<T> template for managing Vulkan synchronization
// primitives (fences and semaphores) per frame.
//
// The class encapsulates:
// - Per-frame synchronization primitives (fences, semaphores)
// - Frame index management and cycling
// - Convenient methods for common synchronization patterns
//
// Usage:
//   TripleBuffering frames;
//   if (!frames.init(device)) return false;
//
//   // In render loop:
//   frames.waitForCurrentFrameIfNeeded();
//   uint32_t idx = frames.currentIndex();
//   // ... record commands using idx for buffer selection ...
//   frames.resetCurrentFence();
//   // ... submit commands with frames.currentImageAvailableSemaphore() ...
//   frames.advance();
//

#include "FrameBuffered.h"
#include "vulkan/VulkanRAII.h"
#include <vulkan/vulkan.h>

// ============================================================================
// FrameSyncPrimitives - Per-frame synchronization resources
// ============================================================================

struct FrameSyncPrimitives {
    ManagedSemaphore imageAvailable;
    ManagedSemaphore renderFinished;
    ManagedFence inFlightFence;

    // Non-copyable (Vulkan resources)
    FrameSyncPrimitives() = default;
    FrameSyncPrimitives(const FrameSyncPrimitives&) = delete;
    FrameSyncPrimitives& operator=(const FrameSyncPrimitives&) = delete;

    // Movable
    FrameSyncPrimitives(FrameSyncPrimitives&&) noexcept = default;
    FrameSyncPrimitives& operator=(FrameSyncPrimitives&&) noexcept = default;
};

// ============================================================================
// TripleBuffering - Manages frame-in-flight synchronization using FrameBuffered
// ============================================================================

class TripleBuffering {
public:
    // Default to 3 frames in flight (triple buffering)
    static constexpr uint32_t DEFAULT_FRAME_COUNT = 3;

    TripleBuffering() = default;
    ~TripleBuffering() = default;

    // Non-copyable (contains Vulkan resources)
    TripleBuffering(const TripleBuffering&) = delete;
    TripleBuffering& operator=(const TripleBuffering&) = delete;

    // Movable
    TripleBuffering(TripleBuffering&&) noexcept = default;
    TripleBuffering& operator=(TripleBuffering&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    // Initialize synchronization primitives for the specified frame count
    // Returns false on failure
    bool init(VkDevice device, uint32_t frameCount = DEFAULT_FRAME_COUNT);

    // Clean up synchronization primitives
    void destroy();

    // Check if initialized
    bool isInitialized() const { return !frames_.empty(); }

    // =========================================================================
    // Frame Index Management (delegated to FrameBuffered)
    // =========================================================================

    uint32_t frameCount() const { return frames_.frameCount(); }
    uint32_t currentIndex() const { return frames_.currentIndex(); }
    uint32_t previousIndex() const { return frames_.previousIndex(); }
    uint32_t nextIndex() const { return frames_.nextIndex(); }
    uint32_t wrapIndex(uint32_t index) const { return frames_.wrapIndex(index); }

    void advance() { frames_.advance(); }
    void reset() { frames_.reset(); }

    // Get pointer to current frame index (for legacy code needing pointer access)
    const uint32_t* currentIndexPtr() const { return frames_.currentIndexPtr(); }

    // =========================================================================
    // Synchronization - Fences
    // =========================================================================

    // Get fence for current frame
    VkFence currentFence() const {
        return frames_.current().inFlightFence.get();
    }

    // Get fence for any frame index
    VkFence fence(uint32_t frameIndex) const {
        return frames_.at(frameIndex).inFlightFence.get();
    }

    // Check if current frame's fence is already signaled (non-blocking)
    bool isCurrentFenceSignaled() const {
        return frames_.current().inFlightFence.isSignaled();
    }

    // Wait for current frame's fence (blocks until signaled)
    void waitForCurrentFrame() const {
        frames_.current().inFlightFence.wait();
    }

    // Wait for current frame only if not already signaled (optimization)
    void waitForCurrentFrameIfNeeded() const {
        if (!frames_.current().inFlightFence.isSignaled()) {
            frames_.current().inFlightFence.wait();
        }
    }

    // Wait for previous frame's fence (useful before destroying resources)
    void waitForPreviousFrame() const {
        const auto& prev = frames_.previous();
        if (!prev.inFlightFence.isSignaled()) {
            prev.inFlightFence.wait();
        }
    }

    // Reset current frame's fence (call before queue submit)
    void resetCurrentFence() const {
        frames_.current().inFlightFence.resetFence();
    }

    // =========================================================================
    // Synchronization - Semaphores
    // =========================================================================

    // Get image available semaphore for current frame
    VkSemaphore currentImageAvailableSemaphore() const {
        return frames_.current().imageAvailable.get();
    }

    // Get render finished semaphore for current frame
    VkSemaphore currentRenderFinishedSemaphore() const {
        return frames_.current().renderFinished.get();
    }

    // Get semaphores for any frame index
    VkSemaphore imageAvailableSemaphore(uint32_t frameIndex) const {
        return frames_.at(frameIndex).imageAvailable.get();
    }

    VkSemaphore renderFinishedSemaphore(uint32_t frameIndex) const {
        return frames_.at(frameIndex).renderFinished.get();
    }

    // =========================================================================
    // Direct Access (for compatibility with existing code)
    // =========================================================================

    // Access the underlying FrameBuffered container
    const FrameBuffered<FrameSyncPrimitives>& frames() const { return frames_; }
    FrameBuffered<FrameSyncPrimitives>& frames() { return frames_; }

private:
    FrameBuffered<FrameSyncPrimitives> frames_;
};
