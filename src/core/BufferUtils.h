#pragma once

#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <cassert>
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
    ~FrameIndexedBuffers() { destroy(); }

    // Non-copyable (contains Vulkan resources)
    FrameIndexedBuffers(const FrameIndexedBuffers&) = delete;
    FrameIndexedBuffers& operator=(const FrameIndexedBuffers&) = delete;

    // Movable
    FrameIndexedBuffers(FrameIndexedBuffers&& other) noexcept
        : buffers_(std::move(other.buffers_))
        , allocations_(std::move(other.allocations_))
        , frameCount_(other.frameCount_)
        , allocator_(other.allocator_) {
        other.frameCount_ = 0;
        other.allocator_ = VK_NULL_HANDLE;
    }

    FrameIndexedBuffers& operator=(FrameIndexedBuffers&& other) noexcept {
        if (this != &other) {
            destroy();
            buffers_ = std::move(other.buffers_);
            allocations_ = std::move(other.allocations_);
            frameCount_ = other.frameCount_;
            allocator_ = other.allocator_;
            other.frameCount_ = 0;
            other.allocator_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    // Allocate buffers for each frame
    bool resize(VmaAllocator allocator, uint32_t frameCount, vk::DeviceSize size,
                vk::BufferUsageFlags usage,
                VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY) {
        destroy();

        if (!allocator || frameCount == 0 || size == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "FrameIndexedBuffers::resize: invalid params (allocator=%p, frames=%u, size=%zu)",
                allocator, frameCount, static_cast<size_t>(size));
            return false;
        }

        allocator_ = allocator;
        frameCount_ = frameCount;
        buffers_.resize(frameCount);
        allocations_.resize(frameCount);

        auto bufferInfo = vk::BufferCreateInfo{}
            .setSize(size)
            .setUsage(usage)
            .setSharingMode(vk::SharingMode::eExclusive);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = memoryUsage;

        for (uint32_t i = 0; i < frameCount; ++i) {
            VkBuffer rawBuffer;
            VkResult result = vmaCreateBuffer(
                allocator,
                reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
                &allocInfo,
                &rawBuffer,
                &allocations_[i],
                nullptr);

            if (result != VK_SUCCESS) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "FrameIndexedBuffers::resize: failed to create buffer %u", i);
                destroy();
                return false;
            }
            buffers_[i] = vk::Buffer(rawBuffer);
        }

        return true;
    }

    // Clean up all buffers
    void destroy() {
        if (allocator_) {
            for (uint32_t i = 0; i < buffers_.size(); ++i) {
                if (buffers_[i]) {
                    vmaDestroyBuffer(allocator_, static_cast<VkBuffer>(buffers_[i]), allocations_[i]);
                }
            }
        }
        buffers_.clear();
        allocations_.clear();
        frameCount_ = 0;
    }

    // =========================================================================
    // SAFE ACCESS - Must provide frameIndex
    // =========================================================================

    // Get buffer for a specific frame (primary access method)
    vk::Buffer get(uint32_t frameIndex) const {
        if (buffers_.empty()) return vk::Buffer{};
        return buffers_[frameIndex % frameCount_];
    }

    // Get raw VkBuffer for APIs that need it
    VkBuffer getVk(uint32_t frameIndex) const {
        return static_cast<VkBuffer>(get(frameIndex));
    }

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

// =============================================================================
// FrameIndexedDescriptorSets - Type-safe per-frame descriptor set management
// =============================================================================
//
// Similar to FrameIndexedBuffers, this enforces correct frame-indexed access
// for descriptor sets. Descriptor sets are NOT owned by this class - they are
// allocated from a DescriptorManager::Pool and managed there.
//
// Usage:
//   FrameIndexedDescriptorSets sets;
//   sets.resize(pool->allocate(layout, frameCount));  // allocate from pool
//
//   // In recordCompute(frameIndex):
//   VkDescriptorSet set = sets.get(frameIndex);
//
//   // In recordDraw(frameIndex):
//   VkDescriptorSet set = sets.get(frameIndex);  // Same set - guaranteed!
//
class FrameIndexedDescriptorSets {
public:
    FrameIndexedDescriptorSets() = default;

    // Populate from a vector of allocated descriptor sets
    void resize(const std::vector<VkDescriptorSet>& sets) {
        sets_ = sets;
        frameCount_ = static_cast<uint32_t>(sets.size());
    }

    // Move semantics for initialization from rvalue
    void resize(std::vector<VkDescriptorSet>&& sets) {
        sets_ = std::move(sets);
        frameCount_ = static_cast<uint32_t>(sets_.size());
    }

    // Get descriptor set for a specific frame (primary access method)
    VkDescriptorSet get(uint32_t frameIndex) const {
        if (sets_.empty()) return VK_NULL_HANDLE;
        return sets_[frameIndex % frameCount_];
    }

    // Get vk::DescriptorSet for vulkan-hpp APIs
    vk::DescriptorSet getVk(uint32_t frameIndex) const {
        return vk::DescriptorSet(get(frameIndex));
    }

    // Set a specific descriptor set (for manual updates)
    void set(uint32_t index, VkDescriptorSet descriptorSet) {
        if (index < sets_.size()) {
            sets_[index] = descriptorSet;
        }
    }

    bool empty() const { return sets_.empty(); }
    uint32_t size() const { return frameCount_; }

    // For descriptor set initialization where you need direct access
    VkDescriptorSet operator[](uint32_t index) const {
        assert(index < frameCount_ && "Index out of bounds");
        return sets_[index];
    }

    // Access underlying vector for bulk operations
    const std::vector<VkDescriptorSet>& data() const { return sets_; }
    std::vector<VkDescriptorSet>& data() { return sets_; }

    // Direct iteration for bulk updates
    auto begin() const { return sets_.begin(); }
    auto end() const { return sets_.end(); }

private:
    std::vector<VkDescriptorSet> sets_;
    uint32_t frameCount_ = 0;
};

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

