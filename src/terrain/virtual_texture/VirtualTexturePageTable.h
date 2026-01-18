#pragma once

#include "VirtualTextureTypes.h"
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <memory>
#include <optional>
#include "VmaBuffer.h"

namespace VirtualTexture {

/**
 * VirtualTexturePageTable manages the indirection texture (page table).
 *
 * The page table maps virtual tile coordinates to physical cache locations.
 * Each mip level has its own indirection texture of appropriate size.
 * Entries are RGBA8: RG = cache position, B = unused, A = valid flag
 */
class VirtualTexturePageTable {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit VirtualTexturePageTable(ConstructToken) {}

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VirtualTextureConfig config;
        uint32_t framesInFlight = 2;
    };

    /**
     * Factory: Create and initialize VirtualTexturePageTable.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<VirtualTexturePageTable> create(const InitInfo& info);

    ~VirtualTexturePageTable();

    // Non-copyable, non-movable
    VirtualTexturePageTable(const VirtualTexturePageTable&) = delete;
    VirtualTexturePageTable& operator=(const VirtualTexturePageTable&) = delete;
    VirtualTexturePageTable(VirtualTexturePageTable&&) = delete;
    VirtualTexturePageTable& operator=(VirtualTexturePageTable&&) = delete;

    // Update entry when tile is loaded into cache
    void setEntry(TileId id, uint16_t cacheX, uint16_t cacheY);

    // Invalidate entry when tile is evicted
    void clearEntry(TileId id);

    // Get the current entry for a tile
    PageTableEntry getEntry(TileId id) const;

    /**
     * Record page table upload commands into the provided command buffer.
     * Uses fence-based synchronization - caller is responsible for submitting
     * the command buffer and waiting on the appropriate frame fence.
     *
     * @param cmd Command buffer to record into (must be in recording state)
     * @param frameIndex Current frame index for staging buffer selection
     */
    void recordUpload(VkCommandBuffer cmd, uint32_t frameIndex);

    // Check if any entries have changed
    bool isDirty() const { return dirty; }

    // Get the image view for a mip level
    VkImageView getImageView(uint32_t mipLevel) const;

    // Get the sampler for the page table
    VkSampler getSampler() const { return pageTableSampler_ ? **pageTableSampler_ : VK_NULL_HANDLE; }

    // Get the combined image view (array of all mip levels)
    VkImageView getCombinedImageView() const { return combinedImageView; }


private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    // Create page table textures
    bool createPageTableTextures(VkDevice device, VmaAllocator allocator,
                                  VkCommandPool commandPool, VkQueue queue);

    // Create sampler
    bool createSampler(VkDevice device);

    // Get linear index into cpuData for a tile
    size_t getEntryIndex(TileId id) const;

    VirtualTextureConfig config;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // One image per mip level
    std::vector<VkImage> pageTableImages;
    std::vector<VmaAllocation> pageTableAllocations;
    std::vector<VkImageView> pageTableViews;

    // Combined image view (texture array)
    VkImageView combinedImageView = VK_NULL_HANDLE;
    std::optional<vk::raii::Sampler> pageTableSampler_;

    // Per-frame staging buffers to avoid race conditions with in-flight frames
    std::vector<VmaBuffer> stagingBuffers_;
    std::vector<void*> stagingMapped_;
    uint32_t framesInFlight_ = 2;

    // CPU-side page table data (linear array, indexed per mip level)
    std::vector<PageTableEntry> cpuData;
    std::vector<size_t> mipOffsets;  // Offset into cpuData for each mip level
    std::vector<uint32_t> mipSizes;  // Number of entries per mip level

    bool dirty = false;
    std::vector<bool> mipDirty;  // Track which mip levels need upload
};

} // namespace VirtualTexture
