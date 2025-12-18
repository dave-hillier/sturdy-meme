#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>
#include <memory>
#include "UBOs.h"
#include "TerrainHeightMap.h"
#include "TerrainTextures.h"
#include "TerrainCBT.h"
#include "TerrainMeshlet.h"
#include "TerrainTileCache.h"
#include "TerrainBuffers.h"
#include "TerrainCameraOptimizer.h"
#include "TerrainPipelines.h"
#include "DescriptorManager.h"
#include "InitContext.h"
#include "core/RAIIAdapter.h"
#include <optional>

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
    uint32_t meshletIndexCount;  // 0 = use direct triangles, >0 = use meshlet instancing
};

// Push constants for sum reduction (single-pass)
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
    uint32_t updateMode;       // 0 = split only, 1 = merge only
    uint32_t frameIndex;       // For temporal spreading
    uint32_t spreadFactor;     // Process 1/N triangles per frame (1 = all)
    uint32_t reserved;         // Reserved for future use (was useCompactBuffer)
};

// Push constants for prepare cull dispatch
struct TerrainPrepareCullDispatchPushConstants {
    uint32_t workgroupSize;  // Subdivision workgroup size
};

// Push constants for frustum cull (now includes dispatch calculation)
struct TerrainFrustumCullPushConstants {
    uint32_t subdivisionWorkgroupSize;  // For computing dispatch args
};

// Push constants for shadow cascade culling
struct TerrainShadowCullPushConstants {
    glm::mat4 lightViewProj;           // Light's view-projection matrix
    glm::vec4 lightFrustumPlanes[6];   // Frustum planes from lightViewProj
    float terrainSize;
    float heightScale;
    uint32_t cascadeIndex;
    uint32_t padding;
};

// Terrain configuration (outside class to avoid C++17 default argument issues)
struct TerrainConfig {
    float size = 16384.0f;              // Terrain size in world units (matches 16384x16384 heightmap)
    float targetEdgePixels = 16.0f;   // Target triangle edge length in pixels
    int maxDepth = 20;                // Maximum CBT subdivision depth (28 gives ~1m resolution for 16km terrain)
    int minDepth = 6;                 // Minimum subdivision depth (64 triangles, ~4km edges)
    float splitThreshold = 24.0f;     // Screen pixels to trigger split (with hysteresis)
    float mergeThreshold = 8.0f;      // Screen pixels to trigger merge
    uint32_t spreadFactor = 2;        // Temporal spreading: process 1/N triangles per frame (1 = all)
    std::string heightmapPath;        // Optional: path to 16-bit PNG heightmap
    float minAltitude = -15.0f;         // Altitude for height value 0 (when loading from file)
    float maxAltitude = 220.0f;       // Altitude for height value 65535 (when loading from file)

    // Computed height scale (maxAltitude - minAltitude), set during init
    float heightScale = 0.0f;

    // Meshlet settings
    bool useMeshlets = true;          // Enable meshlet-based rendering for higher detail
    int meshletSubdivisionLevel = 4;  // LEB subdivision level per meshlet (4 = 16 triangles)

    // Curvature-based LOD
    float flatnessScale = 2.0f;       // How much flat areas reduce subdivision (0=disabled, 2=3x threshold for flat)

    // Tile cache settings for LOD-based height streaming
    std::string tileCacheDir;         // Directory containing preprocessed tiles (empty = disabled)
    float tileLoadRadius = 2000.0f;   // Distance from camera to load high-res tiles
    float tileUnloadRadius = 3000.0f; // Distance from camera to unload tiles
};

