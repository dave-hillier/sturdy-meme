#pragma once

#include "VirtualTextureTypes.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <unordered_map>

namespace VirtualTexture {

/**
 * VirtualTextureCache manages the physical tile cache texture.
 *
 * The cache is a large RGBA8 texture that holds the currently loaded tiles.
 * It uses LRU eviction when the cache is full.
 */
class VirtualTextureCache {
public:
    VirtualTextureCache() = default;
    ~VirtualTextureCache() = default;

    // Non-copyable
    VirtualTextureCache(const VirtualTextureCache&) = delete;
    VirtualTextureCache& operator=(const VirtualTextureCache&) = delete;

    // Initialize the cache
    bool init(VkDevice device, VmaAllocator allocator, VkCommandPool commandPool,
              VkQueue queue, const VirtualTextureConfig& config);

    // Cleanup resources
    void destroy(VkDevice device, VmaAllocator allocator);

    // Allocate a slot for a new tile, evicting LRU if needed
    // Returns the cache slot coordinates, or nullptr if allocation failed
    CacheSlot* allocateSlot(TileId id, uint32_t currentFrame);

    // Mark a tile as used this frame (for LRU tracking)
    void markUsed(TileId id, uint32_t currentFrame);

    // Check if a tile is in the cache
    bool hasTile(TileId id) const;

    // Get the cache slot for a tile (nullptr if not in cache)
    const CacheSlot* getSlot(TileId id) const;

    // Upload tile data to the cache
    void uploadTile(TileId id, const void* pixelData, uint32_t width, uint32_t height,
                    VkDevice device, VkCommandPool commandPool, VkQueue queue);

    // Get the cache texture image view
    VkImageView getCacheImageView() const { return cacheImageView; }

    // Get the sampler for the cache texture
    VkSampler getCacheSampler() const { return cacheSampler; }

    // Get the slot index for a tile (UINT32_MAX if not found)
    uint32_t getTileSlotIndex(TileId id) const {
        auto it = tileToSlot.find(id.pack());
        if (it != tileToSlot.end()) {
            return static_cast<uint32_t>(it->second);
        }
        return UINT32_MAX;
    }

    // Get statistics
    uint32_t getSlotCount() const { return static_cast<uint32_t>(slots.size()); }
    uint32_t getUsedSlotCount() const;

private:
    // Find LRU slot for eviction
    size_t findLRUSlot() const;

    // Create the cache texture
    bool createCacheTexture(VkDevice device, VmaAllocator allocator,
                            VkCommandPool commandPool, VkQueue queue);

    // Create sampler for the cache
    bool createSampler(VkDevice device);

    VirtualTextureConfig config;

    // Physical cache texture
    VkImage cacheImage = VK_NULL_HANDLE;
    VmaAllocation cacheAllocation = VK_NULL_HANDLE;
    VkImageView cacheImageView = VK_NULL_HANDLE;
    VkSampler cacheSampler = VK_NULL_HANDLE;

    // Staging buffer for uploads
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    void* stagingMapped = nullptr;

    // Cache slot management
    std::vector<CacheSlot> slots;
    std::unordered_map<uint32_t, size_t> tileToSlot;  // TileId::pack() -> slot index
};

} // namespace VirtualTexture
