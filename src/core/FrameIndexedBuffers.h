#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <cassert>
#include <cstdint>
#include <vector>

namespace BufferUtils {

// IMPORTANT: When using multiple buffer sets for compute/render ping-pong patterns,
// the buffer set count MUST match the frames-in-flight count. Using fewer buffer sets
// (e.g., 2 sets with 3 frames in flight) causes frame N and frame N+2 to share buffers,
// leading to race conditions where GPU may still be reading from a buffer while CPU writes.
//
// Use TripleBufferedBufferSet for systems that need per-frame isolation with 3 frames in flight.
// The buffer set count should always equal MAX_FRAMES_IN_FLIGHT from Renderer.h.

// =============================================================================
// FrameIndexedBuffers - Type-safe per-frame buffer management
// =============================================================================
//
// This template enforces correct frame-indexed buffer access, preventing the common
// bug where a separate counter (like currentBufferSet_) gets out of sync with frameIndex,
// causing compute and graphics passes to use different buffers.
//
// Key design principles:
// - No parameterless getters: You MUST provide frameIndex to access a buffer
// - No separate counter needed: Buffer selection is always based on frameIndex
// - Compile-time safety: Can't accidentally use the wrong index
//
// Usage:
//   FrameIndexedBuffers buffers;
//   buffers.resize(allocator, frameCount, bufferSize, usage);
//
//   // In recordCulling(frameIndex):
//   vk::Buffer buffer = buffers.get(frameIndex);
//
//   // In render(frameIndex):
//   vk::Buffer buffer = buffers.get(frameIndex);  // Same buffer - guaranteed!
//
// Migration from std::vector<VkBuffer> + currentBufferSet_:
//   Before (buggy):
//     std::vector<VkBuffer> buffers_;
//     uint32_t currentBufferSet_ = 0;
//     VkBuffer getBuffer() { return buffers_[currentBufferSet_]; }  // Can desync!
//     void swap() { currentBufferSet_ = (currentBufferSet_ + 1) % 3; }
//
//   After (safe):
//     FrameIndexedBuffers buffers_;
//     vk::Buffer getBuffer(uint32_t frameIndex) { return buffers_.get(frameIndex); }
//     // No swap() needed - frameIndex is the source of truth
//
class FrameIndexedBuffers {
public:
    FrameIndexedBuffers() = default;
    ~FrameIndexedBuffers();

    // Non-copyable (contains Vulkan resources)
    FrameIndexedBuffers(const FrameIndexedBuffers&) = delete;
    FrameIndexedBuffers& operator=(const FrameIndexedBuffers&) = delete;

    // Movable
    FrameIndexedBuffers(FrameIndexedBuffers&& other) noexcept;
    FrameIndexedBuffers& operator=(FrameIndexedBuffers&& other) noexcept;

    // Allocate buffers for each frame
    bool resize(VmaAllocator allocator, uint32_t frameCount, vk::DeviceSize size,
                vk::BufferUsageFlags usage,
                VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY);

    // Clean up all buffers
    void destroy();

    // =========================================================================
    // SAFE ACCESS - Must provide frameIndex
    // =========================================================================

    // Get buffer for a specific frame (primary access method)
    vk::Buffer get(uint32_t frameIndex) const;

    // Get raw VkBuffer for APIs that need it
    VkBuffer getVk(uint32_t frameIndex) const;

    // =========================================================================
    // Utility methods
    // =========================================================================

    bool empty() const { return buffers_.empty(); }
    uint32_t size() const { return frameCount_; }

    // For descriptor set initialization where you need to bind all frames
    vk::Buffer operator[](uint32_t index) const {
        assert(index < frameCount_ && "Index out of bounds");
        return buffers_[index];
    }

    // Direct iteration for cleanup/initialization
    auto begin() const { return buffers_.begin(); }
    auto end() const { return buffers_.end(); }

private:
    std::vector<vk::Buffer> buffers_;
    std::vector<VmaAllocation> allocations_;
    uint32_t frameCount_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
};

}  // namespace BufferUtils
