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
    float padding;
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
                              const std::vector<VkBuffer>& windBuffers);

    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj);
    void recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time);
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time);
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex);

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
    std::vector<VkDescriptorSet> shadowDescriptorSets;

    // Storage buffers (per frame)
    std::vector<VkBuffer> instanceBuffers;
    std::vector<VmaAllocation> instanceAllocations;
    std::vector<VkBuffer> indirectBuffers;
    std::vector<VmaAllocation> indirectAllocations;

    // Uniform buffers for culling (per frame)
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformAllocations;
    std::vector<void*> uniformMappedPtrs;

    // Descriptor sets
    std::vector<VkDescriptorSet> computeDescriptorSets;
    std::vector<VkDescriptorSet> graphicsDescriptorSets;

    static constexpr uint32_t MAX_INSTANCES = 100000;  // ~100k rendered after culling
};
