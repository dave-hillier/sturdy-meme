#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <array>

#include "CullCommon.h"
#include "VulkanRAII.h"
#include "DescriptorManager.h"
#include "BufferUtils.h"

class TreeSystem;
class TreeLODSystem;

// Per-tree branch shadow input data (uploaded to GPU)
struct BranchShadowInputGPU {
    glm::vec4 positionAndScale;     // xyz = position, w = scale
    glm::vec4 rotationAndArchetype; // x = rotation (radians), y = meshIndex (uint bits), z = archetypeIndex (uint bits), w = boundingRadius (local-space)
};
static_assert(sizeof(BranchShadowInputGPU) == 32, "BranchShadowInputGPU must be 32 bytes for std430");

// Per-instance branch shadow output (visible instances)
struct BranchShadowInstanceGPU {
    glm::mat4 model; // Pre-computed model matrix
};
static_assert(sizeof(BranchShadowInstanceGPU) == 64, "BranchShadowInstanceGPU must be 64 bytes for std430");

// Uniforms for branch shadow culling compute shader
struct BranchShadowCullUniforms {
    glm::vec4 cameraPosition;               // offset 0, size 16
    glm::vec4 cascadeFrustumPlanes[6];      // offset 16, size 96 (Light frustum for current cascade)
    float fullDetailDistance;                // offset 112, size 4
    float hysteresis;                        // offset 116, size 4
    uint32_t cascadeIndex;                   // offset 120, size 4
    uint32_t numTrees;                       // offset 124, size 4
    uint32_t numMeshGroups;                  // offset 128, size 4
    uint32_t _pad0;                          // offset 132, size 4
    uint32_t _pad1;                          // offset 136, size 4
    uint32_t _pad2;                          // offset 140, size 4
};
static_assert(sizeof(BranchShadowCullUniforms) == 144, "BranchShadowCullUniforms must be 144 bytes");

// Per mesh-group metadata for indirect rendering
struct BranchMeshGroupGPU {
    uint32_t meshIndex;       // Index into branch mesh array
    uint32_t firstTree;       // First tree index using this mesh
    uint32_t treeCount;       // Number of trees using this mesh
    uint32_t barkTypeIndex;   // Bark texture index (0=birch, 1=oak, 2=pine, 3=willow)
    uint32_t indexCount;      // Mesh index count
    uint32_t maxInstances;    // Max instances in output partition
    uint32_t outputOffset;    // Base offset in output buffer
    uint32_t _pad0;
};
static_assert(sizeof(BranchMeshGroupGPU) == 32, "BranchMeshGroupGPU must be 32 bytes for std430");

// GPU-driven branch shadow culling system
// Reduces per-tree draw calls to per-archetype indirect draws
class TreeBranchCulling {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        std::string resourcePath;
        uint32_t maxFramesInFlight;
        uint32_t maxTrees = 10000;
        uint32_t maxMeshGroups = 16;
    };

    static std::unique_ptr<TreeBranchCulling> create(const InitInfo& info);
    ~TreeBranchCulling();

    TreeBranchCulling(const TreeBranchCulling&) = delete;
    TreeBranchCulling& operator=(const TreeBranchCulling&) = delete;
    TreeBranchCulling(TreeBranchCulling&&) = delete;
    TreeBranchCulling& operator=(TreeBranchCulling&&) = delete;

    // Update tree input data (call when trees change)
    void updateTreeData(const TreeSystem& treeSystem, const TreeLODSystem* lodSystem);

    // Record culling compute pass for a specific cascade
    void recordCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                       uint32_t cascadeIndex,
                       const glm::vec4* cascadeFrustumPlanes,
                       const glm::vec3& cameraPos,
                       const TreeLODSystem* lodSystem);

    // Check if culling is enabled and ready
    bool isEnabled() const { return cullPipeline_.get() != VK_NULL_HANDLE && !meshGroups_.empty(); }

    // Enable/disable GPU culling (fallback to per-tree rendering when disabled)
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabledByUser() const { return enabled_; }

    // Get output buffers for rendering (use frameIndex to match compute dispatch)
    VkBuffer getInstanceBuffer(uint32_t frameIndex) const;
    VkBuffer getIndirectBuffer(uint32_t frameIndex) const;

    // Get mesh group info for rendering loop
    struct MeshGroupRenderInfo {
        uint32_t meshIndex;
        uint32_t barkTypeIndex;
        VkDeviceSize indirectOffset;
        uint32_t instanceOffset;
    };
    const std::vector<MeshGroupRenderInfo>& getMeshGroups() const { return meshGroupRenderInfo_; }

    VkDevice getDevice() const { return device_; }

private:
    TreeBranchCulling() = default;
    bool init(const InitInfo& info);

    bool createCullPipeline();
    bool createBuffers();
    void updateDescriptorSets();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    uint32_t maxFramesInFlight_ = 0;
    uint32_t maxTrees_ = 0;
    uint32_t maxMeshGroups_ = 0;

    bool enabled_ = true;

    // Compute pipeline for GPU culling
    ManagedPipeline cullPipeline_;
    ManagedPipelineLayout cullPipelineLayout_;
    ManagedDescriptorSetLayout cullDescriptorSetLayout_;
    std::vector<VkDescriptorSet> cullDescriptorSets_;

    // Input buffer: all tree transforms
    VkBuffer inputBuffer_ = VK_NULL_HANDLE;
    VmaAllocation inputAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize inputBufferSize_ = 0;

    // Mesh group metadata buffer
    VkBuffer meshGroupBuffer_ = VK_NULL_HANDLE;
    VmaAllocation meshGroupAllocation_ = VK_NULL_HANDLE;

    // Per-frame output buffers using FrameIndexedBuffers for type-safe access
    BufferUtils::FrameIndexedBuffers outputBuffers_;
    vk::DeviceSize outputBufferSize_ = 0;

    // Indirect draw command buffers (one command per mesh group per cascade)
    BufferUtils::FrameIndexedBuffers indirectBuffers_;

    // Per-frame uniform buffers
    BufferUtils::PerFrameBufferSet uniformBuffers_;

    // Mesh group metadata (CPU side)
    std::vector<BranchMeshGroupGPU> meshGroups_;
    std::vector<MeshGroupRenderInfo> meshGroupRenderInfo_;

    uint32_t numTrees_ = 0;
    bool descriptorSetsInitialized_ = false;
};
