#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <array>
#include "TerrainStreamingManager.h"
#include "TerrainTextures.h"

// Push constants for tile rendering
struct TileRenderPushConstants {
    glm::vec2 tileOffset;    // World offset for this tile
    float tileSize;          // Size of this tile in world units
    float heightScale;       // Height scale
};

// Push constants for tile shadow rendering
struct TileShadowPushConstants {
    glm::mat4 lightViewProj;
    glm::vec2 tileOffset;
    float tileSize;
    float heightScale;
    int cascadeIndex;
    int padding[3];
};

// Configuration for paged terrain rendering
struct PagedTerrainConfig {
    TerrainStreamingConfig streamingConfig;
    float targetEdgePixels = 16.0f;
    int maxCBTDepth = 16;           // Per-tile CBT depth
    int minCBTDepth = 2;
    float splitThreshold = 24.0f;
    float mergeThreshold = 8.0f;
};

// Renders terrain using paged tiles from TerrainStreamingManager
class PagedTerrainRenderer {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkRenderPass renderPass;
        VkRenderPass shadowRenderPass;
        VkDescriptorPool descriptorPool;
        VkExtent2D extent;
        uint32_t shadowMapSize;
        std::string shaderPath;
        std::string texturePath;
        uint32_t framesInFlight;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
    };

    PagedTerrainRenderer() = default;
    ~PagedTerrainRenderer() = default;

    bool init(const InitInfo& info, const PagedTerrainConfig& config);
    void destroy();

    // Update streaming (call once per frame before rendering)
    void update(const glm::vec3& cameraPos, uint64_t frameNumber);

    // Update shared descriptor set resources
    void updateDescriptorSets(const std::vector<VkBuffer>& sceneUniformBuffers,
                               VkImageView shadowMapView,
                               VkSampler shadowSampler);

    // Update uniforms for current frame
    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                        const glm::mat4& view, const glm::mat4& proj);

    // Record compute commands for all visible tiles
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex);

    // Record draw commands for all visible tiles
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex);

    // Record shadow draw for all visible tiles
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                          const glm::mat4& lightViewProj, int cascadeIndex);

    // Get height at world position
    float getHeightAt(float worldX, float worldZ) const;

    // Statistics
    uint32_t getLoadedTileCount() const;
    uint32_t getVisibleTileCount() const;
    size_t getGPUMemoryUsage() const;

    // Toggle wireframe mode
    void setWireframeMode(bool enabled) { wireframeMode = enabled; }
    bool isWireframeMode() const { return wireframeMode; }

private:
    // Per-tile descriptor set management
    struct TileDescriptorSet {
        VkDescriptorSet computeSet = VK_NULL_HANDLE;
        VkDescriptorSet renderSet = VK_NULL_HANDLE;
        TerrainTile* tile = nullptr;  // Which tile this is bound to
    };

    bool createPipelines();
    bool createDescriptorSetLayouts();
    bool createUniformBuffers();

    // Get or allocate descriptor sets for a tile
    TileDescriptorSet* getDescriptorSetForTile(TerrainTile* tile, uint32_t frameIndex);

    // Update descriptor set for a specific tile
    void updateTileDescriptorSet(TileDescriptorSet* ds, TerrainTile* tile, uint32_t frameIndex);

    // Record compute for a single tile
    void recordTileCompute(VkCommandBuffer cmd, TerrainTile* tile,
                           TileDescriptorSet* ds, uint32_t frameIndex);

    // Record draw for a single tile
    void recordTileDraw(VkCommandBuffer cmd, TerrainTile* tile,
                        TileDescriptorSet* ds, uint32_t frameIndex);

    // Vulkan context
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkExtent2D extent{};
    uint32_t shadowMapSize = 0;
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Configuration
    PagedTerrainConfig config;
    bool wireframeMode = false;

    // Streaming manager
    std::unique_ptr<TerrainStreamingManager> streamingManager;

    // Shared textures
    TerrainTextures textures;

    // Descriptor set layouts (shared by all tiles)
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout renderDescriptorSetLayout = VK_NULL_HANDLE;

    // Pipelines (shared by all tiles)
    VkPipelineLayout dispatcherPipelineLayout = VK_NULL_HANDLE;
    VkPipeline dispatcherPipeline = VK_NULL_HANDLE;
    VkPipelineLayout subdivisionPipelineLayout = VK_NULL_HANDLE;
    VkPipeline subdivisionPipeline = VK_NULL_HANDLE;
    VkPipelineLayout sumReductionPipelineLayout = VK_NULL_HANDLE;
    VkPipeline sumReductionPrepassPipeline = VK_NULL_HANDLE;
    VkPipeline sumReductionPipeline = VK_NULL_HANDLE;
    VkPipelineLayout renderPipelineLayout = VK_NULL_HANDLE;
    VkPipeline renderPipeline = VK_NULL_HANDLE;
    VkPipeline wireframePipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;

    // Per-frame uniform buffers
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformAllocations;
    std::vector<void*> uniformMappedPtrs;

    // Per-tile indirect buffers (pooled)
    struct IndirectBuffers {
        VkBuffer dispatchBuffer = VK_NULL_HANDLE;
        VmaAllocation dispatchAllocation = VK_NULL_HANDLE;
        VkBuffer drawBuffer = VK_NULL_HANDLE;
        VmaAllocation drawAllocation = VK_NULL_HANDLE;
    };
    std::vector<IndirectBuffers> indirectBufferPool;

    // Per-tile descriptor sets (pooled, per frame)
    std::vector<std::vector<TileDescriptorSet>> tileDescriptorSets;  // [frameIndex][tileIndex]
    size_t descriptorSetPoolSize = 0;

    // Frame counter for subdivision ping-pong
    uint32_t subdivisionFrameCount = 0;

    // Constants
    static constexpr uint32_t SUBDIVISION_WORKGROUP_SIZE = 64;
    static constexpr uint32_t SUM_REDUCTION_WORKGROUP_SIZE = 256;
    static constexpr size_t INITIAL_DESCRIPTOR_POOL_SIZE = 32;
};
