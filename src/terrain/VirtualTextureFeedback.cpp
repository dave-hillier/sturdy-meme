#include "VirtualTextureFeedback.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstring>

namespace VirtualTexture {

bool VirtualTextureFeedback::init(VkDevice device, VmaAllocator allocator,
                                   uint32_t entries, uint32_t frameCount) {
    maxEntries = entries;
    frameBuffers.resize(frameCount);

    for (auto& fb : frameBuffers) {
        if (!createFrameBuffer(device, allocator, fb)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT feedback frame buffer");
            return false;
        }
    }

    SDL_Log("VirtualTextureFeedback initialized: %u entries, %u frames", maxEntries, frameCount);
    return true;
}

void VirtualTextureFeedback::destroy(VkDevice device, VmaAllocator allocator) {
    for (auto& fb : frameBuffers) {
        destroyFrameBuffer(device, allocator, fb);
    }
    frameBuffers.clear();
    requestedTilePacked.clear();
    requestedTilesSorted.clear();
}

bool VirtualTextureFeedback::createFrameBuffer(VkDevice device, VmaAllocator allocator,
                                                FrameBuffer& fb) {
    VkDeviceSize feedbackSize = maxEntries * sizeof(uint32_t);
    VkDeviceSize counterSize = sizeof(uint32_t);

    // GPU feedback buffer (storage buffer, written by shader)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = feedbackSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                           &fb.feedbackBuffer, &fb.feedbackAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }
    }

    // GPU counter buffer (atomic counter for number of requests)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = counterSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                           &fb.counterBuffer, &fb.counterAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }
    }

    // CPU readback buffer for feedback
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = feedbackSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                           &fb.readbackBuffer, &fb.readbackAllocation, &allocationInfo) != VK_SUCCESS) {
            return false;
        }
        fb.readbackMapped = allocationInfo.pMappedData;
    }

    // CPU readback buffer for counter
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = counterSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                           &fb.counterReadbackBuffer, &fb.counterReadbackAllocation,
                           &allocationInfo) != VK_SUCCESS) {
            return false;
        }
        fb.counterReadbackMapped = allocationInfo.pMappedData;
    }

    return true;
}

void VirtualTextureFeedback::destroyFrameBuffer(VkDevice device, VmaAllocator allocator,
                                                  FrameBuffer& fb) {
    if (fb.feedbackBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, fb.feedbackBuffer, fb.feedbackAllocation);
        fb.feedbackBuffer = VK_NULL_HANDLE;
    }
    if (fb.counterBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, fb.counterBuffer, fb.counterAllocation);
        fb.counterBuffer = VK_NULL_HANDLE;
    }
    if (fb.readbackBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, fb.readbackBuffer, fb.readbackAllocation);
        fb.readbackBuffer = VK_NULL_HANDLE;
        fb.readbackMapped = nullptr;
    }
    if (fb.counterReadbackBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, fb.counterReadbackBuffer, fb.counterReadbackAllocation);
        fb.counterReadbackBuffer = VK_NULL_HANDLE;
        fb.counterReadbackMapped = nullptr;
    }
}

void VirtualTextureFeedback::clear(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (frameIndex >= frameBuffers.size()) return;

    FrameBuffer& fb = frameBuffers[frameIndex];

    // Clear counter to 0 and barrier for fragment shader
    Barriers::clearBufferForFragment(cmd, fb.counterBuffer);
}

void VirtualTextureFeedback::readback(uint32_t frameIndex) {
    if (frameIndex >= frameBuffers.size()) return;

    FrameBuffer& fb = frameBuffers[frameIndex];

    // Read the counter to know how many entries were written
    uint32_t count = 0;
    if (fb.counterReadbackMapped) {
        std::memcpy(&count, fb.counterReadbackMapped, sizeof(uint32_t));
    }

    // Clamp to max entries
    count = std::min(count, maxEntries);

    // Clear previous results
    requestedTilePacked.clear();
    requestedTilesSorted.clear();

    if (count == 0 || !fb.readbackMapped) {
        return;
    }

    // Read tile IDs and deduplicate
    const uint32_t* tileIds = static_cast<const uint32_t*>(fb.readbackMapped);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t packed = tileIds[i];
        if (packed != 0) { // 0 might be invalid/empty
            requestedTilePacked.insert(packed);
        }
    }

    // Convert to TileId and sort by priority (lower mip first)
    requestedTilesSorted.reserve(requestedTilePacked.size());
    for (uint32_t packed : requestedTilePacked) {
        requestedTilesSorted.push_back(TileId::unpack(packed));
    }

    // Sort by mip level (lower mip = larger tiles = higher priority)
    std::sort(requestedTilesSorted.begin(), requestedTilesSorted.end(),
              [](const TileId& a, const TileId& b) {
                  return a.mipLevel < b.mipLevel;
              });
}

std::vector<TileId> VirtualTextureFeedback::getRequestedTiles() const {
    return requestedTilesSorted;
}

VkBuffer VirtualTextureFeedback::getFeedbackBuffer(uint32_t frameIndex) const {
    if (frameIndex >= frameBuffers.size()) return VK_NULL_HANDLE;
    return frameBuffers[frameIndex].feedbackBuffer;
}

VkBuffer VirtualTextureFeedback::getCounterBuffer(uint32_t frameIndex) const {
    if (frameIndex >= frameBuffers.size()) return VK_NULL_HANDLE;
    return frameBuffers[frameIndex].counterBuffer;
}

VkDescriptorBufferInfo VirtualTextureFeedback::getDescriptorInfo(uint32_t frameIndex) const {
    VkDescriptorBufferInfo info{};
    if (frameIndex < frameBuffers.size()) {
        info.buffer = frameBuffers[frameIndex].feedbackBuffer;
        info.offset = 0;
        info.range = maxEntries * sizeof(uint32_t);
    }
    return info;
}

VkDescriptorBufferInfo VirtualTextureFeedback::getCounterDescriptorInfo(uint32_t frameIndex) const {
    VkDescriptorBufferInfo info{};
    if (frameIndex < frameBuffers.size()) {
        info.buffer = frameBuffers[frameIndex].counterBuffer;
        info.offset = 0;
        info.range = sizeof(uint32_t);
    }
    return info;
}

} // namespace VirtualTexture
