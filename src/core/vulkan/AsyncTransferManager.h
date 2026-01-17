#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "VmaResources.h"
#include <deque>
#include <functional>
#include <mutex>
#include <optional>

class VulkanContext;

/**
 * Handle to a pending async transfer operation.
 * Check isComplete() or wait() before using the transferred resource.
 */
struct TransferHandle {
    uint64_t id = 0;

    bool isValid() const { return id != 0; }
};

/**
 * AsyncTransferManager - Non-blocking GPU transfer system.
 *
 * Implements the async transfer pattern from the video:
 * 1. Copy data to staging buffer
 * 2. Submit transfer command with fence (non-blocking)
 * 3. Poll fence each frame via processPendingTransfers()
 * 4. When transfer completes, perform queue ownership transfer if needed
 * 5. Execute completion callback
 *
 * Key design points:
 * - Uses dedicated transfer queue when available (per video recommendation)
 * - Fence-based synchronization (not timeline semaphores for compatibility)
 * - Staging buffer pooling for reduced allocation overhead
 * - Supports both buffer and image transfers
 */
class AsyncTransferManager {
public:
    using CompletionCallback = std::function<void()>;

    AsyncTransferManager() = default;
    ~AsyncTransferManager();

    // Non-copyable
    AsyncTransferManager(const AsyncTransferManager&) = delete;
    AsyncTransferManager& operator=(const AsyncTransferManager&) = delete;

    /**
     * Initialize the transfer manager.
     * @param context Vulkan context (provides device, queues, allocator)
     */
    bool initialize(VulkanContext& context);

    /**
     * Shutdown and wait for all pending transfers.
     */
    void shutdown();

    /**
     * Submit a buffer transfer (CPU to GPU).
     * @param data Source data pointer
     * @param size Size in bytes
     * @param dstBuffer Destination GPU buffer
     * @param dstOffset Offset into destination buffer
     * @param onComplete Optional callback when transfer completes
     * @return Handle to track transfer completion
     */
    TransferHandle submitBufferTransfer(
        const void* data, vk::DeviceSize size,
        vk::Buffer dstBuffer, vk::DeviceSize dstOffset = 0,
        CompletionCallback onComplete = nullptr);

    /**
     * Submit an image transfer (CPU to GPU).
     * Handles layout transitions: undefined -> transferDstOptimal -> finalLayout
     * @param data Source pixel data
     * @param size Size in bytes
     * @param dstImage Destination GPU image
     * @param extent Image dimensions
     * @param finalLayout Layout after transfer (usually shaderReadOnlyOptimal)
     * @param mipLevels Number of mip levels (1 for no mipmaps)
     * @param layerCount Number of array layers (1 for non-array)
     * @param onComplete Optional callback when transfer completes
     * @return Handle to track transfer completion
     */
    TransferHandle submitImageTransfer(
        const void* data, vk::DeviceSize size,
        vk::Image dstImage, vk::Extent3D extent,
        vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        uint32_t mipLevels = 1,
        uint32_t layerCount = 1,
        CompletionCallback onComplete = nullptr);

    /**
     * Check if a specific transfer is complete.
     */
    bool isComplete(TransferHandle handle) const;

    /**
     * Block until a specific transfer completes.
     */
    void wait(TransferHandle handle);

    /**
     * Poll and process completed transfers.
     * Call once per frame from the main/render thread.
     * Executes completion callbacks and releases staging resources.
     */
    void processPendingTransfers();

    /**
     * Wait for all pending transfers to complete.
     * Useful before shutdown or when resources must be ready.
     */
    void waitAll();

    /**
     * Get count of pending transfers.
     */
    size_t getPendingCount() const;

private:
    struct PendingTransfer {
        uint64_t id;
        vk::raii::Fence fence{nullptr};
        vk::CommandBuffer cmdBuffer;  // From transfer pool
        VmaBuffer stagingBuffer;
        CompletionCallback onComplete;
        bool needsOwnershipTransfer = false;
        vk::Image targetImage;  // For queue ownership transfer
        vk::ImageLayout finalLayout;
    };

    // Allocate command buffer from transfer pool
    vk::CommandBuffer allocateTransferCommandBuffer();

    // Free command buffer back to pool
    void freeTransferCommandBuffer(vk::CommandBuffer cmd);

    // Get or create staging buffer of at least the given size
    VmaBuffer acquireStagingBuffer(vk::DeviceSize size);

    // Return staging buffer to pool for reuse
    void releaseStagingBuffer(VmaBuffer buffer);

    std::optional<std::reference_wrapper<VulkanContext>> context_;
    vk::Device device_;
    vk::Queue transferQueue_;
    uint32_t transferQueueFamily_ = 0;
    uint32_t graphicsQueueFamily_ = 0;
    bool hasDedicatedTransfer_ = false;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    // Command pool for transfer operations (created per transfer queue family)
    std::optional<vk::raii::CommandPool> transferCommandPool_;

    // Pending transfers
    std::deque<PendingTransfer> pendingTransfers_;
    mutable std::mutex transferMutex_;

    // Transfer ID counter
    uint64_t nextTransferId_ = 1;

    // Staging buffer pool (for reuse)
    std::vector<VmaBuffer> stagingBufferPool_;
    std::mutex stagingMutex_;
    static constexpr size_t MAX_STAGING_POOL_SIZE = 8;

    bool initialized_ = false;
};
