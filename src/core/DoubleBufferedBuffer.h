#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <vector>

namespace BufferUtils {

struct DoubleBufferedBufferSet {
    std::vector<VkBuffer> buffers;
    std::vector<VmaAllocation> allocations;
};

struct DoubleBufferedBufferConfig {
    const VmaAllocator allocator;
    const uint32_t setCount;
    const VkDeviceSize size;
    const VkBufferUsageFlags usage;
    const VmaMemoryUsage memoryUsage;
    const VmaAllocationCreateFlags allocationFlags;

    DoubleBufferedBufferConfig(
        VmaAllocator allocator = VK_NULL_HANDLE,
        uint32_t setCount = 2,
        VkDeviceSize size = 0,
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO,
        VmaAllocationCreateFlags allocationFlags = 0);
};

class DoubleBufferedBufferBuilder {
public:
    static DoubleBufferedBufferBuilder fromConfig(const DoubleBufferedBufferConfig& config);

    DoubleBufferedBufferBuilder withAllocator(VmaAllocator allocator) const;
    DoubleBufferedBufferBuilder withSetCount(uint32_t count) const;
    DoubleBufferedBufferBuilder withSize(VkDeviceSize size) const;
    DoubleBufferedBufferBuilder withUsage(VkBufferUsageFlags usage) const;
    DoubleBufferedBufferBuilder withMemoryUsage(VmaMemoryUsage usage) const;
    DoubleBufferedBufferBuilder withAllocationFlags(VmaAllocationCreateFlags flags) const;

    DoubleBufferedBufferBuilder& setAllocator(VmaAllocator allocator);
    DoubleBufferedBufferBuilder& setSetCount(uint32_t count);
    DoubleBufferedBufferBuilder& setSize(VkDeviceSize size);
    DoubleBufferedBufferBuilder& setUsage(VkBufferUsageFlags usage);
    DoubleBufferedBufferBuilder& setMemoryUsage(VmaMemoryUsage usage);
    DoubleBufferedBufferBuilder& setAllocationFlags(VmaAllocationCreateFlags flags);

    bool build(DoubleBufferedBufferSet& outBuffers) const;

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    uint32_t setCount_ = 2;
    VkDeviceSize bufferSize_ = 0;
    VkBufferUsageFlags usage_ = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VmaMemoryUsage memoryUsage_ = VMA_MEMORY_USAGE_AUTO;
    VmaAllocationCreateFlags allocationFlags_ = 0;
};

void destroyBuffers(VmaAllocator allocator, const DoubleBufferedBufferSet& buffers);

}  // namespace BufferUtils
