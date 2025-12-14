#pragma once

#include "VirtualTextureTypes.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <unordered_set>

namespace VirtualTexture {

/**
 * GPU feedback buffer for virtual texture tile requests.
 *
 * The shader writes requested tile IDs to this buffer during rendering.
 * After each frame, the CPU reads back the buffer to determine which
 * tiles need to be loaded.
 *
 * Uses double/triple buffering to avoid GPU/CPU synchronization issues.
 */
class VirtualTextureFeedback {
public:
    VirtualTextureFeedback() = default;
    ~VirtualTextureFeedback() = default;

    // Non-copyable
    VirtualTextureFeedback(const VirtualTextureFeedback&) = delete;
    VirtualTextureFeedback& operator=(const VirtualTextureFeedback&) = delete;

    /**
     * Initialize the feedback system
     * @param device Vulkan device
     * @param allocator VMA allocator
     * @param maxEntries Maximum number of tile requests per frame
     * @param frameCount Number of frames in flight (for buffering)
     * @return true on success
     */
    bool init(VkDevice device, VmaAllocator allocator,
              uint32_t maxEntries = 4096, uint32_t frameCount = 2);

    /**
     * Destroy all resources
     */
    void destroy(VkDevice device, VmaAllocator allocator);

    /**
     * Clear the feedback buffer for a new frame
     * Should be called at the start of each frame before rendering
     */
    void clear(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * Read back tile requests from a completed frame
     * Should be called after the frame has finished rendering
     * @param frameIndex The frame index that has completed
     */
    void readback(uint32_t frameIndex);

    /**
     * Get the list of unique requested tile IDs from the last readback
     * Tiles are deduplicated and sorted by priority (lower mip = higher priority)
     */
    std::vector<TileId> getRequestedTiles() const;

    /**
     * Get the feedback buffer for shader binding
     */
    VkBuffer getFeedbackBuffer(uint32_t frameIndex) const;

    /**
     * Get the counter buffer (atomic counter for number of requests)
     */
    VkBuffer getCounterBuffer(uint32_t frameIndex) const;

    /**
     * Get buffer descriptor info for shader binding
     */
    VkDescriptorBufferInfo getDescriptorInfo(uint32_t frameIndex) const;
    VkDescriptorBufferInfo getCounterDescriptorInfo(uint32_t frameIndex) const;

    uint32_t getMaxEntries() const { return maxEntries; }

private:
    struct FrameBuffer {
        VkBuffer feedbackBuffer = VK_NULL_HANDLE;
        VmaAllocation feedbackAllocation = VK_NULL_HANDLE;

        VkBuffer counterBuffer = VK_NULL_HANDLE;
        VmaAllocation counterAllocation = VK_NULL_HANDLE;

        VkBuffer readbackBuffer = VK_NULL_HANDLE;
        VmaAllocation readbackAllocation = VK_NULL_HANDLE;
        void* readbackMapped = nullptr;

        VkBuffer counterReadbackBuffer = VK_NULL_HANDLE;
        VmaAllocation counterReadbackAllocation = VK_NULL_HANDLE;
        void* counterReadbackMapped = nullptr;
    };

    std::vector<FrameBuffer> frameBuffers;
    uint32_t maxEntries = 4096;

    // Results from last readback
    std::unordered_set<uint32_t> requestedTilePacked;
    std::vector<TileId> requestedTilesSorted;

    bool createFrameBuffer(VkDevice device, VmaAllocator allocator, FrameBuffer& fb);
    void destroyFrameBuffer(VkDevice device, VmaAllocator allocator, FrameBuffer& fb);
};

} // namespace VirtualTexture
