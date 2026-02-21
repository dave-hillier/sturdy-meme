#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <array>
#include <cstdint>
#include <vector>

struct TerrainTile;

// Manages the shared 2D array texture for terrain tile data.
// Handles layer allocation/deallocation and copying tile data into layers.
class TileArrayManager {
public:
    struct InitInfo {
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        uint32_t storedTileResolution = 513;
        uint32_t maxLayers = 64;
    };

    TileArrayManager() = default;
    ~TileArrayManager();

    TileArrayManager(const TileArrayManager&) = delete;
    TileArrayManager& operator=(const TileArrayManager&) = delete;
    TileArrayManager(TileArrayManager&&) = delete;
    TileArrayManager& operator=(TileArrayManager&&) = delete;

    bool init(const InitInfo& info);
    void cleanup();

    // Allocate a free layer, returns -1 if none available
    int32_t allocateLayer();

    // Free a previously allocated layer
    void freeLayer(int32_t layerIndex);

    // Copy tile CPU data into a specific array layer (GPU upload)
    void copyTileToLayer(const TerrainTile& tile, uint32_t layerIndex);

    VkImageView getArrayView() const { return arrayView_; }
    VkImage getArrayImage() const { return arrayImage_; }
    uint32_t getMaxLayers() const { return maxLayers_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    uint32_t storedTileResolution_ = 513;
    uint32_t maxLayers_ = 64;

    VkImage arrayImage_ = VK_NULL_HANDLE;
    VmaAllocation arrayAllocation_ = VK_NULL_HANDLE;
    VkImageView arrayView_ = VK_NULL_HANDLE;

    std::array<bool, 64> freeLayers_;
};
