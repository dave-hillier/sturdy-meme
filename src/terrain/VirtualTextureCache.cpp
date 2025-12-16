#include "VirtualTextureCache.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstring>

namespace VirtualTexture {

bool VirtualTextureCache::init(VkDevice device, VmaAllocator allocator,
                                VkCommandPool commandPool, VkQueue queue,
                                const VirtualTextureConfig& cfg) {
    config = cfg;

    // Initialize slot array
    uint32_t totalSlots = config.getTotalCacheSlots();
    slots.resize(totalSlots);

    uint32_t slotsPerAxis = config.getCacheTilesPerAxis();
    for (uint32_t i = 0; i < totalSlots; ++i) {
        slots[i].occupied = false;
        slots[i].lastUsedFrame = 0;
    }

    // Create the cache texture
    if (!createCacheTexture(device, allocator, commandPool, queue)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT cache texture");
        return false;
    }

    // Create sampler
    if (!createSampler(device)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT cache sampler");
        return false;
    }

    // Create staging buffer for tile uploads
    VkDeviceSize stagingSize = config.tileSizePixels * config.tileSizePixels * 4; // RGBA8

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = stagingSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocationInfo;
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer,
                        &stagingAllocation, &allocationInfo) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VT staging buffer");
        return false;
    }
    stagingMapped = allocationInfo.pMappedData;

    SDL_Log("VirtualTextureCache initialized: %u slots (%ux%u tiles), %upx cache",
            totalSlots, slotsPerAxis, slotsPerAxis, config.cacheSizePixels);

    return true;
}

void VirtualTextureCache::destroy(VkDevice device, VmaAllocator allocator) {
    if (stagingBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        stagingBuffer = VK_NULL_HANDLE;
        stagingAllocation = VK_NULL_HANDLE;
        stagingMapped = nullptr;
    }

    cacheSampler.destroy();

    if (cacheImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, cacheImageView, nullptr);
        cacheImageView = VK_NULL_HANDLE;
    }

    if (cacheImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, cacheImage, cacheAllocation);
        cacheImage = VK_NULL_HANDLE;
        cacheAllocation = VK_NULL_HANDLE;
    }

    slots.clear();
    tileToSlot.clear();
}

bool VirtualTextureCache::createCacheTexture(VkDevice device, VmaAllocator allocator,
                                              VkCommandPool commandPool, VkQueue queue) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent.width = config.cacheSizePixels;
    imageInfo.extent.height = config.cacheSizePixels;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &cacheImage,
                       &cacheAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = cacheImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &cacheImageView) != VK_SUCCESS) {
        return false;
    }

    // Transition to shader read layout
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    Barriers::transitionImage(cmd, cacheImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, VK_ACCESS_SHADER_READ_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);

    return true;
}

bool VirtualTextureCache::createSampler(VkDevice device) {
    return ManagedSampler::createLinearClamp(device, cacheSampler);
}

CacheSlot* VirtualTextureCache::allocateSlot(TileId id, uint32_t currentFrame) {
    uint32_t packed = id.pack();

    // Check if already in cache
    auto it = tileToSlot.find(packed);
    if (it != tileToSlot.end()) {
        slots[it->second].lastUsedFrame = currentFrame;
        return &slots[it->second];
    }

    // Find an empty slot first
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].occupied) {
            slots[i].occupied = true;
            slots[i].tileId = id;
            slots[i].lastUsedFrame = currentFrame;
            tileToSlot[packed] = i;
            return &slots[i];
        }
    }

    // No empty slots, evict LRU
    size_t lruIndex = findLRUSlot();
    if (lruIndex < slots.size()) {
        // Remove old mapping
        uint32_t oldPacked = slots[lruIndex].tileId.pack();
        tileToSlot.erase(oldPacked);

        // Set new tile
        slots[lruIndex].tileId = id;
        slots[lruIndex].lastUsedFrame = currentFrame;
        tileToSlot[packed] = lruIndex;
        return &slots[lruIndex];
    }

    return nullptr;
}

void VirtualTextureCache::markUsed(TileId id, uint32_t currentFrame) {
    auto it = tileToSlot.find(id.pack());
    if (it != tileToSlot.end()) {
        slots[it->second].lastUsedFrame = currentFrame;
    }
}

bool VirtualTextureCache::hasTile(TileId id) const {
    return tileToSlot.find(id.pack()) != tileToSlot.end();
}

const CacheSlot* VirtualTextureCache::getSlot(TileId id) const {
    auto it = tileToSlot.find(id.pack());
    if (it != tileToSlot.end()) {
        return &slots[it->second];
    }
    return nullptr;
}

size_t VirtualTextureCache::findLRUSlot() const {
    size_t lruIndex = 0;
    uint32_t oldestFrame = UINT32_MAX;

    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].occupied && slots[i].lastUsedFrame < oldestFrame) {
            oldestFrame = slots[i].lastUsedFrame;
            lruIndex = i;
        }
    }

    return lruIndex;
}

void VirtualTextureCache::uploadTile(TileId id, const void* pixelData,
                                      uint32_t width, uint32_t height,
                                      VkDevice device, VkCommandPool commandPool,
                                      VkQueue queue) {
    // Find the slot for this tile
    auto it = tileToSlot.find(id.pack());
    if (it == tileToSlot.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot upload tile not in cache");
        return;
    }

    size_t slotIndex = it->second;
    uint32_t slotsPerAxis = config.getCacheTilesPerAxis();
    uint32_t slotX = slotIndex % slotsPerAxis;
    uint32_t slotY = slotIndex / slotsPerAxis;

    // Copy to staging buffer
    VkDeviceSize dataSize = width * height * 4;
    std::memcpy(stagingMapped, pixelData, dataSize);

    // Record copy command
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition to transfer dst
    Barriers::transitionImage(cmd, cacheImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

    // Copy buffer to image region at tile slot position
    Barriers::copyBufferToImageRegion(cmd, stagingBuffer, cacheImage,
        static_cast<int32_t>(slotX * config.tileSizePixels),
        static_cast<int32_t>(slotY * config.tileSizePixels),
        width, height);

    // Transition back to shader read
    Barriers::imageTransferToSampling(cmd, cacheImage);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

uint32_t VirtualTextureCache::getUsedSlotCount() const {
    uint32_t count = 0;
    for (const auto& slot : slots) {
        if (slot.occupied) ++count;
    }
    return count;
}

} // namespace VirtualTexture
