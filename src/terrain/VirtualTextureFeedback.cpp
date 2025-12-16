#include "VirtualTextureFeedback.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstring>

namespace VirtualTexture {

bool VirtualTextureFeedback::init(VkDevice device, VmaAllocator allocator,
                                   uint32_t entries, uint32_t frameCount) {
    maxEntries = entries;
    frameBuffers.resize(frameCount);

    for (auto& fb : frameBuffers) {
        if (!createFrameBuffer(allocator, fb)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT feedback frame buffer");
            return false;
        }
    }

    SDL_Log("VirtualTextureFeedback initialized: %u entries, %u frames", maxEntries, frameCount);
    return true;
}

void VirtualTextureFeedback::destroy() {
    // RAII handles buffer cleanup automatically when frameBuffers is cleared
    for (auto& fb : frameBuffers) {
        fb.readbackMapped = nullptr;
        fb.counterReadbackMapped = nullptr;
    }
    frameBuffers.clear();
    requestedTilePacked.clear();
    requestedTilesSorted.clear();
}

bool VirtualTextureFeedback::createFrameBuffer(VmaAllocator allocator, FrameBuffer& fb) {
    VkDeviceSize feedbackSize = maxEntries * sizeof(uint32_t);
    VkDeviceSize counterSize = sizeof(uint32_t);

    // GPU feedback buffer (storage buffer, written by shader)
    if (!VulkanResourceFactory::createStorageBuffer(allocator, feedbackSize, fb.feedbackBuffer)) {
        return false;
    }

    // GPU counter buffer (atomic counter for number of requests)
    if (!VulkanResourceFactory::createStorageBuffer(allocator, counterSize, fb.counterBuffer)) {
        return false;
    }

    // CPU readback buffer for feedback
    if (!VulkanResourceFactory::createReadbackBuffer(allocator, feedbackSize, fb.readbackBuffer)) {
        return false;
    }
    fb.readbackMapped = fb.readbackBuffer.map();

    // CPU readback buffer for counter
    if (!VulkanResourceFactory::createReadbackBuffer(allocator, counterSize, fb.counterReadbackBuffer)) {
        return false;
    }
    fb.counterReadbackMapped = fb.counterReadbackBuffer.map();

    return true;
}

void VirtualTextureFeedback::clear(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (frameIndex >= frameBuffers.size()) return;

    FrameBuffer& fb = frameBuffers[frameIndex];

    // Clear counter to 0 and barrier for fragment shader
    Barriers::clearBufferForFragment(cmd, fb.counterBuffer.get());
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
    return frameBuffers[frameIndex].feedbackBuffer.get();
}

VkBuffer VirtualTextureFeedback::getCounterBuffer(uint32_t frameIndex) const {
    if (frameIndex >= frameBuffers.size()) return VK_NULL_HANDLE;
    return frameBuffers[frameIndex].counterBuffer.get();
}

VkDescriptorBufferInfo VirtualTextureFeedback::getDescriptorInfo(uint32_t frameIndex) const {
    VkDescriptorBufferInfo info{};
    if (frameIndex < frameBuffers.size()) {
        info.buffer = frameBuffers[frameIndex].feedbackBuffer.get();
        info.offset = 0;
        info.range = maxEntries * sizeof(uint32_t);
    }
    return info;
}

VkDescriptorBufferInfo VirtualTextureFeedback::getCounterDescriptorInfo(uint32_t frameIndex) const {
    VkDescriptorBufferInfo info{};
    if (frameIndex < frameBuffers.size()) {
        info.buffer = frameBuffers[frameIndex].counterBuffer.get();
        info.offset = 0;
        info.range = sizeof(uint32_t);
    }
    return info;
}

} // namespace VirtualTexture
