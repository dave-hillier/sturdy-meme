#include "DynamicUniformBuffer.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>

namespace BufferUtils {

DynamicUniformBufferBuilder& DynamicUniformBufferBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator_ = newAllocator;
    return *this;
}

DynamicUniformBufferBuilder& DynamicUniformBufferBuilder::setPhysicalDevice(VkPhysicalDevice device) {
    physicalDevice_ = device;
    return *this;
}

DynamicUniformBufferBuilder& DynamicUniformBufferBuilder::setFrameCount(uint32_t count) {
    frameCount_ = count;
    return *this;
}

DynamicUniformBufferBuilder& DynamicUniformBufferBuilder::setElementSize(VkDeviceSize size) {
    elementSize_ = size;
    return *this;
}

bool DynamicUniformBufferBuilder::build(DynamicUniformBuffer& outBuffer) const {
    if (!allocator_ || !physicalDevice_ || frameCount_ == 0 || elementSize_ == 0) {
        SDL_Log("DynamicUniformBufferBuilder missing required fields");
        return false;
    }

    // Get minimum uniform buffer offset alignment
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    VkDeviceSize minAlignment = props.limits.minUniformBufferOffsetAlignment;

    // Calculate aligned size (round up to alignment)
    VkDeviceSize alignedSize = (elementSize_ + minAlignment - 1) & ~(minAlignment - 1);
    VkDeviceSize totalSize = alignedSize * frameCount_;

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(totalSize)
        .setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    DynamicUniformBuffer result{};
    VmaAllocationInfo allocationInfo{};

    if (vmaCreateBuffer(allocator_, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo, &result.buffer, &result.allocation,
                        &allocationInfo) != VK_SUCCESS) {
        SDL_Log("Failed to create dynamic uniform buffer");
        return false;
    }

    result.mappedPointer = allocationInfo.pMappedData;
    result.alignedSize = alignedSize;
    result.elementSize = elementSize_;
    result.frameCount = frameCount_;

    outBuffer = result;
    return true;
}

void destroyBuffer(VmaAllocator allocator, DynamicUniformBuffer& buffer) {
    if (!allocator) return;
    if (buffer.buffer != VK_NULL_HANDLE && buffer.allocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

}  // namespace BufferUtils
