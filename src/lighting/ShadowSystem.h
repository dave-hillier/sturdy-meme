#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <string>
#include <functional>
#include <memory>

#include "Camera.h"
#include "InitContext.h"
#include "Light.h"
#include "RenderableBuilder.h"
#include "SkinnedMesh.h"
#include "VulkanResourceFactory.h"

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

    /**
     * Factory: Create and initialize shadow system.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<ShadowSystem> create(const InitInfo& info);
    static std::unique_ptr<ShadowSystem> create(const InitContext& ctx,
                                                 VkDescriptorSetLayout mainDescriptorSetLayout,
                                                 VkDescriptorSetLayout skinnedDescriptorSetLayout = VK_NULL_HANDLE);

    ~ShadowSystem();

    // Non-copyable, non-movable (stored via unique_ptr)
    ShadowSystem(ShadowSystem&&) = delete;
    ShadowSystem& operator=(ShadowSystem&&) = delete;
    ShadowSystem(const ShadowSystem&) = delete;
    ShadowSystem& operator=(const ShadowSystem&) = delete;

    // Update cascade matrices based on light direction and camera
    void updateCascadeMatrices(const glm::vec3& lightDir, const Camera& camera);

    // Record shadow pass for all cascades
    // Callback signature: void(VkCommandBuffer cmd, uint32_t cascade, const glm::mat4& lightMatrix)
    using DrawCallback = std::function<void(VkCommandBuffer, uint32_t, const glm::mat4&)>;
    // Pre-cascade compute callback: runs BEFORE each cascade's render pass (for GPU culling)
    // Signature: void(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t cascade, const glm::mat4& lightMatrix)
    using ComputeCallback = std::function<void(VkCommandBuffer, uint32_t, uint32_t, const glm::mat4&)>;
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex,
                          VkDescriptorSet descriptorSet,
                          const std::vector<Renderable>& sceneObjects,
                          const DrawCallback& terrainDrawCallback,
                          const DrawCallback& grassDrawCallback,
                          const DrawCallback& treeDrawCallback = nullptr,
                          const DrawCallback& skinnedDrawCallback = nullptr,
                          const ComputeCallback& preCascadeComputeCallback = nullptr);

    // Record skinned mesh shadow for a single cascade (called after bindSkinnedShadowPipeline)
    void recordSkinnedMeshShadow(VkCommandBuffer cmd, uint32_t cascade,
                                  const glm::mat4& modelMatrix,
                                  const SkinnedMesh& mesh);

    // Bind the skinned shadow pipeline (call once, then record multiple skinned meshes)
    void bindSkinnedShadowPipeline(VkCommandBuffer cmd, VkDescriptorSet descriptorSet);

    // CSM resource accessors (for binding in main shader)
    VkImageView getShadowImageView() const { return csmResources.arrayView; }
    VkSampler getShadowSampler() const { return csmResources.sampler.get(); }
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
    VkImageView getPointShadowArrayView(uint32_t frameIndex) const { return pointShadowResources[frameIndex].arrayView; }
    VkSampler getPointShadowSampler() const { return pointShadowResources.empty() ? VK_NULL_HANDLE : pointShadowResources[0].sampler.get(); }
    VkImageView getSpotShadowArrayView(uint32_t frameIndex) const { return spotShadowResources[frameIndex].arrayView; }
    VkSampler getSpotShadowSampler() const { return spotShadowResources.empty() ? VK_NULL_HANDLE : spotShadowResources[0].sampler.get(); }

    // Dynamic shadow rendering (placeholder for future implementation)
    void renderDynamicShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                              VkDescriptorSet descriptorSet,
                              const std::vector<Renderable>& sceneObjects,
                              const DrawCallback& terrainDrawCallback,
                              const DrawCallback& grassDrawCallback,
                              const DrawCallback& skinnedDrawCallback,
                              const std::vector<Light>& visibleLights);

private:
    ShadowSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    // CSM creation methods
    bool createShadowResources();
    bool createShadowRenderPass();
    bool createShadowPipeline();
    bool createSkinnedShadowPipeline();

    // Dynamic shadow creation methods
    bool createDynamicShadowResources();
    bool createDynamicShadowPipeline();
    void destroyDynamicShadowResources();

    // Pipeline helper
    bool createShadowPipelineCommon(
        const std::string& vertShader,
        const std::string& fragShader,
        VkDescriptorSetLayout descriptorSetLayout,
        const VkVertexInputBindingDescription& binding,
        const std::vector<VkVertexInputAttributeDescription>& attributes,
        VkPipelineLayout& outLayout,
        VkPipeline& outPipeline);

    // Draw helper
    void drawShadowScene(
        VkCommandBuffer cmd,
        VkPipelineLayout layout,
        uint32_t cascadeOrFaceIndex,
        const glm::mat4& lightMatrix,
        const std::vector<Renderable>& sceneObjects,
        const DrawCallback& terrainCallback,
        const DrawCallback& grassCallback,
        const DrawCallback& treeCallback,
        const DrawCallback& skinnedCallback);

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
    VulkanResourceFactory::DepthArrayResources csmResources;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> cascadeFramebuffers;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;

    // CSM cascade data
    std::vector<float> cascadeSplitDepths;
    std::array<glm::mat4, NUM_SHADOW_CASCADES> cascadeMatrices;

    // Dynamic light shadow maps
    static constexpr uint32_t DYNAMIC_SHADOW_MAP_SIZE = 1024;
    static constexpr uint32_t MAX_SHADOW_CASTING_LIGHTS = 8;

    // Point light shadows (cube maps) - per frame
    std::vector<VulkanResourceFactory::DepthArrayResources> pointShadowResources;
    std::vector<std::vector<VkFramebuffer>> pointShadowFramebuffers;  // [frame][face]

    // Spot light shadows (2D depth textures) - per frame
    std::vector<VulkanResourceFactory::DepthArrayResources> spotShadowResources;
    std::vector<std::vector<VkFramebuffer>> spotShadowFramebuffers;   // [frame][light]

    VkPipeline dynamicShadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout dynamicShadowPipelineLayout = VK_NULL_HANDLE;

    // Skinned mesh shadow pipeline (for GPU-skinned characters)
    VkPipeline skinnedShadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skinnedShadowPipelineLayout = VK_NULL_HANDLE;
};
