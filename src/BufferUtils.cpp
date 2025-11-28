#include "BufferUtils.h"

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
    allocator = newAllocator;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setFrameCount(uint32_t count) {
    frameCount = count;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setSize(VkDeviceSize size) {
    bufferSize = size;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setUsage(VkBufferUsageFlags newUsage) {
    usage = newUsage;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setMemoryUsage(VmaMemoryUsage newUsage) {
    memoryUsage = newUsage;
    return *this;
}

PerFrameBufferBuilder& PerFrameBufferBuilder::setAllocationFlags(VmaAllocationCreateFlags flags) {
    allocationFlags = flags;
    return *this;
}

bool PerFrameBufferBuilder::build(PerFrameBufferSet& outBuffers) const {
    if (!allocator || frameCount == 0 || bufferSize == 0) {
        SDL_Log("PerFrameBufferBuilder missing required fields (allocator=%p, frameCount=%u, size=%zu)",
                allocator, frameCount, static_cast<size_t>(bufferSize));
        return false;
    }

    PerFrameBufferSet result{};
    result.buffers.resize(frameCount, VK_NULL_HANDLE);
    result.allocations.resize(frameCount, VK_NULL_HANDLE);
    result.mappedPointers.resize(frameCount, nullptr);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = allocationFlags;

    for (uint32_t i = 0; i < frameCount; i++) {
        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &result.buffers[i], &result.allocations[i],
                            &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create per-frame buffer %u", i);
            destroyCreatedBuffers(allocator, result.buffers, result.allocations, i);
            return false;
        }
        result.mappedPointers[i] = allocationInfo.pMappedData;
    }

    outBuffers = std::move(result);
    return true;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator = newAllocator;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setSetCount(uint32_t count) {
    setCount = count;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setSize(VkDeviceSize size) {
    bufferSize = size;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setUsage(VkBufferUsageFlags newUsage) {
    usage = newUsage;
    return *this;
}

DoubleBufferedBufferBuilder& DoubleBufferedBufferBuilder::setMemoryUsage(VmaMemoryUsage newUsage) {
    memoryUsage = newUsage;
    return *this;
}

bool DoubleBufferedBufferBuilder::build(DoubleBufferedBufferSet& outBuffers) const {
    if (!allocator || setCount == 0 || bufferSize == 0 || usage == 0) {
        SDL_Log("DoubleBufferedBufferBuilder missing required fields (allocator=%p, setCount=%u, size=%zu, usage=%u)",
                allocator, setCount, static_cast<size_t>(bufferSize), usage);
        return false;
    }

    DoubleBufferedBufferSet result{};
    result.buffers.resize(setCount, VK_NULL_HANDLE);
    result.allocations.resize(setCount, VK_NULL_HANDLE);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    for (uint32_t i = 0; i < setCount; i++) {
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &result.buffers[i], &result.allocations[i], nullptr) !=
            VK_SUCCESS) {
            SDL_Log("Failed to create double-buffered buffer %u", i);
            destroyCreatedBuffers(allocator, result.buffers, result.allocations, i);
            return false;
        }
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

void destroyBuffers(VmaAllocator allocator, const DoubleBufferedBufferSet& buffers) {
    if (!allocator) return;
    for (size_t i = 0; i < buffers.buffers.size(); i++) {
        if (buffers.buffers[i] != VK_NULL_HANDLE && buffers.allocations[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffers.buffers[i], buffers.allocations[i]);
        }
    }
}

}  // namespace BufferUtils

