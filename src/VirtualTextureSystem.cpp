#include "VirtualTextureSystem.h"
#include <SDL3/SDL_log.h>
#include <algorithm>

namespace VirtualTexture {

bool VirtualTextureSystem::init(VkDevice device, VmaAllocator allocator,
                                 VkCommandPool commandPool, VkQueue queue,
                                 const std::string& tilePath,
                                 const VirtualTextureConfig& cfg) {
    config = cfg;

    SDL_Log("Initializing VirtualTextureSystem...");
    SDL_Log("  Virtual size: %u px", config.virtualSizePixels);
    SDL_Log("  Tile size: %u px", config.tileSizePixels);
    SDL_Log("  Cache size: %u px", config.cacheSizePixels);
    SDL_Log("  Max mip levels: %u", config.maxMipLevels);

    // Initialize cache
    if (!cache.init(device, allocator, commandPool, queue, config)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VT cache");
        return false;
    }

    // Initialize page table
    if (!pageTable.init(device, allocator, commandPool, queue, config)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VT page table");
        return false;
    }

    // Initialize feedback
    if (!feedback.init(device, allocator, 4096, 2)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VT feedback");
        return false;
    }

    // Initialize tile loader
    if (!tileLoader.init(tilePath, 2)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VT tile loader");
        return false;
    }

    SDL_Log("VirtualTextureSystem initialized successfully");
    return true;
}

void VirtualTextureSystem::destroy(VkDevice device, VmaAllocator allocator) {
    tileLoader.shutdown();
    feedback.destroy(device, allocator);
    pageTable.destroy(device, allocator);
    cache.destroy(device, allocator);
    pendingTiles.clear();
}

void VirtualTextureSystem::beginFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Clear the feedback buffer for this frame
    feedback.clear(cmd, frameIndex);
}

void VirtualTextureSystem::endFrame(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Copy feedback buffer to readback buffer
    // This happens at end of frame so the copy is after all rendering

    VkBuffer srcBuffer = feedback.getFeedbackBuffer(frameIndex);
    VkBuffer counterSrc = feedback.getCounterBuffer(frameIndex);

    // We need to access the internal readback buffers - for now, the readback
    // happens synchronously in update() after waiting for the frame
    // A more efficient approach would be to copy to readback buffers here

    // Memory barrier to ensure shader writes are visible
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VirtualTextureSystem::update(VkDevice device, VkCommandPool commandPool,
                                   VkQueue queue, uint32_t frameIndex) {
    currentFrame++;

    // Process feedback from the completed frame
    processFeedback(frameIndex);

    // Upload any tiles that finished loading
    uploadPendingTiles(device, commandPool, queue);

    // Upload any dirty page table entries
    pageTable.upload(device, commandPool, queue);
}

void VirtualTextureSystem::processFeedback(uint32_t frameIndex) {
    // Read back tile requests from GPU
    feedback.readback(frameIndex);

    // Get deduplicated, sorted list of requested tiles
    std::vector<TileId> requested = feedback.getRequestedTiles();

    if (requested.empty()) {
        return;
    }

    uint32_t queued = 0;
    for (const auto& id : requested) {
        if (queued >= MAX_REQUESTS_PER_FRAME) {
            break;
        }

        uint32_t packed = id.pack();

        // Skip if already in cache
        if (cache.hasTile(id)) {
            cache.markUsed(id, currentFrame);
            continue;
        }

        // Skip if already pending
        if (pendingTiles.find(packed) != pendingTiles.end()) {
            continue;
        }

        // Skip if already queued for loading
        if (tileLoader.isQueued(id)) {
            continue;
        }

        // Queue for loading with priority based on mip level
        // Lower mip = larger tile = higher priority
        int priority = static_cast<int>(id.mipLevel);
        tileLoader.queueTile(id, priority);
        pendingTiles.insert(packed);
        queued++;
    }

    if (queued > 0) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "VT: Queued %u new tile requests", queued);
    }
}

void VirtualTextureSystem::uploadPendingTiles(VkDevice device, VkCommandPool commandPool,
                                               VkQueue queue) {
    // Get tiles that finished loading
    std::vector<LoadedTile> loaded = tileLoader.getLoadedTiles();

    if (loaded.empty()) {
        return;
    }

    uint32_t uploaded = 0;
    for (auto& tile : loaded) {
        if (uploaded >= MAX_UPLOADS_PER_FRAME) {
            // Re-queue remaining tiles for next frame
            // In a real implementation, we'd keep them in a local buffer
            break;
        }

        // Allocate cache slot
        CacheSlot* slot = cache.allocateSlot(tile.id, currentFrame);
        if (!slot) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "VT: Failed to allocate cache slot for tile");
            continue;
        }

        // Upload tile data to cache
        cache.uploadTile(tile.id, tile.pixels.data(),
                         tile.width, tile.height,
                         device, commandPool, queue);

        // Update page table
        uint32_t slotsPerAxis = config.getCacheTilesPerAxis();
        uint32_t packed = tile.id.pack();
        auto slotIt = cache.getTileSlotIndex(tile.id);

        if (slotIt != UINT32_MAX) {
            uint16_t cacheX = slotIt % slotsPerAxis;
            uint16_t cacheY = slotIt / slotsPerAxis;
            pageTable.setEntry(tile.id, cacheX, cacheY);
        }

        // Remove from pending set
        pendingTiles.erase(packed);
        uploaded++;
    }

    if (uploaded > 0) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "VT: Uploaded %u tiles to cache", uploaded);
    }
}

VTParamsUBO VirtualTextureSystem::getParams() const {
    VTParamsUBO params{};

    float virtSize = static_cast<float>(config.virtualSizePixels);
    float cacheSize = static_cast<float>(config.cacheSizePixels);
    float tileSize = static_cast<float>(config.tileSizePixels);
    float border = static_cast<float>(config.borderPixels);

    // xy = size, zw = 1/size
    params.virtualTextureSizeAndInverse = glm::vec4(
        virtSize, virtSize, 1.0f / virtSize, 1.0f / virtSize
    );

    // xy = size, zw = 1/size
    params.physicalCacheSizeAndInverse = glm::vec4(
        cacheSize, cacheSize, 1.0f / cacheSize, 1.0f / cacheSize
    );

    // x = tile size, y = border, z = tile with border, w = unused
    params.tileSizeAndBorder = glm::vec4(
        tileSize, border, tileSize + border * 2.0f, 0.0f
    );

    params.maxMipLevel = config.maxMipLevels - 1;

    return params;
}

void VirtualTextureSystem::requestTile(TileId id) {
    if (!cache.hasTile(id) && !tileLoader.isQueued(id)) {
        tileLoader.queueTile(id, 0); // High priority
        pendingTiles.insert(id.pack());
    }
}

} // namespace VirtualTexture
