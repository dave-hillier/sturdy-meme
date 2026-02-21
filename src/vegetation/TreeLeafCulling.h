#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>

#include "TreeSpatialIndex.h"
#include "TreeCullingTypes.h"
#include "CullCommon.h"
#include "CellCullStage.h"
#include "TreeFilterStage.h"
#include "LeafCullPhase3Stage.h"
#include "DescriptorManager.h"
#include "FrameIndexedBuffers.h"

// Forward declarations
class TreeSystem;
class TreeLODSystem;

// Orchestrates three-phase GPU-driven leaf culling:
//   Phase 1 (CellCullStage): Frustum-cull spatial index cells
//   Phase 2 (TreeFilterStage): Filter trees in visible cells
//   Phase 3 (LeafCullPhase3Stage): Cull individual leaf instances
class TreeLeafCulling {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TreeLeafCulling(ConstructToken) {}

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
    bool isEnabled() const { return cellCullStage_.pipeline.has_value(); }
    bool isSpatialIndexEnabled() const { return spatialIndex_ != nullptr && spatialIndex_->isValid(); }

    // Set culling parameters
    void setParams(const CullingParams& params) { params_ = params; }
    const CullingParams& getParams() const { return params_; }

    // Get output buffers for rendering (indexed by frameIndex for proper triple-buffering)
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

private:
    bool init(const InitInfo& info);
    bool createSharedOutputBuffers(uint32_t numTrees);

    const vk::raii::Device* raiiDevice_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    uint32_t maxFramesInFlight_ = 0;
    float terrainSize_ = 4096.0f;

    CullingParams params_;

    // Shared output buffers
    BufferUtils::FrameIndexedBuffers cullOutputBuffers_;
    BufferUtils::FrameIndexedBuffers cullIndirectBuffers_;
    vk::DeviceSize cullOutputBufferSize_ = 0;

    BufferUtils::FrameIndexedBuffers treeDataBuffers_;
    BufferUtils::FrameIndexedBuffers treeRenderDataBuffers_;
    VkDeviceSize treeDataBufferSize_ = 0;
    VkDeviceSize treeRenderDataBufferSize_ = 0;

    uint32_t numTreesForIndirect_ = 0;
    uint32_t maxLeavesPerType_ = 0;

    // Spatial index
    std::unique_ptr<TreeSpatialIndex> spatialIndex_;

    // Pipeline stages
    CellCullStage cellCullStage_;
    TreeFilterStage treeFilterStage_;
    LeafCullPhase3Stage leafCullPhase3Stage_;
};
