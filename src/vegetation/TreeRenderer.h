#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <array>

#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "core/VulkanRAII.h"
#include "core/DescriptorManager.h"
#include "BufferUtils.h"

// Push constants for tree rendering
struct TreeBranchPushConstants {
    glm::mat4 model;      // offset 0, size 64
    float time;           // offset 64, size 4
    float lodBlendFactor; // offset 68, size 4 (0=full geometry, 1=full impostor)
    float _pad1[2];       // offset 72, size 8 (padding to align vec3 to 16 bytes)
    glm::vec3 barkTint;   // offset 80, size 12
    float roughnessScale; // offset 92, size 4
};

struct TreeLeafPushConstants {
    glm::mat4 model;     // offset 0, size 64
    float time;          // offset 64, size 4
    float lodBlendFactor;// offset 68, size 4 (0=full geometry, 1=full impostor)
    float _pad1[2];      // offset 72, size 8 (padding to align vec3 to 16 bytes)
    glm::vec3 leafTint;  // offset 80, size 12
    float alphaTest;     // offset 92, size 4
    int32_t firstInstance; // offset 96, size 4 (offset into leaf SSBO for this tree)
    float _pad2[3];      // offset 100, size 12 (padding to 16-byte boundary)
};

// Shadow pass push constants (branches)
struct TreeBranchShadowPushConstants {
    glm::mat4 model;      // offset 0, size 64
    int cascadeIndex;     // offset 64, size 4
};

// Shadow pass push constants (leaves with alpha test and instancing)
struct TreeLeafShadowPushConstants {
    glm::mat4 model;       // offset 0, size 64
    int32_t cascadeIndex;  // offset 64, size 4
    float alphaTest;       // offset 68, size 4
    int32_t firstInstance; // offset 72, size 4 (offset into leaf SSBO for this tree)
    float _pad;            // offset 76, size 4 (padding)
};

// Uniforms for tree leaf culling compute shader (must match shader layout)
struct TreeLeafCullUniforms {
    glm::vec4 cameraPosition;           // xyz = camera pos, w = unused
    glm::vec4 frustumPlanes[6];         // Frustum planes for culling
    glm::mat4 treeModel;                // Current tree's model matrix
    float maxDrawDistance;              // Maximum leaf draw distance
    float lodTransitionStart;           // LOD transition start distance
    float lodTransitionEnd;             // LOD transition end distance
    float maxLodDropRate;               // Maximum LOD drop rate (0.0-1.0)
    uint32_t inputFirstInstance;        // Offset into inputInstances for this tree
    uint32_t inputInstanceCount;        // Number of input instances for this tree
    uint32_t outputBaseOffset;          // Base offset in output buffer for this tree
    uint32_t treeIndex;                 // Index of this tree (for indirect command array)
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

    // Get culled leaf descriptor set for a specific type
    VkDescriptorSet getCulledLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const;

    // Get descriptor set for a specific type (returns default if type not found)
    VkDescriptorSet getBranchDescriptorSet(uint32_t frameIndex, const std::string& barkType) const;
    VkDescriptorSet getLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const;

    // Record compute pass for leaf culling (call before render pass)
    void recordLeafCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                           const TreeSystem& treeSystem,
                           const glm::vec3& cameraPos,
                           const glm::vec4* frustumPlanes);

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
    bool isLeafCullingEnabled() const { return cullPipeline_.get() != VK_NULL_HANDLE; }

    VkDevice getDevice() const { return device_; }

private:
    TreeRenderer() = default;
    bool initInternal(const InitInfo& info);
    bool createPipelines(const InitInfo& info);
    bool createDescriptorSetLayout();
    bool allocateDescriptorSets(uint32_t maxFramesInFlight);
    bool createCullPipeline();
    bool createCullBuffers(uint32_t maxLeafInstances, uint32_t numTrees);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    VkExtent2D extent_{};
    uint32_t maxFramesInFlight_ = 0;

    // Pipelines
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

    // =========================================================================
    // Leaf Culling Compute Pipeline
    // =========================================================================
    ManagedPipeline cullPipeline_;
    ManagedPipelineLayout cullPipelineLayout_;
    ManagedDescriptorSetLayout cullDescriptorSetLayout_;

    // Per-frame culling descriptor sets
    std::vector<VkDescriptorSet> cullDescriptorSets_;

    // Double-buffered output buffers (visible leaf instances after culling)
    // Using double-buffering to avoid compute/graphics synchronization issues
    static constexpr uint32_t CULL_BUFFER_SET_COUNT = 2;
    uint32_t currentCullBufferSet_ = 0;

    // Output buffers for visible leaf instances (one per buffer set)
    std::array<VkBuffer, CULL_BUFFER_SET_COUNT> cullOutputBuffers_{};
    std::array<VmaAllocation, CULL_BUFFER_SET_COUNT> cullOutputAllocations_{};
    VkDeviceSize cullOutputBufferSize_ = 0;

    // Indirect draw command buffers (one per buffer set)
    std::array<VkBuffer, CULL_BUFFER_SET_COUNT> cullIndirectBuffers_{};
    std::array<VmaAllocation, CULL_BUFFER_SET_COUNT> cullIndirectAllocations_{};

    // Uniform buffers for culling (per-frame)
    BufferUtils::PerFrameBufferSet cullUniformBuffers_;

    // Culling parameters
    float leafMaxDrawDistance_ = 100.0f;
    float leafLodTransitionStart_ = 50.0f;
    float leafLodTransitionEnd_ = 100.0f;
    float leafMaxLodDropRate_ = 0.75f;

    // Per-frame, per-type descriptor sets for culled leaf output buffer
    std::vector<std::unordered_map<std::string, VkDescriptorSet>> culledLeafDescriptorSets_;

    // Per-tree output offsets in the culled buffer (updated each frame during culling)
    std::vector<uint32_t> perTreeOutputOffsets_;

    // Number of trees for indirect buffer sizing
    uint32_t numTreesForIndirect_ = 0;
};
