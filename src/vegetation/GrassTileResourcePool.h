#pragma once

#include "GrassTile.h"
#include "GrassConstants.h"
#include "DescriptorManager.h"
#include "UBOs.h"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <unordered_map>
#include <array>
#include <vector>

/**
 * GrassTileResourcePool - Manages Vulkan resources for grass tiles
 *
 * This class handles:
 * - Descriptor set allocation and management per tile
 * - Shared resource references for descriptor updates
 * - Buffer binding updates
 *
 * Separates Vulkan resource management from tile logic (GrassTileTracker).
 */
class GrassTileResourcePool {
public:
    using TileCoord = GrassTile::TileCoord;
    using TileCoordHash = GrassTile::TileCoordHash;

    struct InitInfo {
        vk::Device device;
        DescriptorManager::Pool* descriptorPool = nullptr;
        uint32_t framesInFlight = 3;
        vk::DescriptorSetLayout computeDescriptorSetLayout;
    };

    GrassTileResourcePool() = default;
    ~GrassTileResourcePool();

    // Non-copyable, non-movable
    GrassTileResourcePool(const GrassTileResourcePool&) = delete;
    GrassTileResourcePool& operator=(const GrassTileResourcePool&) = delete;

    /**
     * Initialize the resource pool
     */
    bool init(const InitInfo& info);

    /**
     * Cleanup all resources
     */
    void destroy();

    /**
     * Allocate resources for a tile
     * @return true if successful
     */
    bool allocateForTile(const TileCoord& coord);

    /**
     * Release resources for a tile
     */
    void releaseForTile(const TileCoord& coord);

    /**
     * Get descriptor set for a tile at a specific buffer set index
     * @return VK_NULL_HANDLE if tile has no resources
     */
    vk::DescriptorSet getDescriptorSet(const TileCoord& coord, uint32_t bufferSetIndex) const;

    /**
     * Check if a tile has allocated resources
     */
    bool hasTileResources(const TileCoord& coord) const;

    /**
     * Set shared buffers (all tiles write to these)
     */
    void setSharedBuffers(vk::Buffer instanceBuffer, vk::Buffer indirectBuffer);

    /**
     * Set shared image resources
     */
    void setSharedImages(
        vk::ImageView terrainHeightMapView, vk::Sampler terrainHeightMapSampler,
        vk::ImageView displacementView, vk::Sampler displacementSampler,
        vk::ImageView tileArrayView, vk::Sampler tileSampler
    );

    /**
     * Set shared buffer arrays (triple-buffered)
     */
    void setSharedBufferArrays(
        const std::array<vk::Buffer, 3>& tileInfoBuffers,
        const std::vector<vk::Buffer>& cullingUniformBuffers,
        const std::vector<vk::Buffer>& grassParamsBuffers
    );

    /**
     * Write descriptor sets for a tile using current shared resources
     */
    void updateTileDescriptorSets(const TileCoord& coord);

    /**
     * Write per-frame descriptor set bindings for a tile
     * Called each frame before compute dispatch
     */
    void writePerFrameBindings(const TileCoord& coord, uint32_t bufferSetIndex);

    /**
     * Get the number of allocated tiles
     */
    size_t getAllocatedTileCount() const { return tileDescriptorSets_.size(); }

private:
    bool initialized_ = false;
    vk::Device device_;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    uint32_t framesInFlight_ = 3;
    vk::DescriptorSetLayout computeDescriptorSetLayout_;

    // Descriptor sets per tile (indexed by tile coordinate)
    std::unordered_map<TileCoord, std::vector<vk::DescriptorSet>, TileCoordHash> tileDescriptorSets_;

    // Shared buffers
    vk::Buffer sharedInstanceBuffer_;
    vk::Buffer sharedIndirectBuffer_;

    // Shared images
    vk::ImageView terrainHeightMapView_;
    vk::Sampler terrainHeightMapSampler_;
    vk::ImageView displacementView_;
    vk::Sampler displacementSampler_;
    vk::ImageView tileArrayView_;
    vk::Sampler tileSampler_;

    // Shared buffer arrays (triple-buffered)
    std::array<vk::Buffer, 3> tileInfoBuffers_;
    std::vector<vk::Buffer> cullingUniformBuffers_;
    std::vector<vk::Buffer> grassParamsBuffers_;
};
