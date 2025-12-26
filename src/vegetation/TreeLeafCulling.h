#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <array>

#include "TreeSpatialIndex.h"
#include "CullCommon.h"
#include "VulkanRAII.h"
#include "DescriptorManager.h"
#include "BufferUtils.h"

// Forward declarations
class TreeSystem;
class TreeLODSystem;

// Uniforms for tree leaf culling compute shader (must match shader layout)
struct TreeLeafCullUniforms {
    glm::vec4 cameraPosition;
    glm::vec4 frustumPlanes[6];
    float maxDrawDistance;
    float lodTransitionStart;
    float lodTransitionEnd;
    float maxLodDropRate;
    uint32_t numTrees;
    uint32_t totalLeafInstances;
    uint32_t maxLeavesPerType;
    uint32_t _pad1;
};

// Uniforms for cell culling compute shader
struct TreeCellCullUniforms {
    glm::vec4 cameraPosition;
    glm::vec4 frustumPlanes[6];
    float maxDrawDistance;
    uint32_t numCells;
    uint32_t treesPerWorkgroup;
    uint32_t _pad0;
};

// Uniforms for tree filter compute shader
struct TreeFilterUniforms {
    glm::vec4 cameraPosition;
    glm::vec4 frustumPlanes[6];
    float maxDrawDistance;
    uint32_t maxTreesPerCell;
    uint32_t _pad0;
    uint32_t _pad1;
};

// Per-tree culling data (stored in SSBO, one entry per tree)
struct TreeCullData {
    glm::mat4 treeModel;
    uint32_t inputFirstInstance;
    uint32_t inputInstanceCount;
    uint32_t treeIndex;
    uint32_t leafTypeIndex;
    float lodBlendFactor;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(TreeCullData) == 96, "TreeCullData must be 96 bytes for std430 layout");

// Visible tree data (output from tree filtering, input to leaf culling)
struct VisibleTreeData {
    uint32_t originalTreeIndex;
    uint32_t leafFirstInstance;
    uint32_t leafInstanceCount;
    uint32_t leafTypeIndex;
    float lodBlendFactor;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(VisibleTreeData) == 32, "VisibleTreeData must be 32 bytes for std430 layout");

// World-space leaf instance data (output from compute, input to vertex shader)
struct WorldLeafInstanceGPU {
    glm::vec4 worldPosition;
    glm::vec4 worldOrientation;
    uint32_t treeIndex;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(WorldLeafInstanceGPU) == 48, "WorldLeafInstanceGPU must be 48 bytes for std430 layout");

// Per-tree render data (stored in SSBO, indexed by treeIndex in vertex shader)
struct TreeRenderDataGPU {
    glm::mat4 model;
    glm::vec4 tintAndParams;
    glm::vec4 windOffsetAndLOD;
};

// Encapsulates all tree leaf culling compute pipelines and buffers
class TreeLeafCulling {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        std::string resourcePath;
        uint32_t maxFramesInFlight;
        float terrainSize = 4096.0f;
    };

    struct CullingParams {
        float maxDrawDistance = TreeLODConstants::FULL_DETAIL_DISTANCE;
        float lodTransitionStart = TreeLODConstants::LOD_TRANSITION_START;
        float lodTransitionEnd = TreeLODConstants::LOD_TRANSITION_END;
        float maxLodDropRate = 0.75f;
    };

    static std::unique_ptr<TreeLeafCulling> create(const InitInfo& info);
    ~TreeLeafCulling();

    // Non-copyable, non-movable
    TreeLeafCulling(const TreeLeafCulling&) = delete;
    TreeLeafCulling& operator=(const TreeLeafCulling&) = delete;
    TreeLeafCulling(TreeLeafCulling&&) = delete;
    TreeLeafCulling& operator=(TreeLeafCulling&&) = delete;

    // Initialize or update spatial index from tree data
    void updateSpatialIndex(const TreeSystem& treeSystem);

    // Record compute pass for leaf culling (call before render pass)
    void recordCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                       const TreeSystem& treeSystem,
                       const TreeLODSystem* lodSystem,
                       const glm::vec3& cameraPos,
                       const glm::vec4* frustumPlanes);

    // Check if culling is enabled
    bool isEnabled() const { return cullPipeline_.get() != VK_NULL_HANDLE; }
    bool isSpatialIndexEnabled() const { return spatialIndex_ != nullptr && spatialIndex_->isValid(); }

    // Enable/disable two-phase culling
    void setTwoPhaseEnabled(bool enabled) { twoPhaseEnabled_ = enabled; }
    bool isTwoPhaseEnabled() const { return twoPhaseEnabled_; }

    // Set culling parameters
    void setParams(const CullingParams& params) { params_ = params; }
    const CullingParams& getParams() const { return params_; }

    // Get output buffers for rendering (current buffer set)
    VkBuffer getOutputBuffer() const {
        return cullOutputBuffers_.empty() ? VK_NULL_HANDLE : cullOutputBuffers_[currentBufferSet_];
    }
    VkBuffer getIndirectBuffer() const {
        return cullIndirectBuffers_.empty() ? VK_NULL_HANDLE : cullIndirectBuffers_[currentBufferSet_];
    }
    VkBuffer getTreeRenderDataBuffer() const { return treeRenderDataBuffer_; }
    uint32_t getMaxLeavesPerType() const { return maxLeavesPerType_; }

    // Swap buffer sets (call after rendering completes)
    void swapBufferSets() { currentBufferSet_ = (currentBufferSet_ + 1) % maxFramesInFlight_; }

    VkDevice getDevice() const { return device_; }

