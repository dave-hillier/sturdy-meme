#pragma once

#include <vulkan/vulkan.h>
#include <string>

struct SubgroupCapabilities;
class TerrainMeshlet;

// Manages all Vulkan pipelines for terrain rendering
// Extracted from TerrainSystem to reduce complexity
class TerrainPipelines {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VkRenderPass renderPass;
        VkRenderPass shadowRenderPass;
        VkDescriptorSetLayout computeDescriptorSetLayout;
        VkDescriptorSetLayout renderDescriptorSetLayout;
        std::string shaderPath;
        bool useMeshlets;
        uint32_t meshletIndexCount;  // From TerrainMeshlet::getIndexCount()
        const SubgroupCapabilities* subgroupCaps;  // For optimized compute paths
    };

    TerrainPipelines() = default;
    ~TerrainPipelines() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device);

    // Compute pipeline accessors
    VkPipelineLayout getDispatcherPipelineLayout() const { return dispatcherPipelineLayout; }
    VkPipeline getDispatcherPipeline() const { return dispatcherPipeline; }

    VkPipelineLayout getSubdivisionPipelineLayout() const { return subdivisionPipelineLayout; }
    VkPipeline getSubdivisionPipeline() const { return subdivisionPipeline; }

    VkPipelineLayout getSumReductionPipelineLayout() const { return sumReductionPipelineLayout; }
    VkPipeline getSumReductionPrepassPipeline() const { return sumReductionPrepassPipeline; }
    VkPipeline getSumReductionPrepassSubgroupPipeline() const { return sumReductionPrepassSubgroupPipeline; }
    VkPipeline getSumReductionPipeline() const { return sumReductionPipeline; }

    VkPipelineLayout getSumReductionBatchedPipelineLayout() const { return sumReductionBatchedPipelineLayout; }
    VkPipeline getSumReductionBatchedPipeline() const { return sumReductionBatchedPipeline; }

    VkPipelineLayout getFrustumCullPipelineLayout() const { return frustumCullPipelineLayout; }
    VkPipeline getFrustumCullPipeline() const { return frustumCullPipeline; }

    VkPipelineLayout getPrepareDispatchPipelineLayout() const { return prepareDispatchPipelineLayout; }
    VkPipeline getPrepareDispatchPipeline() const { return prepareDispatchPipeline; }

    // Render pipeline accessors
    VkPipelineLayout getRenderPipelineLayout() const { return renderPipelineLayout; }
    VkPipeline getRenderPipeline() const { return renderPipeline; }
    VkPipeline getWireframePipeline() const { return wireframePipeline; }
    VkPipeline getMeshletRenderPipeline() const { return meshletRenderPipeline; }
    VkPipeline getMeshletWireframePipeline() const { return meshletWireframePipeline; }

    // Shadow pipeline accessors
    VkPipelineLayout getShadowPipelineLayout() const { return shadowPipelineLayout; }
    VkPipeline getShadowPipeline() const { return shadowPipeline; }
    VkPipeline getMeshletShadowPipeline() const { return meshletShadowPipeline; }

    // Shadow culling pipeline accessors
    VkPipelineLayout getShadowCullPipelineLayout() const { return shadowCullPipelineLayout; }
    VkPipeline getShadowCullPipeline() const { return shadowCullPipeline; }
    VkPipeline getShadowCulledPipeline() const { return shadowCulledPipeline; }
    VkPipeline getMeshletShadowCulledPipeline() const { return meshletShadowCulledPipeline; }

    // Check if shadow culling is available
    bool hasShadowCulling() const { return shadowCullPipeline != VK_NULL_HANDLE; }

private:
    // Pipeline creation helpers
    bool createDispatcherPipeline();
    bool createSubdivisionPipeline();
    bool createSumReductionPipelines();
    bool createFrustumCullPipelines();
    bool createRenderPipeline();
    bool createWireframePipeline();
    bool createShadowPipeline();
    bool createMeshletRenderPipeline();
    bool createMeshletWireframePipeline();
    bool createMeshletShadowPipeline();
    bool createShadowCullPipelines();

    // Stored from InitInfo for pipeline creation
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout renderDescriptorSetLayout = VK_NULL_HANDLE;
    std::string shaderPath;
    bool useMeshlets = false;
    uint32_t meshletIndexCount = 0;
    const SubgroupCapabilities* subgroupCaps = nullptr;

    // Compute pipelines
    VkPipelineLayout dispatcherPipelineLayout = VK_NULL_HANDLE;
    VkPipeline dispatcherPipeline = VK_NULL_HANDLE;

    VkPipelineLayout subdivisionPipelineLayout = VK_NULL_HANDLE;
    VkPipeline subdivisionPipeline = VK_NULL_HANDLE;

    VkPipelineLayout sumReductionPipelineLayout = VK_NULL_HANDLE;
    VkPipeline sumReductionPrepassPipeline = VK_NULL_HANDLE;
    VkPipeline sumReductionPrepassSubgroupPipeline = VK_NULL_HANDLE;
    VkPipeline sumReductionPipeline = VK_NULL_HANDLE;

    VkPipelineLayout sumReductionBatchedPipelineLayout = VK_NULL_HANDLE;
    VkPipeline sumReductionBatchedPipeline = VK_NULL_HANDLE;

    VkPipelineLayout frustumCullPipelineLayout = VK_NULL_HANDLE;
    VkPipeline frustumCullPipeline = VK_NULL_HANDLE;

    VkPipelineLayout prepareDispatchPipelineLayout = VK_NULL_HANDLE;
    VkPipeline prepareDispatchPipeline = VK_NULL_HANDLE;

    // Render pipelines
    VkPipelineLayout renderPipelineLayout = VK_NULL_HANDLE;
    VkPipeline renderPipeline = VK_NULL_HANDLE;
    VkPipeline wireframePipeline = VK_NULL_HANDLE;
    VkPipeline meshletRenderPipeline = VK_NULL_HANDLE;
    VkPipeline meshletWireframePipeline = VK_NULL_HANDLE;

    // Shadow pipelines
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkPipeline meshletShadowPipeline = VK_NULL_HANDLE;

    // Shadow culling pipelines
    VkPipelineLayout shadowCullPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowCullPipeline = VK_NULL_HANDLE;
    VkPipeline shadowCulledPipeline = VK_NULL_HANDLE;
    VkPipeline meshletShadowCulledPipeline = VK_NULL_HANDLE;
};
