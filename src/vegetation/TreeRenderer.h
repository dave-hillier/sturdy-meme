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
#include "TreeSpatialIndex.h"
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

// Simplified push constants for single-draw leaf rendering
// Per-tree data now comes from tree render data SSBO
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

// Uniforms for tree leaf culling compute shader (must match shader layout)
// This is the global uniform block shared across all trees
struct TreeLeafCullUniforms {
    glm::vec4 cameraPosition;           // xyz = camera pos, w = unused
    glm::vec4 frustumPlanes[6];         // Frustum planes for culling
    float maxDrawDistance;              // Maximum leaf draw distance
    float lodTransitionStart;           // LOD transition start distance
    float lodTransitionEnd;             // LOD transition end distance
    float maxLodDropRate;               // Maximum LOD drop rate (0.0-1.0)
    uint32_t numTrees;                  // Total number of trees
    uint32_t totalLeafInstances;        // Total leaf instances across all trees
    uint32_t maxLeavesPerType;          // Max leaves per leaf type in output buffer
    uint32_t _pad1;
};

// Uniforms for cell culling compute shader (Phase 1: Spatial Partitioning)
// Must match tree_cell_cull.comp layout
struct TreeCellCullUniforms {
    glm::vec4 cameraPosition;           // xyz = camera pos, w = unused
    glm::vec4 frustumPlanes[6];         // Frustum planes for culling
    float maxDrawDistance;              // Maximum tree draw distance
    uint32_t numCells;                  // Total number of cells in grid
    uint32_t treesPerWorkgroup;         // How many trees to process per workgroup
    uint32_t _pad0;
};

// Uniforms for tree filter compute shader (Phase 3: Two-Phase Culling)
// Must match tree_filter.comp TreeFilterUniforms layout
struct TreeFilterUniforms {
    glm::vec4 cameraPosition;           // xyz = camera pos, w = unused
    glm::vec4 frustumPlanes[6];         // Frustum planes for culling
    float maxDrawDistance;              // Maximum leaf draw distance
    uint32_t maxTreesPerCell;           // Maximum trees per cell (for bounds checking)
    uint32_t _pad0;
    uint32_t _pad1;
};

// Number of leaf types (must match tree_leaf_cull.comp NUM_LEAF_TYPES)
constexpr uint32_t NUM_LEAF_TYPES = 4;
// Leaf type indices (oak=0, ash=1, aspen=2, pine=3)
constexpr uint32_t LEAF_TYPE_OAK = 0;
constexpr uint32_t LEAF_TYPE_ASH = 1;
constexpr uint32_t LEAF_TYPE_ASPEN = 2;
constexpr uint32_t LEAF_TYPE_PINE = 3;

// Per-tree culling data (stored in SSBO, one entry per tree)
// Must match tree_leaf_cull.comp TreeCullData struct
struct TreeCullData {
    glm::mat4 treeModel;                // Tree's model matrix
    uint32_t inputFirstInstance;        // Offset into inputInstances for this tree
    uint32_t inputInstanceCount;        // Number of input instances for this tree
    uint32_t treeIndex;                 // Index of this tree (for render data lookup)
    uint32_t leafTypeIndex;             // Leaf type (0=oak, 1=ash, 2=aspen, 3=pine)
    float lodBlendFactor;               // LOD blend factor (0=full detail, 1=full impostor)
    uint32_t _pad0;                     // Padding for std430 alignment
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(TreeCullData) == 96, "TreeCullData must be 96 bytes for std430 layout");

// Phase 3: Visible tree data (output from tree filtering, input to leaf culling)
// Must match tree_filter.comp and tree_leaf_cull_phase3.comp VisibleTreeData struct
struct VisibleTreeData {
    uint32_t originalTreeIndex;         // Index into full TreeCullData array
    uint32_t leafFirstInstance;         // Offset into leaf instance buffer
    uint32_t leafInstanceCount;         // Number of leaves for this tree
    uint32_t leafTypeIndex;             // Leaf type (0=oak, 1=ash, 2=aspen, 3=pine)
    float lodBlendFactor;               // LOD blend factor (0=full detail, 1=full impostor)
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(VisibleTreeData) == 32, "VisibleTreeData must be 32 bytes for std430 layout");

// World-space leaf instance data (output from compute, input to vertex shader)
// Must match tree_leaf_world.glsl WorldLeafInstance struct - 48 bytes
struct WorldLeafInstanceGPU {
    glm::vec4 worldPosition;            // xyz = world pos, w = size
    glm::vec4 worldOrientation;         // world-space quaternion
    uint32_t treeIndex;                 // Index into tree render data SSBO
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(WorldLeafInstanceGPU) == 48, "WorldLeafInstanceGPU must be 48 bytes for std430 layout");

// Per-tree render data (stored in SSBO, indexed by treeIndex in vertex shader)
// Must match tree_leaf_world.glsl TreeRenderData struct
struct TreeRenderDataGPU {
    glm::mat4 model;                    // Tree model matrix (for normals, wind pivot)
    glm::vec4 tintAndParams;            // rgb = leaf tint, a = autumn hue shift
    glm::vec4 windPhaseAndLOD;          // x = wind phase offset, y = LOD blend factor, zw = reserved
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
                           const TreeLODSystem* lodSystem,
                           const glm::vec3& cameraPos,
                           const glm::vec4* frustumPlanes);

    // Initialize or update spatial index from tree data
    // Call when trees are added/removed/moved
    void updateSpatialIndex(const TreeSystem& treeSystem);

    // Check if spatial indexing is enabled
    bool isSpatialIndexEnabled() const { return spatialIndex_ != nullptr && spatialIndex_->isValid(); }

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

    // Enable/disable two-phase culling (Phase 3)
    void setTwoPhaseLeafCulling(bool enabled) { twoPhaseLeafCullingEnabled_ = enabled; }
    bool isTwoPhaseLeafCullingEnabled() const { return twoPhaseLeafCullingEnabled_; }

    VkDevice getDevice() const { return device_; }

