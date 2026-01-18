#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace BufferUtils {

// Single buffer for one-shot allocations (e.g., staging buffers, one-time uniform buffers)
struct SingleBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mappedPointer = nullptr;

    bool isValid() const { return buffer != VK_NULL_HANDLE; }
};

// Builder for single one-shot buffers (staging, one-time uniforms, etc.)
class SingleBufferBuilder {
public:
    SingleBufferBuilder& setAllocator(VmaAllocator allocator);
    SingleBufferBuilder& setSize(VkDeviceSize size);
    SingleBufferBuilder& setUsage(VkBufferUsageFlags usage);
    SingleBufferBuilder& setMemoryUsage(VmaMemoryUsage usage);
    SingleBufferBuilder& setAllocationFlags(VmaAllocationCreateFlags flags);

    bool build(SingleBuffer& outBuffer) const;

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDeviceSize bufferSize_ = 0;
    VkBufferUsageFlags usage_ = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VmaMemoryUsage memoryUsage_ = VMA_MEMORY_USAGE_AUTO;
    VmaAllocationCreateFlags allocationFlags_ =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
};

void destroyBuffer(VmaAllocator allocator, SingleBuffer& buffer);

}  // namespace BufferUtils
