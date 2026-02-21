#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>
#include <optional>
#include "TileGridLogic.h"

struct TerrainTile;

using TerrainHole = TileGrid::TerrainHole;

// Manages terrain hole definitions and their GPU hole mask textures.
// Holes are circular cutouts rasterized per-tile into a 2D array texture.
class HoleMaskManager {
public:
    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        uint32_t storedTileResolution = 513;
        uint32_t maxLayers = 64;
    };

    HoleMaskManager() = default;
    ~HoleMaskManager();

    HoleMaskManager(const HoleMaskManager&) = delete;
    HoleMaskManager& operator=(const HoleMaskManager&) = delete;
    HoleMaskManager(HoleMaskManager&&) = delete;
    HoleMaskManager& operator=(HoleMaskManager&&) = delete;

    bool init(const InitInfo& info);
    void cleanup();

    // Hole management
    void addHoleCircle(float centerX, float centerZ, float radius,
                       const std::vector<TerrainTile*>& activeTiles);
    void removeHoleCircle(float centerX, float centerZ, float radius,
                          const std::vector<TerrainTile*>& activeTiles);
    const std::vector<TerrainHole>& getHoles() const { return holes_; }

    // Query if a point is inside any hole (analytical, not rasterized)
    bool isHole(float x, float z) const;

    // Rasterize holes into a tile mask at specified resolution
    std::vector<uint8_t> rasterizeHolesForTile(float tileMinX, float tileMinZ,
                                                float tileMaxX, float tileMaxZ,
                                                uint32_t resolution) const;

    // Upload hole mask for a specific tile layer
    void uploadTileHoleMask(const TerrainTile& tile, int32_t layerIndex);

    // Re-upload hole masks for all active tiles
    void uploadAllActiveMasks(const std::vector<TerrainTile*>& activeTiles);

    // GPU resource accessors
    VkImageView getArrayView() const { return arrayView_; }
    VkSampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

private:
    std::vector<uint8_t> generateTileHoleMask(const TerrainTile& tile) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    uint32_t storedTileResolution_ = 513;
    uint32_t maxLayers_ = 64;

    VkImage arrayImage_ = VK_NULL_HANDLE;
    VmaAllocation arrayAllocation_ = VK_NULL_HANDLE;
    VkImageView arrayView_ = VK_NULL_HANDLE;
    std::optional<vk::raii::Sampler> sampler_;

    std::vector<TerrainHole> holes_;
};
