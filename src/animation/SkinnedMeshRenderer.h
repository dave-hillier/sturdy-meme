#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>

#include "SkinnedMesh.h"
#include "DescriptorManager.h"
#include "MaterialDescriptorFactory.h"
#include "RenderableBuilder.h"
#include "GlobalBufferManager.h"

class AnimatedCharacter;

// Skinned mesh renderer - handles GPU skinning pipeline and bone matrices
class SkinnedMeshRenderer {
public:
    // Callback type for adding common descriptor bindings
    using AddCommonBindingsCallback = std::function<void(DescriptorManager::LayoutBuilder&)>;

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        VkRenderPass renderPass;  // HDR render pass
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        AddCommonBindingsCallback addCommonBindings;
    };

    // Resources needed for descriptor set writing
    struct DescriptorResources {
        const GlobalBufferManager* globalBufferManager;

        // Shadow system resources
        VkImageView shadowMapView;
        VkSampler shadowMapSampler;
        VkImageView emissiveMapView;
        VkSampler emissiveMapSampler;
        std::vector<VkImageView>* pointShadowViews;  // Per-frame
        VkSampler pointShadowSampler;
        std::vector<VkImageView>* spotShadowViews;   // Per-frame
        VkSampler spotShadowSampler;
        VkImageView snowMaskView;
        VkSampler snowMaskSampler;

        // Placeholder textures
        VkImageView whiteTextureView;
        VkSampler whiteTextureSampler;
    };

    SkinnedMeshRenderer() = default;
    ~SkinnedMeshRenderer() = default;

    bool init(const InitInfo& info);
    void destroy();

    // Create descriptor sets after all resources are ready
    bool createDescriptorSets(const DescriptorResources& resources);

    // Update cloud shadow binding after cloud shadow system is initialized
    void updateCloudShadowBinding(VkImageView cloudShadowView, VkSampler cloudShadowSampler);

    // Update bone matrices from animated character
    void updateBoneMatrices(uint32_t frameIndex, AnimatedCharacter* character);

    // Record draw commands for skinned character
    void record(VkCommandBuffer cmd, uint32_t frameIndex,
                const Renderable& playerObj, AnimatedCharacter& character);

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { extent = newExtent; }

    // Accessors for ShadowSystem integration
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkPipeline getPipeline() const { return pipeline; }
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

private:
    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createBoneMatricesBuffers();

    // Vulkan handles (stored, not owned)
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkExtent2D extent{};
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    AddCommonBindingsCallback addCommonBindings;

    // Created resources
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<VkBuffer> boneMatricesBuffers;
    std::vector<VmaAllocation> boneMatricesAllocations;
    std::vector<void*> boneMatricesMapped;
};
