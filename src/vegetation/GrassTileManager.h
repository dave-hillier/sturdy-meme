#pragma once

#include "GrassTile.h"
#include "GrassTileTracker.h"
#include "GrassTileResourcePool.h"
#include "GrassTileLoadQueue.h"
#include "GrassConstants.h"
#include "GrassLODStrategy.h"
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
 * Composed of:
 * - GrassTileTracker: Pure logic for tile decisions (no Vulkan)
 * - GrassTileResourcePool: Vulkan resource management
 * - GrassTileLoadQueue: Async loading with frame budget
 *
 * Based on Ghost of Tsushima's approach:
 * - World is divided into a grid of tiles
 * - Tiles around the camera are loaded (multi-LOD)
 * - Each tile has its own compute dispatch
 * - Async loading prevents frame hitches
 */
class GrassTileManager {
public:
    using TileCoord = GrassTile::TileCoord;
    using TileCoordHash = GrassTile::TileCoordHash;

    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator = VK_NULL_HANDLE;
        DescriptorManager::Pool* descriptorPool = nullptr;
        uint32_t framesInFlight = 3;
        std::string shaderPath;

        // Pipeline layouts/descriptor set layouts from GrassSystem
        vk::DescriptorSetLayout computeDescriptorSetLayout;
        vk::PipelineLayout computePipelineLayout;
        vk::Pipeline computePipeline;
        vk::DescriptorSetLayout graphicsDescriptorSetLayout;
        vk::PipelineLayout graphicsPipelineLayout;
        vk::Pipeline graphicsPipeline;

        // Async loading configuration
        uint32_t maxLoadsPerFrame = 3;        // Max tiles to load per frame
        float teleportThreshold = 500.0f;     // Distance to detect teleportation
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
     * Uses async loading to prevent hitches
     */
    void updateActiveTiles(const glm::vec3& cameraPos, uint64_t frameNumber, float currentTime);

    /**
     * Update descriptor sets with shared resources
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
     */
    void setSharedBuffers(vk::Buffer sharedInstanceBuffer, vk::Buffer sharedIndirectBuffer);

    /**
     * Record draw calls
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
     * Get number of tiles pending load
     */
    size_t getPendingLoadCount() const { return loadQueue_.getPendingCount(); }

    /**
     * Check if the tiled system is enabled
     */
    bool isEnabled() const { return enabled_; }

    /**
     * Get total number of loaded tiles
     */
    size_t getTotalTileCount() const { return resourcePool_.getAllocatedTileCount(); }

    /**
     * Access to tracker for testing/debugging
     */
    const GrassTileTracker& getTracker() const { return tracker_; }

    /**
     * Access to load queue for configuration
     */
    GrassTileLoadQueue& getLoadQueue() { return loadQueue_; }

    /**
     * Set the LOD strategy (forwards to tracker)
     */
    void setLODStrategy(std::unique_ptr<IGrassLODStrategy> strategy) {
        tracker_.setLODStrategy(std::move(strategy));
    }

    /**
     * Get the current LOD strategy
     */
    const IGrassLODStrategy* getLODStrategy() const {
        return tracker_.getLODStrategy();
    }

private:
    /**
     * Process tile load requests (respects per-frame budget)
     */
    void processLoadQueue(float currentTime);

    /**
     * Process tile unload requests
     */
    void processUnloads(const std::vector<GrassTileTracker::TileRequest>& unloadRequests);

    bool enabled_ = false;
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    uint32_t framesInFlight_ = 3;

    // Composed components
    GrassTileTracker tracker_;
    GrassTileResourcePool resourcePool_;
    GrassTileLoadQueue loadQueue_;

    // Shared pipeline resources
    vk::PipelineLayout computePipelineLayout_;
    vk::Pipeline computePipeline_;

    // Shared buffers
    vk::Buffer sharedInstanceBuffer_;
    vk::Buffer sharedIndirectBuffer_;

    // Active tile data for rendering
    struct ActiveTileData {
        TileCoord coord;
        float creationTime;
    };
    std::vector<ActiveTileData> activeTiles_;

    // Tile creation times (for fade-in)
    std::unordered_map<TileCoord, float, TileCoordHash> tileCreationTimes_;

    // Current time (for tile fade-in)
    float currentTime_ = 0.0f;
};
