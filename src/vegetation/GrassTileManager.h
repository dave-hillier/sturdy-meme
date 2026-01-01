#pragma once

#include "GrassTile.h"
#include "GrassConstants.h"
#include "BufferUtils.h"
#include "DescriptorManager.h"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <array>
#include <memory>

// Forward declarations
class DescriptorManager;
struct TiledGrassPushConstants;  // Defined in GrassSystem.h

/**
 * GrassTileManager - Manages streaming grass tiles around the camera
 *
 * Based on Ghost of Tsushima's approach:
 * - World is divided into a grid of tiles
 * - Tiles around the camera are loaded (3x3 = 9 tiles)
 * - Each tile has its own compute dispatch and draw call
 * - Double-buffering: compute to one set while rendering from another
 */
class GrassTileManager {
public:
    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator = VK_NULL_HANDLE;
        DescriptorManager::Pool* descriptorPool = nullptr;
        uint32_t framesInFlight = 3;
        std::string shaderPath;

        // Pipeline layouts/descriptor set layouts from GrassSystem
        // (shared between tiled and non-tiled rendering)
        vk::DescriptorSetLayout computeDescriptorSetLayout;
        vk::PipelineLayout computePipelineLayout;
        vk::Pipeline computePipeline;
        vk::DescriptorSetLayout graphicsDescriptorSetLayout;
        vk::PipelineLayout graphicsPipelineLayout;
        vk::Pipeline graphicsPipeline;
    };

    GrassTileManager() = default;
    ~GrassTileManager();

    // Non-copyable, non-movable
    GrassTileManager(const GrassTileManager&) = delete;
    GrassTileManager& operator=(const GrassTileManager&) = delete;

    /**
     * Initialize the tile manager
     */
    bool init(const InitInfo& info);

    /**
     * Cleanup resources
     */
    void destroy();

    /**
     * Update active tiles based on camera position
     * Call this once per frame before compute/render
     * @param cameraPos Current camera position
     * @param frameNumber Current frame number (for tracking tile usage)
     */
    void updateActiveTiles(const glm::vec3& cameraPos, uint64_t frameNumber);

    /**
     * Update descriptor sets with shared resources
     * Call when terrain/shadow resources are available
     */
    void updateDescriptorSets(
        vk::ImageView terrainHeightMapView, vk::Sampler terrainHeightMapSampler,
        vk::ImageView displacementView, vk::Sampler displacementSampler,
        vk::ImageView tileArrayView, vk::Sampler tileSampler,
        const std::array<vk::Buffer, 3>& tileInfoBuffers,
        const std::vector<vk::Buffer>& cullingUniformBuffers,
        const std::vector<vk::Buffer>& grassParamsBuffers
    );

    /**
     * Record compute dispatches for all active tiles
     */
    void recordCompute(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                       uint32_t computeBufferSet);

    /**
     * Set shared buffers from GrassSystem
     * In tiled mode, all tiles write to these shared buffers instead of per-tile buffers
     */
    void setSharedBuffers(vk::Buffer sharedInstanceBuffer, vk::Buffer sharedIndirectBuffer);

    /**
     * Record draw calls - uses shared buffers with single draw call
     * (same as non-tiled mode, just compute dispatches are tiled)
     */
    void recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                    uint32_t renderBufferSet,
                    vk::Pipeline graphicsPipeline,
                    vk::PipelineLayout graphicsPipelineLayout,
                    vk::DescriptorSet graphicsDescriptorSet,
                    vk::Buffer sharedIndirectBuffer,
                    const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO);

    /**
     * Get number of currently active tiles
     */
    size_t getActiveTileCount() const { return activeTiles_.size(); }

    /**
     * Check if the tiled system is enabled and initialized
     */
    bool isEnabled() const { return enabled_; }

    /**
     * Get total number of loaded tiles (including inactive)
     */
    size_t getTotalTileCount() const { return tiles_.size(); }

private:
    /**
     * Unload tiles that are too far from camera and safe to release
     * Uses hysteresis margin to prevent thrashing
     */
    void unloadDistantTiles(const glm::vec2& cameraXZ, uint64_t currentFrame);
    /**
     * Calculate which tile coordinate contains a world position
     */
    GrassTile::TileCoord worldToTileCoord(const glm::vec2& worldPos) const;

    /**
     * Get or create a tile at the given coordinate
     */
    GrassTile* getOrCreateTile(const GrassTile::TileCoord& coord);

    /**
     * Create compute descriptor sets for a tile
     */
    bool createTileDescriptorSets(GrassTile* tile);

    /**
     * Update a tile's descriptor sets with current buffers
     */
    void updateTileDescriptorSets(GrassTile* tile, uint32_t bufferSetIndex);

    bool enabled_ = false;
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    uint32_t framesInFlight_ = 3;

    // Shared pipeline resources (from GrassSystem)
    vk::DescriptorSetLayout computeDescriptorSetLayout_;
    vk::PipelineLayout computePipelineLayout_;
    vk::Pipeline computePipeline_;

    // Currently active tiles (indexed by tile coordinate)
    std::unordered_map<GrassTile::TileCoord, std::unique_ptr<GrassTile>, GrassTile::TileCoordHash> tiles_;

    // Ordered list of active tiles for iteration
    std::vector<GrassTile*> activeTiles_;

    // Per-tile descriptor sets (compute only - graphics uses shared descriptor)
    // Key: tile pointer, Value: array of descriptor sets (one per buffer set)
    std::unordered_map<GrassTile*, std::vector<vk::DescriptorSet>> tileDescriptorSets_;

    // Current camera tile coordinate (for detecting movement)
    GrassTile::TileCoord currentCameraTile_{0, 0};

    // Frame tracking for tile unloading
    uint64_t currentFrame_ = 0;

    // Shared buffers from GrassSystem (all tiles write to these)
    vk::Buffer sharedInstanceBuffer_;
    vk::Buffer sharedIndirectBuffer_;

    // Shared resources for descriptor updates
    vk::ImageView terrainHeightMapView_;
    vk::Sampler terrainHeightMapSampler_;
    vk::ImageView displacementView_;
    vk::Sampler displacementSampler_;
    vk::ImageView tileArrayView_;
    vk::Sampler tileSampler_;
    std::array<vk::Buffer, 3> tileInfoBuffers_;
    std::vector<vk::Buffer> cullingUniformBuffers_;
    std::vector<vk::Buffer> grassParamsBuffers_;
};
