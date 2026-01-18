#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <vector>

namespace BufferUtils {

struct PerFrameBufferSet {
    std::vector<VkBuffer> buffers;
    std::vector<VmaAllocation> allocations;
    std::vector<void*> mappedPointers;
};

class PerFrameBufferBuilder {
public:
    PerFrameBufferBuilder& setAllocator(VmaAllocator allocator);
    PerFrameBufferBuilder& setFrameCount(uint32_t count);
    PerFrameBufferBuilder& setSize(VkDeviceSize size);
    PerFrameBufferBuilder& setUsage(VkBufferUsageFlags usage);
    PerFrameBufferBuilder& setMemoryUsage(VmaMemoryUsage usage);
    PerFrameBufferBuilder& setAllocationFlags(VmaAllocationCreateFlags flags);

    bool build(PerFrameBufferSet& outBuffers) const;

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    uint32_t frameCount_ = 0;
    VkDeviceSize bufferSize_ = 0;
    VkBufferUsageFlags usage_ = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VmaMemoryUsage memoryUsage_ = VMA_MEMORY_USAGE_AUTO;
    VmaAllocationCreateFlags allocationFlags_ =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
};

void destroyBuffers(VmaAllocator allocator, const PerFrameBufferSet& buffers);

}  // namespace BufferUtils
