#pragma once

#include "VirtualTextureTypes.h"
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>
#include "VmaResources.h"

namespace VirtualTexture {

/**
 * VirtualTextureCache manages the physical tile cache texture.
 *
 * The cache can use either RGBA8 or BC1 compressed format.
 * BC1 uses 4x less GPU memory but requires all tiles to be BC1 compressed.
 * It uses LRU eviction when the cache is full.
 */
class VirtualTextureCache {
public:
    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VirtualTextureConfig config;
        uint32_t framesInFlight = 2;
        bool useCompression = false;
    };

    // Factory method - returns nullptr on failure
    static std::unique_ptr<VirtualTextureCache> create(const InitInfo& info);

    ~VirtualTextureCache();

    // Move-only
    VirtualTextureCache(VirtualTextureCache&& other) noexcept;
    VirtualTextureCache& operator=(VirtualTextureCache&& other) noexcept;
    VirtualTextureCache(const VirtualTextureCache&) = delete;
    VirtualTextureCache& operator=(const VirtualTextureCache&) = delete;

    // Allocate a slot for a new tile, evicting LRU if needed
    // Returns the cache slot coordinates, or nullptr if allocation failed
    CacheSlot* allocateSlot(TileId id, uint32_t currentFrame);

    // Mark a tile as used this frame (for LRU tracking)
    void markUsed(TileId id, uint32_t currentFrame);

    // Check if a tile is in the cache
    bool hasTile(TileId id) const;

    // Get the cache slot for a tile (nullptr if not in cache)
    const CacheSlot* getSlot(TileId id) const;

    /**
     * Record tile upload commands into the provided command buffer.
     * Uses fence-based synchronization - caller is responsible for submitting
     * the command buffer and waiting on the appropriate frame fence.
     *
     * @param id Tile ID to upload
     * @param pixelData Pixel data to upload (RGBA8 or BC1 compressed)
     * @param width Tile width
     * @param height Tile height
     * @param format Tile format (RGBA8 or BC1)
     * @param cmd Command buffer to record into (must be in recording state)
     * @param frameIndex Current frame index for staging buffer selection
     */
    void recordTileUpload(TileId id, const void* pixelData, uint32_t width, uint32_t height,
                          TileFormat format, VkCommandBuffer cmd, uint32_t frameIndex);

    // Check if the cache is using compressed BC1 format
    bool isCompressed() const { return useCompression_; }

    /**
     * Get the number of staging buffers (one per frame in flight)
     */
    uint32_t getStagingBufferCount() const { return static_cast<uint32_t>(stagingBuffers_.size()); }

    // Get the cache texture image view
    VkImageView getCacheImageView() const { return cacheImageView; }

    // Get the sampler for the cache texture
    VkSampler getCacheSampler() const { return cacheSampler_ ? **cacheSampler_ : VK_NULL_HANDLE; }

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
    VirtualTextureCache() = default;
    bool initInternal(const InitInfo& info);

    // Find LRU slot for eviction
    size_t findLRUSlot() const;

    // Create the cache texture
    bool createCacheTexture(VkDevice device, VmaAllocator allocator,
                            VkCommandPool commandPool, VkQueue queue);

    // Create sampler for the cache
    bool createSampler(VkDevice device);

    // Stored for cleanup
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VirtualTextureConfig config;
    bool useCompression_ = false;  // BC1 compressed cache
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Physical cache texture
    VkImage cacheImage = VK_NULL_HANDLE;
    VmaAllocation cacheAllocation = VK_NULL_HANDLE;
    VkImageView cacheImageView = VK_NULL_HANDLE;
    std::optional<vk::raii::Sampler> cacheSampler_;

    // Per-frame staging buffers to avoid race conditions with in-flight frames
    std::vector<VmaBuffer> stagingBuffers_;
    std::vector<void*> stagingMapped_;
    uint32_t framesInFlight_ = 2;

    // Cache slot management
    std::vector<CacheSlot> slots;
    std::unordered_map<uint32_t, size_t> tileToSlot;  // TileId::pack() -> slot index
};

} // namespace VirtualTexture
