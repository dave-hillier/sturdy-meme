#pragma once

#include "VirtualTextureTypes.h"
#include "VirtualTextureCache.h"
#include "VirtualTexturePageTable.h"
#include "VirtualTextureFeedback.h"
#include "VirtualTextureTileLoader.h"
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <optional>

namespace VirtualTexture {

/**
 * Main virtual texture system orchestrator.
 *
 * Coordinates the cache, page table, feedback, and tile loader components
 * to implement a complete virtual texturing pipeline:
 *
 * 1. Feedback Analysis: Reads GPU feedback to determine needed tiles
 * 2. Tile Loading: Queues missing tiles for async loading
 * 3. Cache Management: Uploads loaded tiles and evicts old ones
 * 4. Page Table Update: Updates indirection textures when tiles change
 *
 * Usage:
 *   - Call beginFrame() at start of frame
 *   - Bind VT descriptors to terrain shader
 *   - Render terrain (shader writes to feedback buffer)
 *   - Call endFrame() after rendering
 *   - Call update() to process feedback and load tiles
 */
class VirtualTextureSystem {
public:
    VirtualTextureSystem() = default;
    ~VirtualTextureSystem() = default;

    // Non-copyable
    VirtualTextureSystem(const VirtualTextureSystem&) = delete;
    VirtualTextureSystem& operator=(const VirtualTextureSystem&) = delete;

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        std::string tilePath;
        VirtualTextureConfig config;
        uint32_t framesInFlight = 2;
    };

    /**
     * Initialize the virtual texture system
     * @return true on success
     */
    bool init(const InitInfo& info);

    /**
     * Destroy all resources
     */
    void destroy(VkDevice device, VmaAllocator allocator);

    /**
     * Begin a new frame - clears feedback buffer
     * @param cmd Command buffer to record clear commands
     * @param frameIndex Current frame index (for double buffering)
     */
    void beginFrame(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * End frame - copy feedback to readback buffer.
     * Records GPU copy commands from feedback storage buffers to CPU-readable readback buffers.
     * @param cmd Command buffer to record copy commands
     * @param frameIndex Current frame index
     */
    void endFrame(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * Process feedback from a PREVIOUS frame and record tile uploads.
     * This should be called at the START of a new frame, processing feedback from
     * (currentFrame - framesInFlight) to ensure proper synchronization.
     *
     * The method:
     * 1. Reads back feedback from a completed frame (N-2 with double buffering)
     * 2. Queues tiles for async loading based on feedback
     * 3. Records tile upload commands for tiles that finished loading
     * 4. Records page table upload commands if dirty
     *
     * @param cmd Command buffer to record upload commands into
     * @param frameIndex Current frame index (NOT the frame being read back)
     */
    void update(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * Get the physical cache texture for shader binding
     */
    VkImageView getCacheImageView() const { return cache->getCacheImageView(); }
    VkSampler getCacheSampler() const { return cache->getCacheSampler(); }

    /**
     * Get page table textures for shader binding
     */
    VkImageView getPageTableImageView(uint32_t mipLevel) const {
        return pageTable->getImageView(mipLevel);
    }
    VkSampler getPageTableSampler() const { return pageTable->getSampler(); }

    /**
     * Get feedback buffer for shader binding
     */
    VkBuffer getFeedbackBuffer(uint32_t frameIndex) const {
        return feedback->getFeedbackBuffer(frameIndex);
    }
    VkBuffer getCounterBuffer(uint32_t frameIndex) const {
        return feedback->getCounterBuffer(frameIndex);
    }

    /**
     * Get UBO data for shader binding
     */
    VTParamsUBO getParams() const;

    /**
     * Get configuration
     */
    const VirtualTextureConfig& getConfig() const { return config; }

    /**
     * Get statistics
     */
    uint32_t getCacheUsedSlots() const { return cache->getUsedSlotCount(); }
    uint32_t getPendingTileCount() const { return tileLoader->getPendingCount(); }
    uint32_t getLoadedTileCount() const { return tileLoader->getLoadedCount(); }
    uint64_t getTotalBytesLoaded() const { return tileLoader->getTotalBytesLoaded(); }
    float getCurrentPenalty() const { return currentPenalty; }
    uint32_t getTotalCacheSlots() const { return config.getTotalCacheSlots(); }

    /**
     * Force load a specific tile (for debugging/testing)
     */
    void requestTile(TileId id);

    /**
     * Check if a tile is resident in cache
     */
    bool isTileResident(TileId id) const { return cache->hasTile(id); }

private:
    VirtualTextureConfig config;

    // RAII-managed subsystems
    std::unique_ptr<VirtualTextureCache> cache;
    std::unique_ptr<VirtualTexturePageTable> pageTable;
    std::unique_ptr<VirtualTextureFeedback> feedback;
    std::unique_ptr<VirtualTextureTileLoader> tileLoader;

    uint32_t currentFrame = 0;
    uint32_t framesInFlight_ = 2;
    std::unordered_set<uint32_t> pendingTiles; // Tiles currently being loaded

    // Over-budget penalty scheme (Ghost of Tsushima style)
    // When cache is under pressure, we increase the penalty to request coarser mips
    float currentPenalty = 0.0f;
    static constexpr float PENALTY_INCREMENT = 0.5f;  // Half a mip level per iteration
    static constexpr float PENALTY_RELAX_RATE = 0.1f; // Relax penalty when stable
    static constexpr float MAX_PENALTY = 4.0f;        // Max 4 mip levels of degradation

    // Maximum tiles to upload per frame (to limit stalls)
    static constexpr uint32_t MAX_UPLOADS_PER_FRAME = 16;
    // Maximum tile requests to queue per frame
    static constexpr uint32_t MAX_REQUESTS_PER_FRAME = 64;

    void processFeedback(uint32_t readbackFrameIndex);
    void recordPendingTileUploads(VkCommandBuffer cmd, uint32_t frameIndex);
};

} // namespace VirtualTexture
