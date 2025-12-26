#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

#include "core/VulkanRAII.h"
#include "core/DescriptorManager.h"
#include "core/BufferUtils.h"
#include "TreeImpostorAtlas.h"  // For TreeLODSettings

class TreeSystem;

// Uniforms for impostor culling compute shader (must match shader layout)
struct alignas(16) ImpostorCullUniforms {
    glm::vec4 cameraPosition;           // xyz = camera pos, w = unused
    glm::vec4 frustumPlanes[6];         // Frustum planes for culling
    glm::mat4 viewProjMatrix;           // View-projection matrix for Hi-Z testing
    glm::vec4 screenParams;             // x = width, y = height, z = 1/width, w = 1/height
    float fullDetailDistance;           // Trees closer than this render as geometry
    float impostorDistance;             // Trees beyond this are culled
    float hysteresis;                   // Hysteresis for LOD transitions
    float blendRange;                   // Distance over which to blend LODs
    uint32_t numTrees;                  // Total number of trees
    uint32_t enableHiZ;                 // 1 = enable Hi-Z culling, 0 = frustum only
    uint32_t useScreenSpaceError;       // 1 = use screen-space error LOD, 0 = distance-based
    float tanHalfFOV;                   // tan(fov/2) for screen-space error calculation
    float errorThresholdFull;           // Screen error threshold for full detail (pixels)
    float errorThresholdImpostor;       // Screen error threshold for impostor (pixels)
    float errorThresholdCull;           // Screen error beyond which to cull
    uint32_t temporalUpdateMode;        // 0=full, 1=partial, 2=skip
    uint32_t temporalUpdateOffset;      // For partial: start index of trees to update
    uint32_t temporalUpdateCount;       // For partial: number of trees to update this frame
    uint32_t _pad0;
};

// Per-archetype sizing data (matches shader struct)
struct ArchetypeCullData {
    glm::vec4 sizingData;  // x = hSize, y = vSize, z = baseOffset, w = boundingSphereRadius
    glm::vec4 lodErrorData; // x = worldErrorFull, y = worldErrorImpostor, z = unused, w = unused
};

// Tree input data for culling (matches shader struct)
struct TreeCullInputData {
    glm::vec4 positionAndScale;      // xyz = world position, w = scale
    glm::vec4 rotationAndArchetype;  // x = Y-axis rotation, yzw = archetype index as uint bits
};

// Visible impostor output data (matches shader struct)
struct ImpostorOutputData {
    glm::vec4 positionAndScale;      // xyz = world position, w = scale
    glm::vec4 rotationAndArchetype;  // x = rotation, y = archetype, z = blend factor, w = reserved
    glm::vec4 sizeAndOffset;         // x = hSize, y = vSize, z = baseOffset, w = reserved
};

/**
 * GPU-driven impostor culling system with Hi-Z occlusion culling.
 *
 * This system performs frustum culling and Hi-Z occlusion culling for tree
 * impostors using compute shaders, outputting visible instances for indirect
 * drawing.
 */
