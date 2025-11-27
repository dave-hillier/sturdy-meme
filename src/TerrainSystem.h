#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>

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

// Push constants for sum reduction
struct TerrainSumReductionPushConstants {
    int passID;
};

// Uniform buffer for terrain system
struct TerrainUniforms {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::vec4 frustumPlanes[6];
    glm::vec4 cameraPosition;
    glm::vec4 terrainParams;   // x = size, y = height scale, z = target edge pixels, w = max depth
    glm::vec4 lodParams;       // x = split threshold, y = merge threshold, z = min depth, w = unused
    glm::vec2 screenSize;
    float lodFactor;
    float padding;
};

// Terrain configuration (outside class to avoid C++17 default argument issues)
struct TerrainConfig {
    float size = 500.0f;              // Terrain size in world units
    float heightScale = 50.0f;        // Maximum height
    float targetEdgePixels = 16.0f;   // Target triangle edge length in pixels
    int maxDepth = 20;                // Maximum CBT subdivision depth
    int minDepth = 2;                 // Minimum subdivision depth
    float splitThreshold = 24.0f;     // Screen pixels to trigger split (with hysteresis)
    float mergeThreshold = 8.0f;      // Screen pixels to trigger merge
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

    // Update terrain uniforms for a frame
    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                        const glm::mat4& view, const glm::mat4& proj);

    // Record compute commands (subdivision update)
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex);

    // Record terrain rendering
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex);

    // Record shadow pass for terrain
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                          const glm::mat4& lightViewProj, int cascadeIndex);

    // Get terrain height at world position (CPU-side, for collision)
    float getHeightAt(float x, float z) const;

    // Get current node count (for debugging/display)
    uint32_t getNodeCount() const { return currentNodeCount; }

    // Config accessors
    const TerrainConfig& getConfig() const { return config; }
    void setConfig(const TerrainConfig& newConfig) { config = newConfig; }

    // Toggle wireframe mode for debugging
    void setWireframeMode(bool enabled) { wireframeMode = enabled; }
    bool isWireframeMode() const { return wireframeMode; }

private:
    // Initialization helpers
    bool createCBTBuffer();
    bool initializeCBT();
    bool createHeightMap();
    bool createTerrainTexture();
    bool loadOrGenerateHeightMap();
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
    uint32_t calculateCBTBufferSize(int maxDepth);
    bool uploadImageData(VkImage image, const void* data, uint32_t width, uint32_t height,
                         VkFormat format, uint32_t bytesPerPixel);

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

    // CBT (Concurrent Binary Tree) buffer
    VkBuffer cbtBuffer = VK_NULL_HANDLE;
    VmaAllocation cbtAllocation = VK_NULL_HANDLE;
    uint32_t cbtBufferSize = 0;

    // Height map texture
    VkImage heightMapImage = VK_NULL_HANDLE;
    VmaAllocation heightMapAllocation = VK_NULL_HANDLE;
    VkImageView heightMapView = VK_NULL_HANDLE;
    VkSampler heightMapSampler = VK_NULL_HANDLE;
    std::vector<float> cpuHeightMap;  // CPU-side copy for collision queries
    uint32_t heightMapResolution = 512;

    // Terrain albedo texture
    VkImage terrainAlbedoImage = VK_NULL_HANDLE;
    VmaAllocation terrainAlbedoAllocation = VK_NULL_HANDLE;
    VkImageView terrainAlbedoView = VK_NULL_HANDLE;
    VkSampler terrainAlbedoSampler = VK_NULL_HANDLE;

    // Indirect dispatch/draw buffers
    VkBuffer indirectDispatchBuffer = VK_NULL_HANDLE;
    VmaAllocation indirectDispatchAllocation = VK_NULL_HANDLE;
    VkBuffer indirectDrawBuffer = VK_NULL_HANDLE;
    VmaAllocation indirectDrawAllocation = VK_NULL_HANDLE;

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
    VkPipeline sumReductionPipeline = VK_NULL_HANDLE;

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

    // Constants
    static constexpr uint32_t SUBDIVISION_WORKGROUP_SIZE = 64;
    static constexpr uint32_t SUM_REDUCTION_WORKGROUP_SIZE = 256;
};
