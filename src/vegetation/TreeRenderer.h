#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "TreeLeafCulling.h"
#include "TreeBranchCulling.h"
#include "VulkanRAII.h"
#include "DescriptorManager.h"

// Push constants for tree rendering
struct TreeBranchPushConstants {
    glm::mat4 model;      // offset 0, size 64
    float time;           // offset 64, size 4
    float lodBlendFactor; // offset 68, size 4 (0=full geometry, 1=full impostor)
    float _pad1[2];       // offset 72, size 8 (padding to align vec3 to 16 bytes)
    glm::vec3 barkTint;   // offset 80, size 12
    float roughnessScale; // offset 92, size 4
};

// Simplified push constants for single-draw leaf rendering
struct TreeLeafPushConstants {
    float time;            // offset 0, size 4
    float alphaTest;       // offset 4, size 4
};

// Shadow pass push constants (branches)
struct TreeBranchShadowPushConstants {
    glm::mat4 model;      // offset 0, size 64
    int cascadeIndex;     // offset 64, size 4
};

// Simplified shadow pass push constants for single-draw leaf rendering
struct TreeLeafShadowPushConstants {
    int32_t cascadeIndex;  // offset 0, size 4
    float alphaTest;       // offset 4, size 4
};

// Push constants for instanced branch shadow rendering
struct TreeBranchShadowInstancedPushConstants {
    uint32_t cascadeIndex;    // offset 0, size 4
    uint32_t instanceOffset;  // offset 4, size 4
};

class TreeRenderer {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkRenderPass hdrRenderPass;
        VkRenderPass shadowRenderPass;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        uint32_t shadowMapSize = 2048;
        std::string resourcePath;
        uint32_t maxFramesInFlight;
    };

    static std::unique_ptr<TreeRenderer> create(const InitInfo& info);
    ~TreeRenderer();

    // Non-copyable, non-movable
    TreeRenderer(const TreeRenderer&) = delete;
    TreeRenderer& operator=(const TreeRenderer&) = delete;
    TreeRenderer(TreeRenderer&&) = delete;
    TreeRenderer& operator=(TreeRenderer&&) = delete;

    // Update descriptor sets for a specific texture type
    void updateBarkDescriptorSet(
        uint32_t frameIndex,
        const std::string& barkType,
        VkBuffer uniformBuffer,
        VkBuffer windBuffer,
        VkImageView shadowMapView,
        VkSampler shadowSampler,
        VkImageView barkAlbedo,
        VkImageView barkNormal,
        VkImageView barkRoughness,
        VkImageView barkAO,
        VkSampler barkSampler);

    void updateLeafDescriptorSet(
        uint32_t frameIndex,
        const std::string& leafType,
        VkBuffer uniformBuffer,
        VkBuffer windBuffer,
        VkImageView shadowMapView,
        VkSampler shadowSampler,
        VkImageView leafAlbedo,
        VkSampler leafSampler,
        VkBuffer leafInstanceBuffer,
        VkDeviceSize leafInstanceBufferSize);

    // Update culled leaf descriptor sets (called after cull buffers are created)
    void updateCulledLeafDescriptorSet(
        uint32_t frameIndex,
        const std::string& leafType,
        VkBuffer uniformBuffer,
        VkBuffer windBuffer,
        VkImageView shadowMapView,
        VkSampler shadowSampler,
        VkImageView leafAlbedo,
        VkSampler leafSampler);

    // Get descriptor set for a specific type (returns default if type not found)
    VkDescriptorSet getBranchDescriptorSet(uint32_t frameIndex, const std::string& barkType) const;
    VkDescriptorSet getLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const;
    VkDescriptorSet getCulledLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const;

    // Record compute pass for leaf culling (call before render pass)
    void recordLeafCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                           const TreeSystem& treeSystem,
                           const TreeLODSystem* lodSystem,
                           const glm::vec3& cameraPos,
                           const glm::vec4* frustumPlanes);

    // Record compute pass for branch shadow culling (call before shadow pass)
    void recordBranchShadowCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                                   uint32_t cascadeIndex,
                                   const glm::vec4* cascadeFrustumPlanes,
                                   const glm::vec3& cameraPos,
                                   const TreeLODSystem* lodSystem);

    // Update branch culling data (call when trees change)
    void updateBranchCullingData(const TreeSystem& treeSystem, const TreeLODSystem* lodSystem);

    // Check if branch shadow culling is available (subsystem initialized)
    bool isBranchShadowCullingAvailable() const;

    // Check if branch shadow culling is enabled (available AND user toggle is on)
    bool isBranchShadowCullingEnabled() const;

    // Enable/disable branch shadow culling
    void setBranchShadowCullingEnabled(bool enabled);

    // Initialize or update spatial index from tree data
    void updateSpatialIndex(const TreeSystem& treeSystem);

    // Check if spatial indexing is enabled
    bool isSpatialIndexEnabled() const;

    // Render all trees (optionally filtering by LOD)
    void render(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                const TreeSystem& treeSystem, const TreeLODSystem* lodSystem = nullptr);

    // Render tree shadows (optionally filtering by LOD)
    void renderShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                       const TreeSystem& treeSystem, int cascadeIndex,
                       const TreeLODSystem* lodSystem = nullptr);

    // Update extent on resize
    void setExtent(VkExtent2D newExtent);

    // Check if leaf culling is enabled
    bool isLeafCullingEnabled() const;

    // Enable/disable two-phase culling
    void setTwoPhaseLeafCulling(bool enabled);
    bool isTwoPhaseLeafCullingEnabled() const;

    VkDevice getDevice() const { return device_; }