private:
    TreeLeafCulling() = default;
    bool init(const InitInfo& info);

    bool createLeafCullPipeline();
    bool createLeafCullBuffers(uint32_t maxLeafInstances, uint32_t numTrees);
    bool createCellCullPipeline();
    bool createCellCullBuffers();
    bool createTreeFilterPipeline();
    bool createTreeFilterBuffers(uint32_t maxTrees);
    bool createTwoPhaseLeafCullPipeline();
    bool createTwoPhaseLeafCullDescriptorSets();

    void updateCullDescriptorSets(const TreeSystem& treeSystem);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    uint32_t maxFramesInFlight_ = 0;
    float terrainSize_ = 4096.0f;

    CullingParams params_;

    // =========================================================================
    // Single-phase Leaf Culling Pipeline
    // =========================================================================
    ManagedPipeline cullPipeline_;
    ManagedPipelineLayout cullPipelineLayout_;
    ManagedDescriptorSetLayout cullDescriptorSetLayout_;
    std::vector<VkDescriptorSet> cullDescriptorSets_;

    // Triple-buffered output buffers (matches frames in flight)
    // Buffer set count MUST match frames in flight to avoid compute/graphics race conditions.
    uint32_t currentBufferSet_ = 0;

    std::vector<VkBuffer> cullOutputBuffers_;
    std::vector<VmaAllocation> cullOutputAllocations_;
    VkDeviceSize cullOutputBufferSize_ = 0;

    std::vector<VkBuffer> cullIndirectBuffers_;
    std::vector<VmaAllocation> cullIndirectAllocations_;

    BufferUtils::PerFrameBufferSet cullUniformBuffers_;

    VkBuffer treeDataBuffer_ = VK_NULL_HANDLE;
    VmaAllocation treeDataAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize treeDataBufferSize_ = 0;

    VkBuffer treeRenderDataBuffer_ = VK_NULL_HANDLE;
    VmaAllocation treeRenderDataAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize treeRenderDataBufferSize_ = 0;

    uint32_t numTreesForIndirect_ = 0;
    uint32_t maxLeavesPerType_ = 0;

    // =========================================================================
    // Spatial Index & Cell Culling
    // =========================================================================
    std::unique_ptr<TreeSpatialIndex> spatialIndex_;

    ManagedPipeline cellCullPipeline_;
    ManagedPipelineLayout cellCullPipelineLayout_;
    ManagedDescriptorSetLayout cellCullDescriptorSetLayout_;
    std::vector<VkDescriptorSet> cellCullDescriptorSets_;

    VkBuffer visibleCellBuffer_ = VK_NULL_HANDLE;
    VmaAllocation visibleCellAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize visibleCellBufferSize_ = 0;

    VkBuffer cellCullIndirectBuffer_ = VK_NULL_HANDLE;
    VmaAllocation cellCullIndirectAllocation_ = VK_NULL_HANDLE;

    BufferUtils::PerFrameBufferSet cellCullUniformBuffers_;

    // =========================================================================
    // Tree Filtering (Two-Phase Culling)
    // =========================================================================
    ManagedPipeline treeFilterPipeline_;
    ManagedPipelineLayout treeFilterPipelineLayout_;
    ManagedDescriptorSetLayout treeFilterDescriptorSetLayout_;
    std::vector<VkDescriptorSet> treeFilterDescriptorSets_;

    BufferUtils::PerFrameBufferSet treeFilterUniformBuffers_;

    ManagedPipeline twoPhaseLeafCullPipeline_;
    ManagedPipelineLayout twoPhaseLeafCullPipelineLayout_;
    ManagedDescriptorSetLayout twoPhaseLeafCullDescriptorSetLayout_;
    std::vector<VkDescriptorSet> twoPhaseLeafCullDescriptorSets_;

    VkBuffer visibleTreeBuffer_ = VK_NULL_HANDLE;
    VmaAllocation visibleTreeAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize visibleTreeBufferSize_ = 0;

    VkBuffer leafCullIndirectDispatch_ = VK_NULL_HANDLE;
    VmaAllocation leafCullIndirectDispatchAllocation_ = VK_NULL_HANDLE;

    bool twoPhaseEnabled_ = true;
    bool descriptorSetsInitialized_ = false;
};