class TerrainSystem {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkRenderPass renderPass;
        VkRenderPass shadowRenderPass;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkExtent2D extent;
        uint32_t shadowMapSize;
        std::string shaderPath;
        std::string texturePath;
        uint32_t framesInFlight;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
    };

    // System-specific params for InitContext-based init
    struct TerrainInitParams {
        VkRenderPass renderPass;
        VkRenderPass shadowRenderPass;
        uint32_t shadowMapSize;
        std::string texturePath;
    };

    /**
     * Factory: Create and initialize TerrainSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<TerrainSystem> create(const InitContext& ctx,
                                                  const TerrainInitParams& params,
                                                  const TerrainConfig& config = {});

    ~TerrainSystem();

    // Non-copyable, non-movable
    TerrainSystem(const TerrainSystem&) = delete;
    TerrainSystem& operator=(const TerrainSystem&) = delete;
    TerrainSystem(TerrainSystem&&) = delete;
    TerrainSystem& operator=(TerrainSystem&&) = delete;

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { extent = newExtent; }

    // Update terrain descriptor sets with shared resources
    void updateDescriptorSets(VkDevice device,
                              const std::vector<VkBuffer>& sceneUniformBuffers,
                              VkImageView shadowMapView,
                              VkSampler shadowSampler,
                              const std::vector<VkBuffer>& snowUBOBuffers,
                              const std::vector<VkBuffer>& cloudShadowUBOBuffers);

    // Set snow mask texture for snow accumulation rendering
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

    // Record shadow culling compute (call before recordShadowDraw for each cascade)
    void recordShadowCull(VkCommandBuffer cmd, uint32_t frameIndex,
                          const glm::mat4& lightViewProj, int cascadeIndex);

    // Shadow culling toggle
    void setShadowCulling(bool enabled) { shadowCullingEnabled = enabled; }
    bool isShadowCullingEnabled() const { return shadowCullingEnabled; }

    // Get terrain height at world position (CPU-side, for collision)
    float getHeightAt(float x, float z) const;

    // Get raw heightmap data for physics integration
    const float* getHeightMapData() const { return heightMap->getData(); }
    uint32_t getHeightMapResolution() const { return heightMap->getResolution(); }

    // Get current triangle count from GPU (for debugging/display)
    uint32_t getTriangleCount() const;

    // Optimization toggles
    void setSkipFrameOptimization(bool enabled) { cameraOptimizer.setEnabled(enabled); }
    bool isSkipFrameOptimizationEnabled() const { return cameraOptimizer.isEnabled(); }
    void setGpuCulling(bool enabled) { gpuCullingEnabled = enabled; }
    bool isGpuCullingEnabled() const { return gpuCullingEnabled; }

    // Debug info
    bool isCurrentlySkipping() const { return cameraOptimizer.wasLastFrameSkipped(); }
    uint32_t getCurrentPhase() const { return subdivisionFrameCount & 1; }  // 0 = split, 1 = merge

    // Legacy method - prefer getTriangleCount()
    uint32_t getNodeCount() const { return getTriangleCount(); }

    // Config accessors
    const TerrainConfig& getConfig() const { return config; }
    void setConfig(const TerrainConfig& newConfig) { config = newConfig; }

    // Heightmap accessors for grass integration
    VkImageView getHeightMapView() const { return heightMap->getView(); }
    VkSampler getHeightMapSampler() const { return heightMap->getSampler(); }

    // Hole mask for caves/wells (areas with no terrain)
    bool isHole(float x, float z) const { return heightMap->isHole(x, z); }
    void setHole(float x, float z, bool isHole) { heightMap->setHole(x, z, isHole); }
    void setHoleCircle(float centerX, float centerZ, float radius, bool isHole) {
        heightMap->setHoleCircle(centerX, centerZ, radius, isHole);
    }
    void uploadHoleMaskToGPU() { heightMap->uploadHoleMaskToGPU(); }
    const uint8_t* getHoleMaskData() const { return heightMap->getHoleMaskData(); }

    // Tile cache accessor for physics integration (returns nullptr if not enabled)
    TerrainTileCache* getTileCache() { return tileCache.get(); }
    const TerrainTileCache* getTileCache() const { return tileCache.get(); }

    // Tile cache GPU resource accessors (for grass/other systems)
    VkImageView getTileArrayView() const {
        return tileCache ? tileCache->getTileArrayView() : VK_NULL_HANDLE;
    }
    VkSampler getTileSampler() const {
        return tileCache ? tileCache->getSampler() : VK_NULL_HANDLE;
    }
    // DEPRECATED: Use getTileInfoBuffer(frameIndex) to avoid CPU-GPU sync issues
    [[deprecated("Use getTileInfoBuffer(uint32_t frameIndex) to avoid CPU-GPU sync issues")]]
    VkBuffer getTileInfoBuffer() const {
        return tileCache ? tileCache->getTileInfoBuffer(0) : VK_NULL_HANDLE;
    }
    VkBuffer getTileInfoBuffer(uint32_t frameIndex) const {
        return tileCache ? tileCache->getTileInfoBuffer(frameIndex) : VK_NULL_HANDLE;
    }

    // Toggle wireframe mode for debugging
    void setWireframeMode(bool enabled) { wireframeMode = enabled; }
    bool isWireframeMode() const { return wireframeMode; }

    // Meshlet control
    void setMeshletsEnabled(bool enabled) { config.useMeshlets = enabled; }
    bool isMeshletsEnabled() const { return config.useMeshlets; }
    uint32_t getMeshletTriangleCount() const { return meshlet ? meshlet->getTriangleCount() : 0; }
    int getMeshletSubdivisionLevel() const { return config.meshletSubdivisionLevel; }
    bool setMeshletSubdivisionLevel(int level);  // Returns true if successful, reinitializes meshlet

private:
    TerrainSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info, const TerrainConfig& config);
    bool initInternal(const InitContext& ctx, const TerrainInitParams& params, const TerrainConfig& config);
    void cleanup();

    // Descriptor set creation
    bool createComputeDescriptorSetLayout();
    bool createRenderDescriptorSetLayout();
    bool createDescriptorSets();

    // Utility functions
    void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);
    void querySubgroupCapabilities();

    // Vulkan resources
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkExtent2D extent = {0, 0};
    uint32_t shadowMapSize = 0;
    std::string shaderPath;
    std::string texturePath;
    uint32_t framesInFlight = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Composed subsystems (RAII-managed)
    std::unique_ptr<TerrainHeightMap> heightMap;
    std::optional<RAIIAdapter<TerrainTextures>> textures;
    std::optional<RAIIAdapter<TerrainCBT>> cbt;
    std::unique_ptr<TerrainMeshlet> meshlet;
    std::unique_ptr<TerrainTileCache> tileCache;  // LOD-based tile streaming
    std::optional<RAIIAdapter<TerrainBuffers>> buffers;      // Uniform, indirect, and visibility buffers
    TerrainCameraOptimizer cameraOptimizer;                  // Skip-frame optimization (no destroy needed)
    std::unique_ptr<TerrainPipelines> pipelines;  // All compute and graphics pipelines

    // Descriptor set layouts (needed for descriptor allocation)
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout renderDescriptorSetLayout = VK_NULL_HANDLE;

    // Descriptor sets
    std::vector<VkDescriptorSet> computeDescriptorSets;  // Per frame
    std::vector<VkDescriptorSet> renderDescriptorSets;   // Per frame
    std::vector<VkDescriptorSet> shadowDescriptorSets;   // Per frame

    // Configuration
    TerrainConfig config;
    bool wireframeMode = false;
    bool gpuCullingEnabled = true;             // GPU frustum culling for split phase
    bool shadowCullingEnabled = true;          // GPU frustum culling for shadow cascades

    // Runtime state
    uint32_t currentNodeCount = 2;  // Start with 2 root triangles
    uint32_t subdivisionFrameCount = 0;  // Frame counter for split/merge ping-pong
    SubgroupCapabilities subgroupCaps;  // GPU subgroup feature support

    // Constants
    static constexpr uint32_t SUBDIVISION_WORKGROUP_SIZE = 64;
    static constexpr uint32_t SUM_REDUCTION_WORKGROUP_SIZE = 256;
    static constexpr uint32_t FRUSTUM_CULL_WORKGROUP_SIZE = 256;
    static constexpr uint32_t MAX_VISIBLE_TRIANGLES = 4 * 1024 * 1024;  // 4M triangles max
};
