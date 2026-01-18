#include "PerFrameBuffer.h"
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

PerFrameBufferBuilder& PerFrameBufferBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator_ = newAllocator;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setFrameCount(uint32_t count) {
    frameCount_ = count;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setSize(VkDeviceSize size) {
    bufferSize_ = size;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setUsage(VkBufferUsageFlags newUsage) {
    usage_ = newUsage;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setMemoryUsage(VmaMemoryUsage newUsage) {
    memoryUsage_ = newUsage;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setAllocationFlags(VmaAllocationCreateFlags flags) {
    allocationFlags_ = flags;
    return *this;
}

bool PerFrameBufferBuilder::build(PerFrameBufferSet& outBuffers) const {
    if (!allocator_ || frameCount_ == 0 || bufferSize_ == 0) {
        SDL_Log("PerFrameBufferBuilder missing required fields (allocator=%p, frameCount=%u, size=%zu)",
                allocator_, frameCount_, static_cast<size_t>(bufferSize_));
        return false;
    }

    PerFrameBufferSet result{};
    result.buffers.resize(frameCount_, VK_NULL_HANDLE);
    result.allocations.resize(frameCount_, VK_NULL_HANDLE);
    result.mappedPointers.resize(frameCount_, nullptr);

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(bufferSize_)
        .setUsage(static_cast<vk::BufferUsageFlags>(usage_))
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage_;
    allocInfo.flags = allocationFlags_;

    for (uint32_t i = 0; i < frameCount_; i++) {
        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo, &result.buffers[i], &result.allocations[i],
                            &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create per-frame buffer %u", i);
            destroyCreatedBuffers(allocator_, result.buffers, result.allocations, i);
            return false;
        }
        result.mappedPointers[i] = allocationInfo.pMappedData;
    }

    outBuffers = std::move(result);
    return true;
}

void destroyBuffers(VmaAllocator allocator, const PerFrameBufferSet& buffers) {
    if (!allocator) return;
    for (size_t i = 0; i < buffers.buffers.size(); i++) {
        if (buffers.buffers[i] != VK_NULL_HANDLE && buffers.allocations[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffers.buffers[i], buffers.allocations[i]);
        }
    }
}

}  // namespace BufferUtils
