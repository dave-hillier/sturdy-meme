#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <vector>

namespace BufferUtils {

struct DoubleBufferedBufferSet {
    std::vector<VkBuffer> buffers;
    std::vector<VmaAllocation> allocations;
};

class DoubleBufferedBufferBuilder {
public:
    DoubleBufferedBufferBuilder& setAllocator(VmaAllocator allocator);
    DoubleBufferedBufferBuilder& setSetCount(uint32_t count);
    DoubleBufferedBufferBuilder& setSize(VkDeviceSize size);
    DoubleBufferedBufferBuilder& setUsage(VkBufferUsageFlags usage);
    DoubleBufferedBufferBuilder& setMemoryUsage(VmaMemoryUsage usage);

    bool build(DoubleBufferedBufferSet& outBuffers) const;

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    uint32_t setCount_ = 2;
    VkDeviceSize bufferSize_ = 0;
    VkBufferUsageFlags usage_ = 0;
    VmaMemoryUsage memoryUsage_ = VMA_MEMORY_USAGE_AUTO;
};

void destroyBuffers(VmaAllocator allocator, const DoubleBufferedBufferSet& buffers);

}  // namespace BufferUtils