private:
    TreeRenderer() = default;
    bool initInternal(const InitInfo& info);
    bool createPipelines(const InitInfo& info);
    bool createDescriptorSetLayout();
    bool allocateDescriptorSets(uint32_t maxFramesInFlight);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    VkExtent2D extent_{};
    uint32_t maxFramesInFlight_ = 0;

    // Graphics Pipelines
    ManagedPipeline branchPipeline_;
    ManagedPipeline leafPipeline_;
    ManagedPipeline branchShadowPipeline_;
    ManagedPipeline leafShadowPipeline_;

    // Pipeline layouts
    ManagedPipelineLayout branchPipelineLayout_;
    ManagedPipelineLayout leafPipelineLayout_;
    ManagedPipelineLayout branchShadowPipelineLayout_;
    ManagedPipelineLayout leafShadowPipelineLayout_;

    // Descriptor set layouts
    ManagedDescriptorSetLayout branchDescriptorSetLayout_;
    ManagedDescriptorSetLayout leafDescriptorSetLayout_;

    // Per-frame descriptor sets (indexed by frame, then by texture type)
    std::vector<std::unordered_map<std::string, VkDescriptorSet>> branchDescriptorSets_;
    std::vector<std::unordered_map<std::string, VkDescriptorSet>> leafDescriptorSets_;

    // Default descriptor sets for types without registered textures
    std::vector<VkDescriptorSet> defaultBranchDescriptorSets_;
    std::vector<VkDescriptorSet> defaultLeafDescriptorSets_;

    // Per-frame, per-type descriptor sets for culled leaf output buffer
    std::vector<std::unordered_map<std::string, VkDescriptorSet>> culledLeafDescriptorSets_;

    // Track which descriptor sets have been initialized (to avoid redundant updates)
    // Key format: "frameIndex:typeName"
    std::unordered_set<std::string> initializedBarkDescriptors_;
    std::unordered_set<std::string> initializedLeafDescriptors_;
    std::unordered_set<std::string> initializedCulledLeafDescriptors_;

    // Leaf Culling subsystem (handles all compute culling)
    std::unique_ptr<TreeLeafCulling> leafCulling_;

    // Branch Shadow Culling subsystem (GPU-driven branch shadow instancing)
    std::unique_ptr<TreeBranchCulling> branchShadowCulling_;

    // Instanced branch shadow rendering pipeline
    ManagedPipeline branchShadowInstancedPipeline_;
    ManagedPipelineLayout branchShadowInstancedPipelineLayout_;
    ManagedDescriptorSetLayout branchShadowInstancedDescriptorSetLayout_;
    std::vector<VkDescriptorSet> branchShadowInstancedDescriptorSets_;
};