class ImpostorCullSystem {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        std::string resourcePath;
        uint32_t maxFramesInFlight;
        uint32_t maxTrees = 100000;
        uint32_t maxArchetypes = 16;
    };

    static std::unique_ptr<ImpostorCullSystem> create(const InitInfo& info);
    ~ImpostorCullSystem();

    // Non-copyable, non-movable
    ImpostorCullSystem(const ImpostorCullSystem&) = delete;
    ImpostorCullSystem& operator=(const ImpostorCullSystem&) = delete;
    ImpostorCullSystem(ImpostorCullSystem&&) = delete;
    ImpostorCullSystem& operator=(ImpostorCullSystem&&) = delete;

    /**
     * Update tree input data for culling.
     * Call when tree instances change.
     */
    void updateTreeData(const TreeSystem& treeSystem, const TreeImpostorAtlas* atlas);

    /**
     * Update archetype data (sizing, bounding radius).
     * Call when archetypes are added or modified.
     */
    void updateArchetypeData(const TreeImpostorAtlas* atlas);

    /**
     * Record compute dispatch for impostor culling.
     * Call after terrain depth pass and Hi-Z pyramid generation.
     *
     * @param cmd Command buffer
     * @param frameIndex Current frame index
     * @param cameraPos Camera world position
     * @param frustumPlanes 6 frustum planes for culling
     * @param viewProjMatrix View-projection matrix
     * @param hiZPyramidView Hi-Z pyramid image view (nullptr to disable Hi-Z)
     * @param hiZSampler Sampler for Hi-Z pyramid
     * @param lodSettings LOD settings from TreeLODSystem (single source of truth)
     * @param tanHalfFOV tan(fov/2) from projection matrix for screen-space error
     */
    void recordCulling(VkCommandBuffer cmd, uint32_t frameIndex,
                       const glm::vec3& cameraPos,
                       const glm::vec4* frustumPlanes,
                       const glm::mat4& viewProjMatrix,
                       VkImageView hiZPyramidView,
                       VkSampler hiZSampler,
                       const TreeLODSettings& lodSettings,
                       float tanHalfFOV);

    // Get output buffers for rendering (per-frame to avoid race conditions)
    VkBuffer getVisibleImpostorBuffer(uint32_t frameIndex) const {
        return visibleImpostorBuffers_.buffers[frameIndex];
    }
    VkBuffer getIndirectDrawBuffer(uint32_t frameIndex) const {
        return indirectDrawBuffers_.buffers[frameIndex];
    }

    // Get visible impostor count (read from GPU - may be stale)
    uint32_t getVisibleCount() const { return lastVisibleCount_; }

    // Update extent on resize
    void setExtent(VkExtent2D newExtent);

    // Enable/disable Hi-Z culling
    void setHiZEnabled(bool enabled) { hiZEnabled_ = enabled; }
    bool isHiZEnabled() const { return hiZEnabled_; }

    // Get tree count
    uint32_t getTreeCount() const { return treeCount_; }

private:
    ImpostorCullSystem() = default;
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createComputePipeline();
    bool createDescriptorSetLayout();
    bool allocateDescriptorSets();
    bool createBuffers();

    void updateDescriptorSets(uint32_t frameIndex, VkImageView hiZPyramidView, VkSampler hiZSampler);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    VkExtent2D extent_{};
    uint32_t maxFramesInFlight_ = 0;
    uint32_t maxTrees_ = 0;
    uint32_t maxArchetypes_ = 0;

    // Compute pipeline
    ManagedPipeline cullPipeline_;
    ManagedPipelineLayout cullPipelineLayout_;
    ManagedDescriptorSetLayout cullDescriptorSetLayout_;

    // Per-frame descriptor sets
    std::vector<VkDescriptorSet> cullDescriptorSets_;

    // Tree input buffer (all trees) - RAII auto-cleanup
    ManagedBuffer treeInputBuffer_;
    VkDeviceSize treeInputBufferSize_ = 0;

    // Archetype data buffer - RAII auto-cleanup
    ManagedBuffer archetypeBuffer_;
    VkDeviceSize archetypeBufferSize_ = 0;

    // Visible impostor output buffers (per-frame to avoid GPU race conditions)
    BufferUtils::PerFrameBufferSet visibleImpostorBuffers_;
    VkDeviceSize visibleImpostorBufferSize_ = 0;

    // Indirect draw command buffers (per-frame to avoid GPU race conditions)
    BufferUtils::PerFrameBufferSet indirectDrawBuffers_;

    // Uniform buffers (per-frame)
    BufferUtils::PerFrameBufferSet uniformBuffers_;

    // Visibility cache buffer for temporal coherence
    // Stores 1 bit per tree: 1 = visible as impostor, 0 = not visible
    // RAII auto-cleanup
    ManagedBuffer visibilityCacheBuffer_;
    VkDeviceSize visibilityCacheBufferSize_ = 0;

    // State
    uint32_t treeCount_ = 0;
    uint32_t archetypeCount_ = 0;
    uint32_t lastVisibleCount_ = 0;
    bool hiZEnabled_ = true;

    // Track if Hi-Z texture changed
    VkImageView lastHiZView_ = VK_NULL_HANDLE;
};
