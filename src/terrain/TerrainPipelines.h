#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <memory>
#include "VulkanRAII.h"

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

    /**
     * Factory: Create and initialize TerrainPipelines.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<TerrainPipelines> create(const InitInfo& info);

    ~TerrainPipelines() = default;

    // Non-copyable, non-movable
    TerrainPipelines(const TerrainPipelines&) = delete;
    TerrainPipelines& operator=(const TerrainPipelines&) = delete;
    TerrainPipelines(TerrainPipelines&&) = delete;
    TerrainPipelines& operator=(TerrainPipelines&&) = delete;

    // Compute pipeline accessors
    VkPipelineLayout getDispatcherPipelineLayout() const { return dispatcherPipelineLayout_.get(); }
    VkPipeline getDispatcherPipeline() const { return dispatcherPipeline_.get(); }

    VkPipelineLayout getSubdivisionPipelineLayout() const { return subdivisionPipelineLayout_.get(); }
    VkPipeline getSubdivisionPipeline() const { return subdivisionPipeline_.get(); }

    VkPipelineLayout getSumReductionPipelineLayout() const { return sumReductionPipelineLayout_.get(); }
    VkPipeline getSumReductionPrepassPipeline() const { return sumReductionPrepassPipeline_.get(); }
    VkPipeline getSumReductionPrepassSubgroupPipeline() const { return sumReductionPrepassSubgroupPipeline_.get(); }
    VkPipeline getSumReductionPipeline() const { return sumReductionPipeline_.get(); }

    VkPipelineLayout getSumReductionBatchedPipelineLayout() const { return sumReductionBatchedPipelineLayout_.get(); }
    VkPipeline getSumReductionBatchedPipeline() const { return sumReductionBatchedPipeline_.get(); }

    VkPipelineLayout getFrustumCullPipelineLayout() const { return frustumCullPipelineLayout_.get(); }
    VkPipeline getFrustumCullPipeline() const { return frustumCullPipeline_.get(); }

    VkPipelineLayout getPrepareDispatchPipelineLayout() const { return prepareDispatchPipelineLayout_.get(); }
    VkPipeline getPrepareDispatchPipeline() const { return prepareDispatchPipeline_.get(); }

    // Render pipeline accessors
    VkPipelineLayout getRenderPipelineLayout() const { return renderPipelineLayout_.get(); }
    VkPipeline getRenderPipeline() const { return renderPipeline_.get(); }
    VkPipeline getWireframePipeline() const { return wireframePipeline_.get(); }
    VkPipeline getMeshletRenderPipeline() const { return meshletRenderPipeline_.get(); }
    VkPipeline getMeshletWireframePipeline() const { return meshletWireframePipeline_.get(); }

    // Shadow pipeline accessors
    VkPipelineLayout getShadowPipelineLayout() const { return shadowPipelineLayout_.get(); }
    VkPipeline getShadowPipeline() const { return shadowPipeline_.get(); }
    VkPipeline getMeshletShadowPipeline() const { return meshletShadowPipeline_.get(); }

    // Shadow culling pipeline accessors
    VkPipelineLayout getShadowCullPipelineLayout() const { return shadowCullPipelineLayout_.get(); }
    VkPipeline getShadowCullPipeline() const { return shadowCullPipeline_.get(); }
    VkPipeline getShadowCulledPipeline() const { return shadowCulledPipeline_.get(); }
    VkPipeline getMeshletShadowCulledPipeline() const { return meshletShadowCulledPipeline_.get(); }

    // Check if shadow culling is available
    bool hasShadowCulling() const { return static_cast<bool>(shadowCullPipeline_); }

private:
    TerrainPipelines() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);

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
    ManagedPipelineLayout dispatcherPipelineLayout_;
    ManagedPipeline dispatcherPipeline_;

    ManagedPipelineLayout subdivisionPipelineLayout_;
    ManagedPipeline subdivisionPipeline_;

    ManagedPipelineLayout sumReductionPipelineLayout_;
    ManagedPipeline sumReductionPrepassPipeline_;
    ManagedPipeline sumReductionPrepassSubgroupPipeline_;
    ManagedPipeline sumReductionPipeline_;

    ManagedPipelineLayout sumReductionBatchedPipelineLayout_;
    ManagedPipeline sumReductionBatchedPipeline_;

    ManagedPipelineLayout frustumCullPipelineLayout_;
    ManagedPipeline frustumCullPipeline_;

    ManagedPipelineLayout prepareDispatchPipelineLayout_;
    ManagedPipeline prepareDispatchPipeline_;

    // Render pipelines
    ManagedPipelineLayout renderPipelineLayout_;
    ManagedPipeline renderPipeline_;
    ManagedPipeline wireframePipeline_;
    ManagedPipeline meshletRenderPipeline_;
    ManagedPipeline meshletWireframePipeline_;

    // Shadow pipelines
    ManagedPipelineLayout shadowPipelineLayout_;
    ManagedPipeline shadowPipeline_;
    ManagedPipeline meshletShadowPipeline_;

    // Shadow culling pipelines
    ManagedPipelineLayout shadowCullPipelineLayout_;
    ManagedPipeline shadowCullPipeline_;
    ManagedPipeline shadowCulledPipeline_;
    ManagedPipeline meshletShadowCulledPipeline_;
};
