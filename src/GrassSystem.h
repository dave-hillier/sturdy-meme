#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

struct GrassPushConstants {
    float time;
    int cascadeIndex;  // For shadow pass: which cascade we're rendering
};

struct GrassUniforms {
    glm::vec4 cameraPosition;      // xyz = position, w = unused
    glm::vec4 frustumPlanes[6];    // 6 frustum planes (ax+by+cz+d form)
    float maxDrawDistance;          // Max distance for grass rendering
    float lodTransitionStart;       // Distance where LOD transition begins
    float lodTransitionEnd;         // Distance where LOD transition ends
    float terrainSize;              // Terrain size for heightmap UV calculation
    float terrainHeightScale;       // Terrain height scale
    float padding[3];
};

struct GrassInstance {
    glm::vec4 positionAndFacing;  // xyz = position, w = facing angle
    glm::vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = unused
};

class GrassSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkRenderPass renderPass;
        VkRenderPass shadowRenderPass;
        VkDescriptorPool descriptorPool;
        VkExtent2D extent;
        uint32_t shadowMapSize;
        std::string shaderPath;
        uint32_t framesInFlight;
    };

    GrassSystem() = default;
    ~GrassSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    void updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& uniformBuffers,
                              VkImageView shadowMapView, VkSampler shadowSampler,
                              const std::vector<VkBuffer>& windBuffers,
                              const std::vector<VkBuffer>& lightBuffers,
                              VkImageView terrainHeightMapView, VkSampler terrainHeightMapSampler);

    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                        float terrainSize, float terrainHeightScale);
    void recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time);
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time);
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex);

    // Double-buffer management: call at frame start to swap compute/render buffer sets
    void advanceBufferSet();

private:
    bool createShadowPipeline();
    bool createBuffers();
    bool createComputeDescriptorSetLayout();
    bool createComputePipeline();
    bool createGraphicsDescriptorSetLayout();
    bool createGraphicsPipeline();
    bool createDescriptorSets();

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    uint32_t shadowMapSize = 0;
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    // Compute pipeline
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;

    // Graphics pipeline
    VkDescriptorSetLayout graphicsDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout graphicsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    // Shadow pipeline (for casting shadows)
    VkDescriptorSetLayout shadowDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;

    // Double-buffered storage buffers: A/B sets that alternate each frame
    // Set A and Set B alternate: compute writes to one while graphics reads from other
    // Note: We don't need per-frame copies since the set alternation provides isolation
    static constexpr uint32_t BUFFER_SET_COUNT = 2;
    VkBuffer instanceBuffers[BUFFER_SET_COUNT];      // [setIndex]
    VmaAllocation instanceAllocations[BUFFER_SET_COUNT];
    VkBuffer indirectBuffers[BUFFER_SET_COUNT];
    VmaAllocation indirectAllocations[BUFFER_SET_COUNT];

    // Uniform buffers for culling (per frame, not double-buffered)
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformAllocations;
    std::vector<void*> uniformMappedPtrs;

    // Descriptor sets: [2] for A/B buffer sets, one per set
    // Per-frame descriptor sets for uniform buffers that DO need per-frame copies
    VkDescriptorSet computeDescriptorSets[BUFFER_SET_COUNT];
    VkDescriptorSet graphicsDescriptorSets[BUFFER_SET_COUNT];
    VkDescriptorSet shadowDescriptorSetsDB[BUFFER_SET_COUNT];

    // Double-buffer state: which set is being computed vs rendered
    // Both start at 0 for bootstrap (first frame uses same buffer for compute+render)
    // After first advanceBufferSet(), they diverge for true double-buffering
    uint32_t computeBufferSet = 0;  // Set being written by compute
    uint32_t renderBufferSet = 0;   // Set being read by graphics

    // Terrain heightmap for grass placement (stored for compute descriptor updates)
    VkImageView terrainHeightMapView = VK_NULL_HANDLE;
    VkSampler terrainHeightMapSampler = VK_NULL_HANDLE;

    static constexpr uint32_t MAX_INSTANCES = 100000;  // ~100k rendered after culling
};
