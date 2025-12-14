#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

/**
 * WaterTileCull - Phase 7: Screen-Space Tile Visibility
 *
 * Implements tile-based visibility culling for water rendering:
 * - Divides screen into tiles (default 32x32 pixels)
 * - Compute shader determines which tiles contain water
 * - Uses indirect drawing to only render visible tiles
 * - Significant performance improvement for partial water coverage
 *
 * Based on Far Cry 5 water rendering GDC 2018.
 */

class WaterTileCull {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue computeQueue;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkExtent2D extent;
        uint32_t tileSize = 32;  // Pixels per tile
    };

    // Tile visibility data
    struct TileData {
        glm::uvec2 tileCoord;     // Tile coordinate (x, y)
        float minDepth;           // Minimum water depth in tile
        float maxDepth;           // Maximum water depth in tile
    };

    // Push constants for tile cull compute
    struct TileCullPushConstants {
        glm::mat4 viewProjMatrix;
        glm::vec4 waterPlane;     // xyz = normal, w = -distance
        glm::vec4 cameraPos;      // xyz = position, w = unused
        glm::uvec2 screenSize;    // Screen dimensions
        glm::uvec2 tileCount;     // Number of tiles (x, y)
        float waterLevel;         // Base water level
        float tileSize;           // Tile size in pixels
        float nearPlane;
        float farPlane;
    };

    // Indirect draw command for Vulkan
    struct IndirectDrawCommand {
        uint32_t vertexCount;
        uint32_t instanceCount;
        uint32_t firstVertex;
        uint32_t firstInstance;
    };

    WaterTileCull() = default;
    ~WaterTileCull() = default;

    bool init(const InitInfo& info);
    void destroy();
    void resize(VkExtent2D newExtent);

    // Record compute pass to determine visible tiles
    // Call before water rendering
    void recordTileCull(VkCommandBuffer cmd, uint32_t frameIndex,
                        const glm::mat4& viewProj,
                        const glm::vec3& cameraPos,
                        float waterLevel,
                        VkImageView depthView);

    // Get indirect draw buffer for water rendering
    VkBuffer getIndirectDrawBuffer() const { return indirectDrawBuffer; }

    // Get visible tile count for this frame
    uint32_t getVisibleTileCount(uint32_t frameIndex) const;

    // Check if water was visible in previous frame (for temporal culling)
    // Uses previous frame's tile cull results to skip water draw
    bool wasWaterVisibleLastFrame(uint32_t currentFrameIndex) const;

    // Get tile buffer for debug visualization
    VkBuffer getTileBuffer() const { return tileBuffer; }

    // Configuration
    void setEnabled(bool enable) { enabled = enable; }
    bool isEnabled() const { return enabled; }
    glm::uvec2 getTileCount() const { return tileCount; }
    uint32_t getTileSize() const { return tileSize; }

    // Call at end of frame to update visibility tracking
    void endFrame(uint32_t frameIndex);

private:
    bool createBuffers();
    bool createComputePipeline();
    bool createDescriptorSets();

    // Synchronize cull output for drawing and counter transfer
    void barrierCullResultsForDrawAndTransfer(VkCommandBuffer cmd, uint32_t frameIndex);

    // Synchronize counter readback for CPU access
    void barrierCounterForHostRead(VkCommandBuffer cmd, uint32_t frameIndex);

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    std::string shaderPath;

    uint32_t framesInFlight = 0;
    VkExtent2D extent = {0, 0};
    uint32_t tileSize = 32;
    glm::uvec2 tileCount = {0, 0};
    bool enabled = true;

    // Tile visibility buffer
    // Stores which tiles contain water
    VkBuffer tileBuffer = VK_NULL_HANDLE;
    VmaAllocation tileAllocation = VK_NULL_HANDLE;

    // Counter buffer (atomic counter for visible tiles)
    VkBuffer counterBuffer = VK_NULL_HANDLE;
    VmaAllocation counterAllocation = VK_NULL_HANDLE;
    void* counterMapped = nullptr;

    // Readback buffer for CPU-visible counter results
    VkBuffer counterReadbackBuffer = VK_NULL_HANDLE;
    VmaAllocation counterReadbackAllocation = VK_NULL_HANDLE;
    void* counterReadbackMapped = nullptr;

    // Indirect draw buffer
    VkBuffer indirectDrawBuffer = VK_NULL_HANDLE;
    VmaAllocation indirectDrawAllocation = VK_NULL_HANDLE;

    // Compute pipeline
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Depth texture sampler for tile culling
    VkSampler depthSampler = VK_NULL_HANDLE;

    // CPU-side visibility tracking to avoid double-buffer aliasing issues
    // Tracks the last frame number where water was visible
    uint64_t lastVisibleFrame = 0;
    uint64_t currentAbsoluteFrame = 0;
    static constexpr uint64_t VISIBILITY_GRACE_FRAMES = 4;  // Keep rendering for N frames after last visible
};
