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

void destroyBuffer(VmaAllocator allocator, DynamicUniformBuffer& buffer);

}  // namespace BufferUtils
