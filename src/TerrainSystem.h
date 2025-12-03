#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>
#include "UBOs.h"
#include "TerrainHeightMap.h"
#include "TerrainTextures.h"
#include "TerrainCBT.h"

class GpuProfiler;

// Push constants for terrain rendering
struct TerrainPushConstants {
    glm::mat4 model;
};

// Push constants for shadow pass
struct TerrainShadowPushConstants {
    glm::mat4 lightViewProj;
    float terrainSize;
    float heightScale;
    int cascadeIndex;
    int padding;
};

// Push constants for dispatcher compute shader
struct TerrainDispatcherPushConstants {
    uint32_t subdivisionWorkgroupSize;
    uint32_t meshletVertexCount;
};

// Push constants for sum reduction (legacy single-pass)
struct TerrainSumReductionPushConstants {
    int passID;
};

// Push constants for batched sum reduction (multi-level)
struct TerrainSumReductionBatchedPushConstants {
    int startLevel;       // Starting depth level
    int levelsToProcess;  // Number of levels to process in this dispatch
};

// Subgroup capabilities for runtime feature detection
struct SubgroupCapabilities {
    bool hasSubgroupArithmetic = false;
    uint32_t subgroupSize = 0;
};

// Push constants for subdivision compute shader
struct TerrainSubdivisionPushConstants {
    uint32_t updateMode;  // 0 = split only, 1 = merge only
};

// Terrain configuration (outside class to avoid C++17 default argument issues)
struct TerrainConfig {
    float size = 16384.0f;              // Terrain size in world units (matches 16384x16384 heightmap)
    float targetEdgePixels = 16.0f;   // Target triangle edge length in pixels
    int maxDepth = 28;                // Maximum CBT subdivision depth (28 gives ~1m resolution for 16km terrain)
    int minDepth = 6;                 // Minimum subdivision depth (64 triangles, ~4km edges)
    float splitThreshold = 24.0f;     // Screen pixels to trigger split (with hysteresis)
    float mergeThreshold = 8.0f;      // Screen pixels to trigger merge
    std::string heightmapPath;        // Optional: path to 16-bit PNG heightmap
    float minAltitude = 0.0f;         // Altitude for height value 0 (when loading from file)
    float maxAltitude = 200.0f;       // Altitude for height value 65535 (when loading from file)

    // Computed height scale (maxAltitude - minAltitude), set during init
    float heightScale = 0.0f;
};

class TerrainSystem {
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

    TerrainSystem() = default;
    ~TerrainSystem() = default;

    bool init(const InitInfo& info, const TerrainConfig& config = {});
    void destroy(VkDevice device, VmaAllocator allocator);

    // Update terrain descriptor sets with shared resources
    void updateDescriptorSets(VkDevice device,
                              const std::vector<VkBuffer>& sceneUniformBuffers,
                              VkImageView shadowMapView,
                              VkSampler shadowSampler);

    // Set snow mask texture for snow accumulation rendering (legacy)
    void setSnowMask(VkDevice device, VkImageView snowMaskView, VkSampler snowMaskSampler);

    // Set volumetric snow cascade textures
    void setVolumetricSnowCascades(VkDevice device,
                                    VkImageView cascade0View, VkImageView cascade1View, VkImageView cascade2View,
                                    VkSampler cascadeSampler);

    // Set cloud shadow map texture
    void setCloudShadowMap(VkDevice device, VkImageView cloudShadowView, VkSampler cloudShadowSampler);

    // Update terrain uniforms for a frame
    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                        const glm::mat4& view, const glm::mat4& proj,
                        const std::array<glm::vec4, 3>& snowCascadeParams = {},
                        bool useVolumetricSnow = false,
                        float snowMaxHeight = 10.0f);

    // Record compute commands (subdivision update)
    // Pass optional GpuProfiler for detailed per-phase profiling
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex, GpuProfiler* profiler = nullptr);

    // Record terrain rendering
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex);

    // Record shadow pass for terrain
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                          const glm::mat4& lightViewProj, int cascadeIndex);

    // Get terrain height at world position (CPU-side, for collision)
    float getHeightAt(float x, float z) const;

    // Get raw heightmap data for physics integration
    const float* getHeightMapData() const { return heightMap.getData(); }
    uint32_t getHeightMapResolution() const { return heightMap.getResolution(); }

    // Get current triangle count from GPU (for debugging/display)
    uint32_t getTriangleCount() const;

    // Legacy method - prefer getTriangleCount()
    uint32_t getNodeCount() const { return getTriangleCount(); }

    // Config accessors
    const TerrainConfig& getConfig() const { return config; }
    void setConfig(const TerrainConfig& newConfig) { config = newConfig; }

    // Heightmap accessors for grass integration
    VkImageView getHeightMapView() const { return heightMap.getView(); }
    VkSampler getHeightMapSampler() const { return heightMap.getSampler(); }

    // Toggle wireframe mode for debugging
    void setWireframeMode(bool enabled) { wireframeMode = enabled; }
    bool isWireframeMode() const { return wireframeMode; }

