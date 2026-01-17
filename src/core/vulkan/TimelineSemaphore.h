#pragma once

// ============================================================================
// TimelineSemaphore.h - Vulkan 1.2 timeline semaphore wrapper
// ============================================================================
//
// Timeline semaphores provide several advantages over binary semaphores + fences:
// - Non-blocking counter queries (vkGetSemaphoreCounterValue)
// - Host-side signaling without command buffers (vkSignalSemaphore)
// - Multiple wait values in a single submit
// - Cleaner synchronization model with monotonically increasing values
//
// This class wraps a timeline semaphore with convenient methods for:
// - Querying current GPU counter value (non-blocking)
// - Checking if a specific value has been reached
// - Waiting with timeout for specific values
// - Host-side signaling
//

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <cstdint>

/**
 * RAII wrapper for Vulkan timeline semaphores (Vulkan 1.2 core feature).
 *
 * Timeline semaphores maintain a monotonically increasing 64-bit counter.
 * GPU operations signal the semaphore by setting the counter to a value,
 * and waits block until the counter reaches or exceeds a target value.
 */
class TimelineSemaphore {
public:
    TimelineSemaphore() = default;
    ~TimelineSemaphore() = default;

    // Non-copyable (Vulkan resource)
    TimelineSemaphore(const TimelineSemaphore&) = delete;
    TimelineSemaphore& operator=(const TimelineSemaphore&) = delete;

    // Movable
    TimelineSemaphore(TimelineSemaphore&&) noexcept = default;
    TimelineSemaphore& operator=(TimelineSemaphore&&) noexcept = default;

    /**
     * Initialize the timeline semaphore.
     * @param device Vulkan device (must support timeline semaphores)
     * @param initialValue Starting counter value (default 0)
     * @return true on success
     */
    bool init(const vk::raii::Device& device, uint64_t initialValue = 0);

    /**
     * Check if semaphore is initialized.
     */
    bool isInitialized() const { return semaphore_.has_value() && device_ != nullptr; }

    /**
     * Get the underlying Vulkan semaphore handle.
     */
    vk::Semaphore get() const { return semaphore_ ? **semaphore_ : vk::Semaphore{}; }

    /**
     * Get the raw VkSemaphore handle.
     */
    VkSemaphore getHandle() const { return semaphore_ ? static_cast<VkSemaphore>(**semaphore_) : VK_NULL_HANDLE; }

    // =========================================================================
    // Counter Queries (Non-blocking)
    // =========================================================================

    /**
     * Get current semaphore counter value from the GPU (non-blocking).
     * This is the key function for non-blocking completion checks.
     */
    uint64_t getCounterValue() const;

    /**
     * Check if the semaphore counter has reached a specific value (non-blocking).
     * @param value Target value to check
     * @return true if counter >= value
     */
    bool hasReached(uint64_t value) const {
        return getCounterValue() >= value;
    }

    /**
     * Get the next signal value (for tracking in flight work).
     * Call this before submitting work, then pass to signalValue in submit.
     */
    uint64_t nextSignalValue() { return ++pendingSignalValue_; }

    /**
     * Get the current pending signal value (the last value returned by nextSignalValue).
     */
    uint64_t pendingSignalValue() const { return pendingSignalValue_; }

    // =========================================================================
    // Host Operations
    // =========================================================================

    /**
     * Wait on host until semaphore reaches a value (blocking).
     * @param value Target value to wait for
     * @param timeoutNs Timeout in nanoseconds (default UINT64_MAX = infinite)
     * @return vk::Result::eSuccess on completion, eTimeout on timeout
     */
    vk::Result wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX) const;

    /**
     * Wait on host until semaphore reaches pending signal value.
     * Equivalent to wait(pendingSignalValue()).
     */
    vk::Result waitForPending(uint64_t timeoutNs = UINT64_MAX) const {
        return wait(pendingSignalValue_, timeoutNs);
    }

    /**
     * Signal the semaphore from the host (without GPU work).
     * @param value Value to signal (must be > current counter)
     * @return true on success
     */
    bool signal(uint64_t value);

    // =========================================================================
    // Submit Helpers
    // =========================================================================

    /**
     * Create a SemaphoreSubmitInfo for vkQueueSubmit2 (Vulkan 1.3 / KHR).
     * @param value Value to wait for or signal
     * @param stageMask Pipeline stage mask for wait operations
     */
    vk::SemaphoreSubmitInfo createSubmitInfo(
        uint64_t value,
        vk::PipelineStageFlags2 stageMask = vk::PipelineStageFlagBits2::eAllCommands) const;

    /**
     * Fill TimelineSemaphoreSubmitInfo for vkQueueSubmit (Vulkan 1.2).
     * Use with vk::SubmitInfo and attach via pNext.
     */
    static vk::TimelineSemaphoreSubmitInfo createTimelineSubmitInfo(
        const uint64_t* waitValues, uint32_t waitCount,
        const uint64_t* signalValues, uint32_t signalCount);

private:
    const vk::raii::Device* device_ = nullptr;
    std::optional<vk::raii::Semaphore> semaphore_;
    uint64_t pendingSignalValue_ = 0;
};

// ============================================================================
// Utility functions for timeline semaphore operations
// ============================================================================

namespace TimelineSemaphoreUtils {

/**
 * Create a timeline semaphore create info structure.
 * Chain this to SemaphoreCreateInfo::pNext.
 */
inline vk::SemaphoreTypeCreateInfo createTimelineTypeInfo(uint64_t initialValue = 0) {
    return vk::SemaphoreTypeCreateInfo{}
        .setSemaphoreType(vk::SemaphoreType::eTimeline)
        .setInitialValue(initialValue);
}

/**
 * Create a complete SemaphoreCreateInfo for a timeline semaphore.
 */
inline std::pair<vk::SemaphoreCreateInfo, vk::SemaphoreTypeCreateInfo>
createTimelineSemaphoreCreateInfo(uint64_t initialValue = 0) {
    auto typeInfo = createTimelineTypeInfo(initialValue);
    auto createInfo = vk::SemaphoreCreateInfo{}.setPNext(&typeInfo);
    return {createInfo, typeInfo};
}

} // namespace TimelineSemaphoreUtils
