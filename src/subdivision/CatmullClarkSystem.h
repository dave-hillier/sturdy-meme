#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include "UBOs.h"
#include "CatmullClarkCBT.h"
#include "CatmullClarkMesh.h"
#include "DescriptorManager.h"

// Push constants for rendering
struct CatmullClarkPushConstants {
    glm::mat4 model;
};

// Push constants for subdivision compute shader
struct CatmullClarkSubdivisionPushConstants {
    float targetEdgePixels;
    float splitThreshold;
    float mergeThreshold;
    uint32_t padding;
};

// Catmull-Clark configuration
struct CatmullClarkConfig {
    glm::vec3 position = glm::vec3(0.0f, 3.0f, 0.0f);  // World position
    glm::vec3 scale = glm::vec3(2.0f);                  // Scale
    float targetEdgePixels = 12.0f;                     // Target triangle edge length in pixels
    int maxDepth = 16;                                  // Maximum subdivision depth
    float splitThreshold = 18.0f;                       // Screen pixels to trigger split
    float mergeThreshold = 6.0f;                        // Screen pixels to trigger merge
    std::string objPath;                                // Optional OBJ file path (empty = use cube)
};

class CatmullClarkSystem {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkRenderPass renderPass;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
    };

    CatmullClarkSystem() = default;
    ~CatmullClarkSystem() = default;

    bool init(const InitInfo& info, const CatmullClarkConfig& config = {});
    void destroy(VkDevice device, VmaAllocator allocator);

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { extent = newExtent; }

    // Update descriptor sets with shared resources
    void updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& sceneUniformBuffers);

    // Update uniforms for a frame
    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                        const glm::mat4& view, const glm::mat4& proj);

    // Record compute commands (subdivision update)
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex);

    // Record rendering
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex);

    // Config accessors
    const CatmullClarkConfig& getConfig() const { return config; }
    void setConfig(const CatmullClarkConfig& newConfig) { config = newConfig; }

    // Toggle wireframe mode
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
    bool createSubdivisionPipeline();
    bool createRenderPipeline();
    bool createWireframePipeline();

    // Vulkan resources
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Composed subsystems
    CatmullClarkCBT cbt;
    CatmullClarkMesh mesh;

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
    VkPipelineLayout subdivisionPipelineLayout = VK_NULL_HANDLE;
    VkPipeline subdivisionPipeline = VK_NULL_HANDLE;

    // Render pipelines
    VkDescriptorSetLayout renderDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout renderPipelineLayout = VK_NULL_HANDLE;
    VkPipeline renderPipeline = VK_NULL_HANDLE;
    VkPipeline wireframePipeline = VK_NULL_HANDLE;

    // Descriptor sets
    std::vector<VkDescriptorSet> computeDescriptorSets;  // Per frame
    std::vector<VkDescriptorSet> renderDescriptorSets;   // Per frame

    // Configuration
    CatmullClarkConfig config;
    bool wireframeMode = false;

    // Constants
    static constexpr uint32_t SUBDIVISION_WORKGROUP_SIZE = 64;
};