private:
    // Initialization helpers
    bool createUniformBuffers();
    bool createIndirectBuffers();

    // Descriptor set creation
    bool createComputeDescriptorSetLayout();
    bool createRenderDescriptorSetLayout();
    bool createDescriptorSets();

    // Pipeline creation
    bool createDispatcherPipeline();
    bool createSubdivisionPipeline();
    bool createSumReductionPipelines();
    bool createRenderPipeline();
    bool createWireframePipeline();
    bool createShadowPipeline();

    // Utility functions
    void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);
    void querySubgroupCapabilities();

    // Vulkan resources
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    uint32_t shadowMapSize = 0;
    std::string shaderPath;
    std::string texturePath;
    uint32_t framesInFlight = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Composed subsystems
    TerrainHeightMap heightMap;
    TerrainTextures textures;
    TerrainCBT cbt;

    // Indirect dispatch/draw buffers
    VkBuffer indirectDispatchBuffer = VK_NULL_HANDLE;
    VmaAllocation indirectDispatchAllocation = VK_NULL_HANDLE;
    VkBuffer indirectDrawBuffer = VK_NULL_HANDLE;
    VmaAllocation indirectDrawAllocation = VK_NULL_HANDLE;
    void* indirectDrawMappedPtr = nullptr;  // Persistently mapped for readback

    // Uniform buffers (per frame in flight)
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformAllocations;
    std::vector<void*> uniformMappedPtrs;

    // Compute pipelines
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout dispatcherPipelineLayout = VK_NULL_HANDLE;
    VkPipeline dispatcherPipeline = VK_NULL_HANDLE;
    VkPipelineLayout subdivisionPipelineLayout = VK_NULL_HANDLE;
    VkPipeline subdivisionPipeline = VK_NULL_HANDLE;
    VkPipelineLayout sumReductionPipelineLayout = VK_NULL_HANDLE;
    VkPipeline sumReductionPrepassPipeline = VK_NULL_HANDLE;
    VkPipeline sumReductionPrepassSubgroupPipeline = VK_NULL_HANDLE;  // Subgroup-optimized version
    VkPipeline sumReductionPipeline = VK_NULL_HANDLE;

    // Optimized sum reduction pipelines (batched multi-level)
    VkPipelineLayout sumReductionBatchedPipelineLayout = VK_NULL_HANDLE;
    VkPipeline sumReductionBatchedPipeline = VK_NULL_HANDLE;

    // Render pipelines
    VkDescriptorSetLayout renderDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout renderPipelineLayout = VK_NULL_HANDLE;
    VkPipeline renderPipeline = VK_NULL_HANDLE;
    VkPipeline wireframePipeline = VK_NULL_HANDLE;

    // Shadow pipeline
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;

    // Descriptor sets
    std::vector<VkDescriptorSet> computeDescriptorSets;  // Per frame
    std::vector<VkDescriptorSet> renderDescriptorSets;   // Per frame
    std::vector<VkDescriptorSet> shadowDescriptorSets;   // Per frame

    // Configuration
    TerrainConfig config;
    bool wireframeMode = false;

    // Runtime state
    uint32_t currentNodeCount = 2;  // Start with 2 root triangles
    uint32_t subdivisionFrameCount = 0;  // Frame counter for split/merge ping-pong
    SubgroupCapabilities subgroupCaps;  // GPU subgroup feature support

    // Camera state tracking for skip-frame optimization
    struct CameraState {
        glm::vec3 position{0.0f};
        glm::vec3 forward{0.0f, 0.0f, -1.0f};
        bool valid = false;
    };
    CameraState previousCamera;
    uint32_t staticFrameCount = 0;       // Consecutive frames camera hasn't moved
    uint32_t framesSinceLastCompute = 0; // Frames since last subdivision compute
    bool forceNextCompute = true;        // Force compute on next frame

    // Skip-frame thresholds
    static constexpr float POSITION_THRESHOLD = 0.1f;    // World units
    static constexpr float ROTATION_THRESHOLD = 0.001f;  // Dot product threshold
    static constexpr uint32_t MAX_SKIP_FRAMES = 30;      // Force update every N frames
    static constexpr uint32_t CONVERGENCE_FRAMES = 4;    // Frames to converge after camera stops

    // Helper to detect camera movement
    bool cameraHasMoved(const glm::vec3& cameraPos, const glm::mat4& view);

    // Constants
    static constexpr uint32_t SUBDIVISION_WORKGROUP_SIZE = 64;
    static constexpr uint32_t SUM_REDUCTION_WORKGROUP_SIZE = 256;
};
