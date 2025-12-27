#pragma once

#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace BufferUtils {

// IMPORTANT: When using multiple buffer sets for compute/render ping-pong patterns,
// the buffer set count MUST match the frames-in-flight count. Using fewer buffer sets
// (e.g., 2 sets with 3 frames in flight) causes frame N and frame N+2 to share buffers,
// leading to race conditions where GPU may still be reading from a buffer while CPU writes.
//
// Use TripleBufferedBufferSet for systems that need per-frame isolation with 3 frames in flight.
// The buffer set count should always equal MAX_FRAMES_IN_FLIGHT from Renderer.h.

// Single buffer for one-shot allocations (e.g., staging buffers, one-time uniform buffers)
struct SingleBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mappedPointer = nullptr;

    bool isValid() const { return buffer != VK_NULL_HANDLE; }
};

struct PerFrameBufferSet {
    std::vector<VkBuffer> buffers;
    std::vector<VmaAllocation> allocations;
    std::vector<void*> mappedPointers;
};

struct DoubleBufferedBufferSet {
    std::vector<VkBuffer> buffers;
    std::vector<VmaAllocation> allocations;
};

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

// Double-buffered images for ping-pong rendering (temporal effects, SSR, etc.)
struct DoubleBufferedImageSet {
    VkImage images[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView views[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation allocations[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    bool isValid() const { return images[0] != VK_NULL_HANDLE && images[1] != VK_NULL_HANDLE; }
};

// Tracks which buffer is for reading vs writing in a ping-pong scheme
class PingPongTracker {
public:
    void advance() { std::swap(writeIndex, readIndex); }
    void reset() { writeIndex = 0; readIndex = 1; }

    uint32_t getWriteIndex() const { return writeIndex; }
    uint32_t getReadIndex() const { return readIndex; }

private:
    uint32_t writeIndex = 0;
    uint32_t readIndex = 1;
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
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDeviceSize bufferSize = 0;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO;
    VmaAllocationCreateFlags allocationFlags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
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
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t frameCount = 0;
    VkDeviceSize elementSize = 0;
};

// Builder for double-buffered images (ping-pong for temporal effects)
class DoubleBufferedImageBuilder {
public:
    DoubleBufferedImageBuilder& setDevice(VkDevice device);
    DoubleBufferedImageBuilder& setAllocator(VmaAllocator allocator);
    DoubleBufferedImageBuilder& setExtent(uint32_t width, uint32_t height);
    DoubleBufferedImageBuilder& setExtent3D(uint32_t width, uint32_t height, uint32_t depth);
    DoubleBufferedImageBuilder& setFormat(VkFormat format);
    DoubleBufferedImageBuilder& setUsage(VkImageUsageFlags usage);
    DoubleBufferedImageBuilder& setAspectMask(VkImageAspectFlags aspect);

    bool build(DoubleBufferedImageSet& outImages) const;

private:
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
};

void destroyBuffer(VmaAllocator allocator, SingleBuffer& buffer);
void destroyBuffer(VmaAllocator allocator, DynamicUniformBuffer& buffer);
void destroyBuffers(VmaAllocator allocator, const PerFrameBufferSet& buffers);
void destroyBuffers(VmaAllocator allocator, const DoubleBufferedBufferSet& buffers);
void destroyImages(VkDevice device, VmaAllocator allocator, DoubleBufferedImageSet& images);

}  // namespace BufferUtils

