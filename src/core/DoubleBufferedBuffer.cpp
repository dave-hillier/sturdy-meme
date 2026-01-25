#include "DoubleBufferedBuffer.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>

namespace BufferUtils {
namespace {

void destroyCreatedBuffers(VmaAllocator allocator,
                           const std::vector<VkBuffer>& buffers,
                           const std::vector<VmaAllocation>& allocations,
                           size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (buffers[i] != VK_NULL_HANDLE && allocations[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffers[i], allocations[i]);
        }
    }
}

}  // namespace

DoubleBufferedBufferConfig::DoubleBufferedBufferConfig(
    VmaAllocator allocator,
    uint32_t setCount,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage,
    VmaAllocationCreateFlags allocationFlags)
    : allocator(allocator),
      setCount(setCount),
      size(size),
      usage(usage),
      memoryUsage(memoryUsage),
      allocationFlags(allocationFlags) {}

DoubleBufferedBufferBuilder DoubleBufferedBufferBuilder::fromConfig(const DoubleBufferedBufferConfig& config) {
    DoubleBufferedBufferBuilder builder;
    builder.allocator_ = config.allocator;
    builder.setCount_ = config.setCount;
    builder.bufferSize_ = config.size;
    builder.usage_ = config.usage;
    builder.memoryUsage_ = config.memoryUsage;
    builder.allocationFlags_ = config.allocationFlags;
    return builder;
}

DoubleBufferedBufferBuilder DoubleBufferedBufferBuilder::withAllocator(VmaAllocator allocator) const {
    auto builder = *this;
    builder.allocator_ = allocator;
    return builder;
}

DoubleBufferedBufferBuilder DoubleBufferedBufferBuilder::withSetCount(uint32_t count) const {
    auto builder = *this;
    builder.setCount_ = count;
    return builder;
}

DoubleBufferedBufferBuilder DoubleBufferedBufferBuilder::withSize(VkDeviceSize size) const {
    auto builder = *this;
    builder.bufferSize_ = size;
    return builder;
}

DoubleBufferedBufferBuilder DoubleBufferedBufferBuilder::withUsage(VkBufferUsageFlags usage) const {
    auto builder = *this;
    builder.usage_ = usage;
    return builder;
}

DoubleBufferedBufferBuilder DoubleBufferedBufferBuilder::withMemoryUsage(VmaMemoryUsage usage) const {
    auto builder = *this;
    builder.memoryUsage_ = usage;
    return builder;
}

DoubleBufferedBufferBuilder DoubleBufferedBufferBuilder::withAllocationFlags(VmaAllocationCreateFlags flags) const {
    auto builder = *this;
    builder.allocationFlags_ = flags;
    return builder;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator_ = newAllocator;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setSetCount(uint32_t count) {
    setCount_ = count;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setSize(VkDeviceSize size) {
    bufferSize_ = size;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setUsage(VkBufferUsageFlags newUsage) {
    usage_ = newUsage;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setMemoryUsage(VmaMemoryUsage newUsage) {
    memoryUsage_ = newUsage;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setAllocationFlags(VmaAllocationCreateFlags flags) {
    allocationFlags_ = flags;
    return *this;
}

bool DoubleBufferedBufferBuilder::build(DoubleBufferedBufferSet& outBuffers) const {
    if (!allocator_ || setCount_ == 0 || bufferSize_ == 0 || usage_ == 0) {
        SDL_Log("DoubleBufferedBufferBuilder missing required fields (allocator=%p, setCount=%u, size=%zu, usage=%u)",
                allocator_, setCount_, static_cast<size_t>(bufferSize_), usage_);
        return false;
    }

    DoubleBufferedBufferSet result{};
    result.buffers.resize(setCount_, VK_NULL_HANDLE);
    result.allocations.resize(setCount_, VK_NULL_HANDLE);

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(bufferSize_)
        .setUsage(static_cast<vk::BufferUsageFlags>(usage_))
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage_;
    allocInfo.flags = allocationFlags_;

    for (uint32_t i = 0; i < setCount_; i++) {
        if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo, &result.buffers[i], &result.allocations[i], nullptr) !=
            VK_SUCCESS) {
            SDL_Log("Failed to create double-buffered buffer %u", i);
            destroyCreatedBuffers(allocator_, result.buffers, result.allocations, i);
            return false;
        }
    }

    outBuffers = std::move(result);
    return true;
}

void destroyBuffers(VmaAllocator allocator, const DoubleBufferedBufferSet& buffers) {
    if (!allocator) return;
    for (size_t i = 0; i < buffers.buffers.size(); i++) {
        if (buffers.buffers[i] != VK_NULL_HANDLE && buffers.allocations[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffers.buffers[i], buffers.allocations[i]);
        }
    }
}

}  // namespace BufferUtils
