#include "SingleBuffer.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>

namespace BufferUtils {

SingleBufferBuilder& SingleBufferBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator_ = newAllocator;
    return *this;
}

SingleBufferBuilder& SingleBufferBuilder::setSize(VkDeviceSize size) {
    bufferSize_ = size;
    return *this;
}

SingleBufferBuilder& SingleBufferBuilder::setUsage(VkBufferUsageFlags newUsage) {
    usage_ = newUsage;
    return *this;
}

SingleBufferBuilder& SingleBufferBuilder::setMemoryUsage(VmaMemoryUsage newUsage) {
    memoryUsage_ = newUsage;
    return *this;
}

SingleBufferBuilder& SingleBufferBuilder::setAllocationFlags(VmaAllocationCreateFlags flags) {
    allocationFlags_ = flags;
    return *this;
}

bool SingleBufferBuilder::build(SingleBuffer& outBuffer) const {
    if (!allocator_ || bufferSize_ == 0) {
        SDL_Log("SingleBufferBuilder missing required fields (allocator=%p, size=%zu)",
                allocator_, static_cast<size_t>(bufferSize_));
        return false;
    }

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(bufferSize_)
        .setUsage(static_cast<vk::BufferUsageFlags>(usage_))
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage_;
    allocInfo.flags = allocationFlags_;

    SingleBuffer result{};
    VmaAllocationInfo allocationInfo{};

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo, &result.buffer, &result.allocation,
                        &allocationInfo) != VK_SUCCESS) {
        SDL_Log("Failed to create single buffer");
        return false;
    }

    result.mappedPointer = allocationInfo.pMappedData;
    outBuffer = result;
    return true;
}

void destroyBuffer(VmaAllocator allocator, SingleBuffer& buffer) {
    if (!allocator) return;
    if (buffer.buffer != VK_NULL_HANDLE && buffer.allocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

}  // namespace BufferUtils
