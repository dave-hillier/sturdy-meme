#pragma once

#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

namespace BufferUtils {

struct PerFrameBufferSet {
    std::vector<VkBuffer> buffers;
    std::vector<VmaAllocation> allocations;
    std::vector<void*> mappedPointers;
};

struct DoubleBufferedBufferSet {
    std::vector<VkBuffer> buffers;
    std::vector<VmaAllocation> allocations;
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
    VmaAllocator allocator = VK_NULL_HANDLE;
    uint32_t frameCount = 0;
    VkDeviceSize bufferSize = 0;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO;
    VmaAllocationCreateFlags allocationFlags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
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
    VmaAllocator allocator = VK_NULL_HANDLE;
    uint32_t setCount = 2;
    VkDeviceSize bufferSize = 0;
    VkBufferUsageFlags usage = 0;
    VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO;
};

void destroyBuffers(VmaAllocator allocator, const PerFrameBufferSet& buffers);
void destroyBuffers(VmaAllocator allocator, const DoubleBufferedBufferSet& buffers);

}  // namespace BufferUtils

