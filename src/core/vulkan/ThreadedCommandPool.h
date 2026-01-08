#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <mutex>

class VulkanContext;

/**
 * ThreadedCommandPool - Multi-threaded command pool manager.
 *
 * Implements the command pool strategy from the video:
 * - pools_[frameIndex][threadId] = unique command pool
 * - Total pools = framesInFlight × threadCount
 *
 * This allows parallel command buffer recording across threads
 * without synchronization, because each thread has its own pool.
 *
 * Usage:
 *   // At frame start
 *   pool.resetFrame(currentFrame);
 *
 *   // In parallel recording tasks
 *   int threadId = TaskScheduler::instance().getCurrentThreadId();
 *   vk::CommandBuffer cmd = pool.allocatePrimary(currentFrame, threadId);
 *   // ... record commands ...
 *
 *   // For secondary command buffers (parallel draw recording)
 *   vk::CommandBuffer secondary = pool.allocateSecondary(currentFrame, threadId);
 */
class ThreadedCommandPool {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    ThreadedCommandPool() = default;
    ~ThreadedCommandPool() = default;

    // Non-copyable
    ThreadedCommandPool(const ThreadedCommandPool&) = delete;
    ThreadedCommandPool& operator=(const ThreadedCommandPool&) = delete;

    /**
     * Initialize command pools.
     * @param context Vulkan context
     * @param threadCount Number of worker threads (from TaskScheduler)
     */
    bool initialize(VulkanContext& context, uint32_t threadCount);

    /**
     * Shutdown and release all pools.
     */
    void shutdown();

    /**
     * Reset all pools for a given frame.
     * Call at the start of each frame before recording.
     * @param frameIndex Current frame index (0 to MAX_FRAMES_IN_FLIGHT-1)
     */
    void resetFrame(uint32_t frameIndex);

    /**
     * Allocate a primary command buffer for a thread.
     * @param frameIndex Current frame index
     * @param threadId Worker thread ID (from TaskScheduler)
     * @return Primary command buffer ready for recording
     */
    vk::CommandBuffer allocatePrimary(uint32_t frameIndex, uint32_t threadId);

    /**
     * Allocate a secondary command buffer for parallel draw recording.
     * Secondary buffers must inherit from a render pass.
     * @param frameIndex Current frame index
     * @param threadId Worker thread ID
     * @return Secondary command buffer ready for recording
     */
    vk::CommandBuffer allocateSecondary(uint32_t frameIndex, uint32_t threadId);

    /**
     * Get the command pool for a specific frame and thread.
     * Useful for custom allocations.
     */
    vk::CommandPool getPool(uint32_t frameIndex, uint32_t threadId) const;

    /**
     * Get thread count.
     */
    uint32_t getThreadCount() const { return threadCount_; }

    /**
     * Check if initialized.
     */
    bool isInitialized() const { return initialized_; }

private:
    struct PerThreadPool {
        vk::raii::CommandPool pool{nullptr};

        // Pre-allocated command buffers for reuse
        std::vector<vk::CommandBuffer> primaryBuffers;
        std::vector<vk::CommandBuffer> secondaryBuffers;
        uint32_t nextPrimary = 0;
        uint32_t nextSecondary = 0;
    };

    // pools_[frameIndex][threadId]
    std::vector<std::vector<PerThreadPool>> pools_;

    vk::Device device_;
    uint32_t graphicsQueueFamily_ = 0;
    uint32_t threadCount_ = 0;
    bool initialized_ = false;

    // Mutex for allocation (rarely contended since each thread uses its own pool)
    mutable std::mutex allocationMutex_;

    static constexpr uint32_t INITIAL_PRIMARY_BUFFERS = 2;
    static constexpr uint32_t INITIAL_SECONDARY_BUFFERS = 4;
};

/**
 * RAII wrapper for beginning/ending secondary command buffer with inheritance.
 * Automatically sets up the inheritance info required for render pass continuation.
 */
class SecondaryCommandBufferScope {
public:
    SecondaryCommandBufferScope(
        vk::CommandBuffer buffer,
        vk::RenderPass renderPass,
        uint32_t subpass,
        vk::Framebuffer framebuffer,
        bool occlusionQueryEnable = false)
        : buffer_(buffer)
    {
        auto inheritance = vk::CommandBufferInheritanceInfo{}
            .setRenderPass(renderPass)
            .setSubpass(subpass)
            .setFramebuffer(framebuffer)
            .setOcclusionQueryEnable(occlusionQueryEnable);

        buffer_.begin(vk::CommandBufferBeginInfo{}
            .setFlags(vk::CommandBufferUsageFlagBits::eRenderPassContinue |
                      vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
            .setPInheritanceInfo(&inheritance));
    }

    ~SecondaryCommandBufferScope() {
        buffer_.end();
    }

    vk::CommandBuffer get() const { return buffer_; }
    operator vk::CommandBuffer() const { return buffer_; }

private:
    vk::CommandBuffer buffer_;
};
