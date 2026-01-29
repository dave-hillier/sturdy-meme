#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <cstdint>

namespace BufferUtils {

// Dynamic uniform buffer: single buffer with aligned offsets for each frame
// Use with VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC to avoid per-frame descriptor updates
struct DynamicUniformBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mappedPointer = nullptr;
    VkDeviceSize alignedSize = 0;    // Size of each frame's data (aligned)
    VkDeviceSize elementSize = 0;    // Original unaligned size
    uint32_t frameCount = 0;

    bool isValid() const { return buffer != VK_NULL_HANDLE; }

    // Get dynamic offset for a specific frame
    uint32_t getDynamicOffset(uint32_t frameIndex) const {
        return static_cast<uint32_t>(alignedSize * frameIndex);
    }

    // Get pointer to a specific frame's data for writing
    void* getMappedPtr(uint32_t frameIndex) const {
        if (!mappedPointer) return nullptr;
        return static_cast<char*>(mappedPointer) + alignedSize * frameIndex;
    }

    // Total buffer size
    VkDeviceSize getTotalSize() const { return alignedSize * frameCount; }
};

// Multi-slot dynamic uniform buffer: single buffer with aligned offsets for multiple
// slots per frame. Use for per-object data like bone matrices where each character
// needs its own slot in the buffer, selected via dynamic offset at draw time.
struct MultiSlotDynamicBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mappedPointer = nullptr;
    VkDeviceSize alignedSlotSize = 0;  // Size of each slot (aligned to minUniformBufferOffsetAlignment)
    VkDeviceSize elementSize = 0;       // Original unaligned element size
    uint32_t slotsPerFrame = 0;         // Number of slots per frame (e.g., max characters)
    uint32_t frameCount = 0;

    bool isValid() const { return buffer != VK_NULL_HANDLE; }

    // Get dynamic offset for a specific frame and slot
    uint32_t getDynamicOffset(uint32_t frameIndex, uint32_t slotIndex) const {
        return static_cast<uint32_t>(alignedSlotSize * (frameIndex * slotsPerFrame + slotIndex));
    }

    // Get pointer to a specific frame/slot for writing
    void* getMappedPtr(uint32_t frameIndex, uint32_t slotIndex) const {
        if (!mappedPointer) return nullptr;
        return static_cast<char*>(mappedPointer) + alignedSlotSize * (frameIndex * slotsPerFrame + slotIndex);
    }

    // Total buffer size
    VkDeviceSize getTotalSize() const { return alignedSlotSize * slotsPerFrame * frameCount; }

    // Get aligned slot size for descriptor writes
    VkDeviceSize getAlignedSlotSize() const { return alignedSlotSize; }
};

// Builder for dynamic uniform buffers (single buffer with aligned per-frame data)
// Use with VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
class DynamicUniformBufferBuilder {
public:
    DynamicUniformBufferBuilder& setAllocator(VmaAllocator allocator);
    DynamicUniformBufferBuilder& setPhysicalDevice(VkPhysicalDevice physicalDevice);
    DynamicUniformBufferBuilder& setFrameCount(uint32_t count);
    DynamicUniformBufferBuilder& setElementSize(VkDeviceSize size);

    bool build(DynamicUniformBuffer& outBuffer) const;

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    uint32_t frameCount_ = 0;
    VkDeviceSize elementSize_ = 0;
};

// Builder for multi-slot dynamic uniform buffers
// Use with VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC for per-object data
class MultiSlotDynamicBufferBuilder {
public:
    MultiSlotDynamicBufferBuilder& setAllocator(VmaAllocator allocator);
    MultiSlotDynamicBufferBuilder& setPhysicalDevice(VkPhysicalDevice physicalDevice);
    MultiSlotDynamicBufferBuilder& setFrameCount(uint32_t count);
    MultiSlotDynamicBufferBuilder& setSlotsPerFrame(uint32_t count);
    MultiSlotDynamicBufferBuilder& setElementSize(VkDeviceSize size);

    bool build(MultiSlotDynamicBuffer& outBuffer) const;

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    uint32_t frameCount_ = 0;
    uint32_t slotsPerFrame_ = 0;
    VkDeviceSize elementSize_ = 0;
};

void destroyBuffer(VmaAllocator allocator, DynamicUniformBuffer& buffer);
void destroyBuffer(VmaAllocator allocator, MultiSlotDynamicBuffer& buffer);

}  // namespace BufferUtils
