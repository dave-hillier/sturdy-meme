#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

// Push constants for terrain rendering
struct TerrainPushConstants {
    float terrainSize;
    float heightScale;
    float maxDepth;
    float debugWireframe;
};

// Push constants for terrain shadow pass
struct TerrainShadowPushConstants {
    glm::mat4 lightViewProj;
    float terrainSize;
    float heightScale;
    float maxDepth;
    int cascadeIndex;
};

// Push constants for CBT compute shaders
struct CBTComputePushConstants {
    glm::mat4 viewProj;
    glm::vec4 cameraPos;
    glm::vec4 terrainParams;  // x = terrainSize, y = heightScale, z = splitThreshold, w = mergeThreshold
    glm::vec4 screenParams;   // x = width, y = height, z = maxDepth, w = unused
};

// Push constants for sum reduction
struct SumReductionPushConstants {
    uint32_t passIndex;
    uint32_t maxDepth;
    uint32_t numWorkgroups;
    uint32_t padding;
};

class TerrainCBT {
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
        uint32_t framesInFlight;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
    };

    TerrainCBT() = default;
    ~TerrainCBT() = default;

    bool init(const InitInfo& info);
    void destroy();

    // Update descriptor sets with shared resources (UBO, shadow map)
    void updateDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                              VkImageView shadowMapView, VkSampler shadowSampler);

    // Record compute passes (dispatcher, subdivision, sum reduction)
    void recordComputePass(VkCommandBuffer cmd, uint32_t frameIndex,
                           const glm::mat4& viewProj, const glm::vec3& cameraPos,
                           float screenWidth, float screenHeight);

    // Record render pass
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, bool wireframeDebug);

    // Record shadow pass
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                          const glm::mat4& lightViewProj, int cascadeIndex);

    // Configuration
    void setTerrainSize(float size) { terrainSize = size; }
    void setHeightScale(float scale) { heightScale = scale; }
    void setMaxDepth(uint32_t depth) { maxDepth = depth; }
    void setSplitThreshold(float threshold) { splitThreshold = threshold; }
    void setMergeThreshold(float threshold) { mergeThreshold = threshold; }

    float getTerrainSize() const { return terrainSize; }
    float getHeightScale() const { return heightScale; }
    uint32_t getMaxDepth() const { return maxDepth; }
    uint32_t getLeafCount() const;

    // Height map
    bool loadHeightMap(const std::string& path);
    bool hasHeightMap() const { return heightMapView != VK_NULL_HANDLE; }

private:
    bool createCBTBuffer();
    bool initializeCBT();
    bool createIndirectBuffers();
    bool createHeightMapResources();
    bool createComputePipelines();
    bool createGraphicsPipeline();
    bool createShadowPipeline();
    bool createDescriptorSetLayouts();
    bool createDescriptorSets();

    // Helper to copy CPU data to GPU buffer
    void uploadBufferData(VkBuffer buffer, const void* data, VkDeviceSize size);

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    uint32_t shadowMapSize = 0;
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;

    // Configuration
    float terrainSize = 100.0f;      // World units
    float heightScale = 20.0f;       // Maximum height
    uint32_t maxDepth = 10;          // CBT maximum depth (2^10 = 1024 max triangles per base)
    float splitThreshold = 50.0f;    // Pixels - split if edge > this
    float mergeThreshold = 25.0f;    // Pixels - merge if edge < this

    // CBT buffer (persistent across frames)
    VkBuffer cbtBuffer = VK_NULL_HANDLE;
    VmaAllocation cbtAllocation = VK_NULL_HANDLE;
    VkDeviceSize cbtBufferSize = 0;

    // Indirect dispatch buffer (per frame for double buffering)
    std::vector<VkBuffer> indirectDispatchBuffers;
    std::vector<VmaAllocation> indirectDispatchAllocations;

    // Indirect draw buffer (per frame)
    std::vector<VkBuffer> indirectDrawBuffers;
    std::vector<VmaAllocation> indirectDrawAllocations;

    // Height map texture
    VkImage heightMapImage = VK_NULL_HANDLE;
    VmaAllocation heightMapAllocation = VK_NULL_HANDLE;
    VkImageView heightMapView = VK_NULL_HANDLE;
    VkSampler heightMapSampler = VK_NULL_HANDLE;

    // Compute pipelines
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline dispatcherPipeline = VK_NULL_HANDLE;
    VkPipeline subdivisionPipeline = VK_NULL_HANDLE;
    VkPipeline sumReductionPipeline = VK_NULL_HANDLE;

    // Graphics pipeline
    VkDescriptorSetLayout graphicsDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout graphicsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    // Shadow pipeline
    VkDescriptorSetLayout shadowDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;

    // Descriptor sets
    std::vector<VkDescriptorSet> computeDescriptorSets;
    std::vector<VkDescriptorSet> graphicsDescriptorSets;
    std::vector<VkDescriptorSet> shadowDescriptorSets;

    // CPU-side copy of leaf count for debugging
    mutable uint32_t cachedLeafCount = 2;

    // Default placeholder height map (flat)
    static constexpr uint32_t DEFAULT_HEIGHTMAP_SIZE = 256;
};
