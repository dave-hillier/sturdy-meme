#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>

#include "VmaBuffer.h"

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
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit WaterTileCull(ConstructToken) {}

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
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Tile visibility data
    struct TileData {
        glm::uvec2 tileCoord;     // Tile coordinate (x, y)
        float minDepth;           // Minimum water depth in tile
        float maxDepth;           // Maximum water depth in tile
    };

    // Push constants for tile cull compute
    // alignas(16) ensures proper alignment for SIMD operations on glm::mat4.
    // Without this, O3-optimized aligned SSE/AVX loads can crash.
    struct alignas(16) TileCullPushConstants {
        glm::mat4 viewProjMatrix;
        glm::vec4 waterPlane;     // xyz = normal, w = -distance
        glm::vec4 cameraPos;      // xyz = position, w = unused
        glm::uvec2 screenSize;    // Screen dimensions
        glm::uvec2 tileCount;     // Number of tiles (x, y)
        float waterLevel;         // Base water level
        float tileSize;           // Tile size in pixels
        float nearPlane;
        float farPlane;
        uint32_t maxTiles;        // Output buffer capacity
        uint32_t _pad0;
    };

    // Indirect draw command for Vulkan
    struct IndirectDrawCommand {
        uint32_t vertexCount;
        uint32_t instanceCount;
        uint32_t firstVertex;
        uint32_t firstInstance;
    };

    /**
     * Factory: Create and initialize WaterTileCull.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<WaterTileCull> create(const InitInfo& info);


    ~WaterTileCull();

    // Non-copyable, non-movable
    WaterTileCull(const WaterTileCull&) = delete;
    WaterTileCull& operator=(const WaterTileCull&) = delete;
    WaterTileCull(WaterTileCull&&) = delete;
    WaterTileCull& operator=(WaterTileCull&&) = delete;
    void resize(VkExtent2D newExtent);

    // Record compute pass to determine visible tiles
    // Call before water rendering
    void recordTileCull(VkCommandBuffer cmd, uint32_t frameIndex,
                        const glm::mat4& viewProj,
                        const glm::vec3& cameraPos,
                        float waterLevel,
                        VkImageView depthView);

    // Get indirect draw buffer for water rendering
    VkBuffer getIndirectDrawBuffer() const { return indirectDrawBuffer_.get(); }

    // Get visible tile count for this frame
    uint32_t getVisibleTileCount(uint32_t frameIndex) const;

    // Check if water was visible in previous frame (for temporal culling)
    // Uses previous frame's tile cull results to skip water draw
    bool wasWaterVisibleLastFrame(uint32_t currentFrameIndex) const;

    // Get tile buffer for debug visualization
    VkBuffer getTileBuffer() const { return tileBuffer_.get(); }

    // Configuration
    void setEnabled(bool enable) { enabled = enable; }
    bool isEnabled() const { return enabled; }
    glm::uvec2 getTileCount() const { return tileCount; }
    uint32_t getTileSize() const { return tileSize; }

    // Call at end of frame to update visibility tracking
    void endFrame(uint32_t frameIndex);

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createBuffers();
    bool createComputePipeline();
    bool createDescriptorSets();

    // Synchronize counter buffer for transfer (compute → transfer)
    void barrierCounterForTransfer(VkCommandBuffer cmd, uint32_t frameIndex);

    // Synchronize tile/indirect buffers for drawing (compute → vertex/draw)
    void barrierCullResultsForDraw(VkCommandBuffer cmd, uint32_t frameIndex);

    // Synchronize counter readback for CPU access (transfer → host)
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

    // Tile visibility buffer (stores which tiles contain water)
    ManagedBuffer tileBuffer_;

    // Counter buffer (atomic counter for visible tiles, CPU-to-GPU mapped)
    ManagedBuffer counterBuffer_;
    void* counterMapped = nullptr;

    // Readback buffer for CPU-visible counter results
    ManagedBuffer counterReadbackBuffer_;
    void* counterReadbackMapped = nullptr;

    // Indirect draw buffer
    ManagedBuffer indirectDrawBuffer_;

    // Compute pipeline (RAII-managed)
    std::optional<vk::raii::Pipeline> computePipeline_;
    std::optional<vk::raii::PipelineLayout> computePipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::DescriptorPool> descriptorPool_;
    std::vector<VkDescriptorSet> descriptorSets;

    // Depth texture sampler for tile culling (RAII-managed)
    std::optional<vk::raii::Sampler> depthSampler_;

    // CPU-side visibility tracking to avoid double-buffer aliasing issues
    // Tracks the last frame number where water was visible
    uint64_t lastVisibleFrame = 0;
    uint64_t currentAbsoluteFrame = 0;
    static constexpr uint64_t VISIBILITY_GRACE_FRAMES = 4;  // Keep rendering for N frames after last visible

    // RAII device pointer
    const vk::raii::Device* raiiDevice_ = nullptr;
};
