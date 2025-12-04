#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <string>
#include <functional>

#include "Camera.h"
#include "RenderableBuilder.h"
#include "SkinnedMesh.h"

// Number of cascades for CSM
static constexpr uint32_t NUM_SHADOW_CASCADES = 4;

// Push constants for shadow rendering
struct ShadowPushConstants {
    glm::mat4 model;
    int cascadeIndex;  // Which cascade we're rendering to
    int padding[3];    // Padding to align
};

class ShadowSystem {
public:
    // Configuration for shadow system initialization
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkDescriptorSetLayout mainDescriptorSetLayout;  // For pipeline compatibility
        VkDescriptorSetLayout skinnedDescriptorSetLayout = VK_NULL_HANDLE;  // For skinned shadow pipeline (optional)
        std::string shaderPath;
        uint32_t framesInFlight;
    };

    ShadowSystem() = default;
    ~ShadowSystem() = default;

    bool init(const InitInfo& info);
    void destroy();

    // Update cascade matrices based on light direction and camera
    void updateCascadeMatrices(const glm::vec3& lightDir, const Camera& camera);

    // Record shadow pass for all cascades
    // Callback signature: void(VkCommandBuffer cmd, uint32_t cascade, const glm::mat4& lightMatrix)
    using DrawCallback = std::function<void(VkCommandBuffer, uint32_t, const glm::mat4&)>;
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex,
                          VkDescriptorSet descriptorSet,
                          const std::vector<Renderable>& sceneObjects,
                          const DrawCallback& terrainDrawCallback,
                          const DrawCallback& grassDrawCallback,
                          const DrawCallback& skinnedDrawCallback = nullptr);

    // Record skinned mesh shadow for a single cascade (called after bindSkinnedShadowPipeline)
    void recordSkinnedMeshShadow(VkCommandBuffer cmd, uint32_t cascade,
                                  const glm::mat4& modelMatrix,
                                  const SkinnedMesh& mesh);

    // Bind the skinned shadow pipeline (call once, then record multiple skinned meshes)
    void bindSkinnedShadowPipeline(VkCommandBuffer cmd, VkDescriptorSet descriptorSet);

    // CSM resource accessors (for binding in main shader)
    VkImageView getShadowImageView() const { return shadowImageView; }
    VkSampler getShadowSampler() const { return shadowSampler; }
    VkRenderPass getShadowRenderPass() const { return shadowRenderPass; }
    VkPipeline getShadowPipeline() const { return shadowPipeline; }
    VkPipelineLayout getShadowPipelineLayout() const { return shadowPipelineLayout; }
    VkPipeline getSkinnedShadowPipeline() const { return skinnedShadowPipeline; }
    VkPipelineLayout getSkinnedShadowPipelineLayout() const { return skinnedShadowPipelineLayout; }

    // Cascade data accessors
    const std::array<glm::mat4, NUM_SHADOW_CASCADES>& getCascadeMatrices() const { return cascadeMatrices; }
    const std::vector<float>& getCascadeSplitDepths() const { return cascadeSplitDepths; }
    uint32_t getShadowMapSize() const { return SHADOW_MAP_SIZE; }

    // Dynamic shadow resource accessors (for binding in main shader)
    VkImageView getPointShadowArrayView(uint32_t frameIndex) const { return pointShadowArrayViews[frameIndex]; }
    VkSampler getPointShadowSampler() const { return pointShadowSampler; }
    VkImageView getSpotShadowArrayView(uint32_t frameIndex) const { return spotShadowArrayViews[frameIndex]; }
    VkSampler getSpotShadowSampler() const { return spotShadowSampler; }

    // Dynamic shadow rendering (placeholder for future implementation)
    void renderDynamicShadows(VkCommandBuffer cmd, uint32_t frameIndex);

private:
    // CSM creation methods
    bool createShadowResources();
    bool createShadowRenderPass();
    bool createShadowPipeline();
    bool createSkinnedShadowPipeline();

    // Dynamic shadow creation methods
    bool createDynamicShadowResources();
    bool createDynamicShadowRenderPass();
    bool createDynamicShadowPipeline();
    void destroyDynamicShadowResources();

    // Cascade calculation methods
    void calculateCascadeSplits(float nearClip, float farClip, float lambda, std::vector<float>& splits);
    glm::mat4 calculateCascadeMatrix(const glm::vec3& lightDir, const Camera& camera, float nearSplit, float farSplit);

    // Vulkan handles (not owned)
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDescriptorSetLayout mainDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skinnedDescriptorSetLayout = VK_NULL_HANDLE;
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    // CSM shadow map resources
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
    VkImage shadowImage = VK_NULL_HANDLE;
    VmaAllocation shadowImageAllocation = VK_NULL_HANDLE;
    VkImageView shadowImageView = VK_NULL_HANDLE;  // Array view for sampling
    std::array<VkImageView, NUM_SHADOW_CASCADES> cascadeImageViews{};  // Per-layer views for rendering
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, NUM_SHADOW_CASCADES> cascadeFramebuffers{};  // Per-cascade framebuffers
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;

    // CSM cascade data
    std::vector<float> cascadeSplitDepths;
    std::array<glm::mat4, NUM_SHADOW_CASCADES> cascadeMatrices;

    // Dynamic light shadow maps
    static constexpr uint32_t DYNAMIC_SHADOW_MAP_SIZE = 1024;
    static constexpr uint32_t MAX_SHADOW_CASTING_LIGHTS = 8;  // Max lights that can cast shadows per frame

    // Point light shadows (cube maps)
    std::vector<VkImage> pointShadowImages;              // Per-frame cube map arrays
    std::vector<VmaAllocation> pointShadowAllocations;
    std::vector<VkImageView> pointShadowArrayViews;      // Array view for all cube maps
    std::vector<std::array<VkImageView, 6>> pointShadowFaceViews;  // Per-face views for rendering [frame][face]
    VkSampler pointShadowSampler = VK_NULL_HANDLE;

    // Spot light shadows (2D depth textures)
    std::vector<VkImage> spotShadowImages;               // Per-frame texture arrays
    std::vector<VmaAllocation> spotShadowAllocations;
    std::vector<VkImageView> spotShadowArrayViews;       // Array view for all textures
    std::vector<std::vector<VkImageView>> spotShadowLayerViews;  // Per-layer views [frame][light]
    VkSampler spotShadowSampler = VK_NULL_HANDLE;

    VkRenderPass shadowRenderPassDynamic = VK_NULL_HANDLE;  // Render pass for dynamic shadows
    std::vector<std::vector<VkFramebuffer>> pointShadowFramebuffers;  // [frame][face]
    std::vector<std::vector<VkFramebuffer>> spotShadowFramebuffers;   // [frame][light]

    VkPipeline dynamicShadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout dynamicShadowPipelineLayout = VK_NULL_HANDLE;

    // Skinned mesh shadow pipeline (for GPU-skinned characters)
    VkPipeline skinnedShadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skinnedShadowPipelineLayout = VK_NULL_HANDLE;
};
