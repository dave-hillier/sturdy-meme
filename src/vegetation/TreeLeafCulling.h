#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <array>
#include <optional>

#include "TreeSpatialIndex.h"
#include "CullCommon.h"
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

// Params structs for shader-specific data (separate from shared CullingUniforms)
struct LeafCullParams {
    uint32_t numTrees;
    uint32_t totalLeafInstances;
    uint32_t maxLeavesPerType;
    uint32_t _pad1;
};

struct CellCullParams {
    uint32_t numCells;
    uint32_t treesPerWorkgroup;
    uint32_t _pad0;
    uint32_t _pad1;
};

struct TreeFilterParams {
    uint32_t maxTreesPerCell;
    uint32_t maxVisibleTrees;  // Buffer capacity for bounds checking
    uint32_t _pad0;
    uint32_t _pad1;
};

// Params for phase 3 leaf culling (matches shader LeafCullP3Params)
struct LeafCullP3Params {
    uint32_t maxLeavesPerType;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
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
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
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
        // Note: lodTransitionStart/End/maxLodDropRate are legacy - leaf dropping now
        // uses lodBlendFactor directly (from screen-space error LOD system)
        float lodTransitionStart = TreeLODConstants::LOD_TRANSITION_START;  // Legacy
        float lodTransitionEnd = TreeLODConstants::LOD_TRANSITION_END;      // Legacy
        float maxLodDropRate = 0.75f;                                        // Legacy
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
    bool isEnabled() const { return cullPipeline_.has_value(); }
    bool isSpatialIndexEnabled() const { return spatialIndex_ != nullptr && spatialIndex_->isValid(); }

    // Enable/disable two-phase culling
    void setTwoPhaseEnabled(bool enabled) { twoPhaseEnabled_ = enabled; }
    bool isTwoPhaseEnabled() const { return twoPhaseEnabled_; }

    // Set culling parameters
    void setParams(const CullingParams& params) { params_ = params; }
    const CullingParams& getParams() const { return params_; }

    // Get output buffers for rendering (indexed by frameIndex for proper triple-buffering)
    // IMPORTANT: Always pass the same frameIndex used for recordCulling() to ensure
    // compute and graphics passes use the same buffer set.
    // Uses FrameIndexedBuffers to enforce this pattern and prevent desync bugs.
    VkBuffer getOutputBuffer(uint32_t frameIndex) const {
        return cullOutputBuffers_.getVk(frameIndex);
    }
    VkBuffer getIndirectBuffer(uint32_t frameIndex) const {
        return cullIndirectBuffers_.getVk(frameIndex);
    }
    VkBuffer getTreeRenderDataBuffer(uint32_t frameIndex) const {
        return treeRenderDataBuffers_.getVk(frameIndex);
    }
    uint32_t getMaxLeavesPerType() const { return maxLeavesPerType_; }

    VkDevice getDevice() const { return device_; }

    explicit TreeLeafCulling(ConstructToken) {}

private:
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

    const vk::raii::Device* raiiDevice_ = nullptr;
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
    std::optional<vk::raii::Pipeline> cullPipeline_;
    std::optional<vk::raii::PipelineLayout> cullPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> cullDescriptorSetLayout_;
    std::vector<VkDescriptorSet> cullDescriptorSets_;

    // Triple-buffered output buffers using FrameIndexedBuffers for type-safe access.
    // This enforces that buffer access always uses frameIndex, preventing the common
    // desync bug where a separate counter gets out of sync with frameIndex.
    BufferUtils::FrameIndexedBuffers cullOutputBuffers_;
    BufferUtils::FrameIndexedBuffers cullIndirectBuffers_;
    vk::DeviceSize cullOutputBufferSize_ = 0;

    BufferUtils::PerFrameBufferSet cullUniformBuffers_;  // CullingUniforms at binding 3
    BufferUtils::PerFrameBufferSet leafCullParamsBuffers_;  // LeafCullParams at binding 8

    // Triple-buffered tree data buffers to prevent race conditions.
    // These are updated every frame via vkCmdUpdateBuffer, so they must be
    // triple-buffered to avoid overwriting data that in-flight frames are reading.
    BufferUtils::FrameIndexedBuffers treeDataBuffers_;
    BufferUtils::FrameIndexedBuffers treeRenderDataBuffers_;
    VkDeviceSize treeDataBufferSize_ = 0;
    VkDeviceSize treeRenderDataBufferSize_ = 0;

    uint32_t numTreesForIndirect_ = 0;
    uint32_t maxLeavesPerType_ = 0;

    // =========================================================================
    // Spatial Index & Cell Culling
    // =========================================================================
    std::unique_ptr<TreeSpatialIndex> spatialIndex_;

    std::optional<vk::raii::Pipeline> cellCullPipeline_;
    std::optional<vk::raii::PipelineLayout> cellCullPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> cellCullDescriptorSetLayout_;
    std::vector<VkDescriptorSet> cellCullDescriptorSets_;

    // Triple-buffered intermediate buffers to prevent race conditions.
    // These are reset and written each frame, so they must be triple-buffered
    // to avoid frame N+1 overwriting data that frame N is still reading.
    BufferUtils::FrameIndexedBuffers visibleCellBuffers_;
    VkDeviceSize visibleCellBufferSize_ = 0;

    BufferUtils::FrameIndexedBuffers cellCullIndirectBuffers_;

    BufferUtils::PerFrameBufferSet cellCullUniformBuffers_;  // CullingUniforms at binding 3
    BufferUtils::PerFrameBufferSet cellCullParamsBuffers_;  // CellCullParams at binding 4

    // =========================================================================
    // Tree Filtering (Two-Phase Culling)
    // =========================================================================
    std::optional<vk::raii::Pipeline> treeFilterPipeline_;
    std::optional<vk::raii::PipelineLayout> treeFilterPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> treeFilterDescriptorSetLayout_;
    std::vector<VkDescriptorSet> treeFilterDescriptorSets_;

    BufferUtils::PerFrameBufferSet treeFilterUniformBuffers_;  // CullingUniforms at binding 6
    BufferUtils::PerFrameBufferSet treeFilterParamsBuffers_;  // TreeFilterParams at binding 7

    std::optional<vk::raii::Pipeline> twoPhaseLeafCullPipeline_;
    std::optional<vk::raii::PipelineLayout> twoPhaseLeafCullPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> twoPhaseLeafCullDescriptorSetLayout_;
    std::vector<VkDescriptorSet> twoPhaseLeafCullDescriptorSets_;

    BufferUtils::PerFrameBufferSet leafCullP3ParamsBuffers_;  // LeafCullP3Params at binding 6

    // Triple-buffered intermediate buffers for two-phase culling
    BufferUtils::FrameIndexedBuffers visibleTreeBuffers_;
    VkDeviceSize visibleTreeBufferSize_ = 0;
    uint32_t maxVisibleTrees_ = 0;  // Buffer capacity for bounds checking in shader

    BufferUtils::FrameIndexedBuffers leafCullIndirectDispatchBuffers_;

    bool twoPhaseEnabled_ = true;
    bool descriptorSetsInitialized_ = false;
};