private:
    TreeRenderer() = default;
    bool initInternal(const InitInfo& info);
    bool createPipelines(const InitInfo& info);
    bool createDescriptorSetLayout();
    bool allocateDescriptorSets(uint32_t maxFramesInFlight);
    bool createCullPipeline();
    bool createCullBuffers(uint32_t maxLeafInstances, uint32_t numTrees);
    bool createCellCullPipeline();
    bool createCellCullBuffers();
    bool createTreeFilterPipeline();
    bool createTreeFilterBuffers(uint32_t maxTrees);
    bool createLeafCullPhase3Pipeline();
    bool createLeafCullPhase3DescriptorSets();

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

    // Per-tree cull data buffer (SSBO with all tree transforms and offsets for compute)
    VkBuffer treeDataBuffer_ = VK_NULL_HANDLE;
    VmaAllocation treeDataAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize treeDataBufferSize_ = 0;

    // Per-tree render data buffer (SSBO with tints, autumn, wind phase for vertex shader)
    VkBuffer treeRenderDataBuffer_ = VK_NULL_HANDLE;
    VmaAllocation treeRenderDataAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize treeRenderDataBufferSize_ = 0;

    // Culling parameters - should match tree LOD settings (fullDetailDistance = 250)
    float leafMaxDrawDistance_ = 250.0f;
    float leafLodTransitionStart_ = 150.0f;
    float leafLodTransitionEnd_ = 250.0f;
    float leafMaxLodDropRate_ = 0.75f;

    // Per-frame, per-type descriptor sets for culled leaf output buffer
    std::vector<std::unordered_map<std::string, VkDescriptorSet>> culledLeafDescriptorSets_;

    // Per-tree output offsets in the culled buffer (updated each frame during culling)
    std::vector<uint32_t> perTreeOutputOffsets_;

    // Number of trees for indirect buffer sizing
    uint32_t numTreesForIndirect_ = 0;

    // Max leaves per leaf type (for partitioned output buffer)
    uint32_t maxLeavesPerType_ = 0;

    // =========================================================================
    // Spatial Index (Phase 1: Spatial Partitioning)
    // =========================================================================
    std::unique_ptr<TreeSpatialIndex> spatialIndex_;

    // Cell culling compute pipeline
    ManagedPipeline cellCullPipeline_;
    ManagedPipelineLayout cellCullPipelineLayout_;
    ManagedDescriptorSetLayout cellCullDescriptorSetLayout_;

    // Per-frame cell culling descriptor sets
    std::vector<VkDescriptorSet> cellCullDescriptorSets_;

    // Visible cell output buffer (indices of cells that passed frustum culling)
    VkBuffer visibleCellBuffer_ = VK_NULL_HANDLE;
    VmaAllocation visibleCellAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize visibleCellBufferSize_ = 0;

    // Indirect dispatch buffer for tree culling (set by cell culling)
    VkBuffer cellCullIndirectBuffer_ = VK_NULL_HANDLE;
    VmaAllocation cellCullIndirectAllocation_ = VK_NULL_HANDLE;

    // Uniform buffer for cell culling
    BufferUtils::PerFrameBufferSet cellCullUniformBuffers_;

    // Terrain size for spatial index configuration
    float terrainSize_ = 4096.0f;

    // =========================================================================
    // Phase 3: Two-Phase Tree-to-Leaf Culling
    // =========================================================================

    // Tree filtering compute pipeline (filters trees from visible cells)
    ManagedPipeline treeFilterPipeline_;
    ManagedPipelineLayout treeFilterPipelineLayout_;
    ManagedDescriptorSetLayout treeFilterDescriptorSetLayout_;

    // Per-frame tree filter descriptor sets
    std::vector<VkDescriptorSet> treeFilterDescriptorSets_;

    // Uniform buffer for tree filtering
    BufferUtils::PerFrameBufferSet treeFilterUniformBuffers_;

    // Phase 3 leaf culling pipeline (processes visible trees only)
    ManagedPipeline leafCullPhase3Pipeline_;
    ManagedPipelineLayout leafCullPhase3PipelineLayout_;
    ManagedDescriptorSetLayout leafCullPhase3DescriptorSetLayout_;
    std::vector<VkDescriptorSet> leafCullPhase3DescriptorSets_;

    // Visible tree output buffer (compacted list of visible trees)
    // Contains: [visibleTreeCount, VisibleTreeData[0], VisibleTreeData[1], ...]
    VkBuffer visibleTreeBuffer_ = VK_NULL_HANDLE;
    VmaAllocation visibleTreeAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize visibleTreeBufferSize_ = 0;

    // Indirect dispatch buffer for leaf culling (set by tree filtering)
    VkBuffer leafCullIndirectDispatch_ = VK_NULL_HANDLE;
    VmaAllocation leafCullIndirectDispatchAllocation_ = VK_NULL_HANDLE;

    // Flag to enable two-phase culling (Phase 3)
    bool twoPhaseLeafCullingEnabled_ = true;
};
