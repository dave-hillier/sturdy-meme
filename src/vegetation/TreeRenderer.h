#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "TreeLeafCulling.h"
#include "TreeBranchCulling.h"
#include "DescriptorManager.h"

// Push constants for tree rendering
// alignas(16) ensures proper alignment for SIMD operations on glm::mat4.
// Without this, O3-optimized aligned SSE/AVX loads can crash.
struct alignas(16) TreeBranchPushConstants {
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
// alignas(16) ensures proper alignment for SIMD operations on glm::mat4.
struct alignas(16) TreeBranchShadowPushConstants {
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
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        vk::Device device;
        vk::PhysicalDevice physicalDevice;
        VmaAllocator allocator;
        vk::RenderPass hdrRenderPass;
        vk::RenderPass shadowRenderPass;
        DescriptorManager::Pool* descriptorPool;
        vk::Extent2D extent;
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
        vk::Buffer uniformBuffer,
        vk::Buffer windBuffer,
        vk::ImageView shadowMapView,
        vk::Sampler shadowSampler,
        vk::ImageView barkAlbedo,
        vk::ImageView barkNormal,
        vk::ImageView barkRoughness,
        vk::ImageView barkAO,
        vk::Sampler barkSampler);

    void updateLeafDescriptorSet(
        uint32_t frameIndex,
        const std::string& leafType,
        vk::Buffer uniformBuffer,
        vk::Buffer windBuffer,
        vk::ImageView shadowMapView,
        vk::Sampler shadowSampler,
        vk::ImageView leafAlbedo,
        vk::Sampler leafSampler,
        vk::Buffer leafInstanceBuffer,
        vk::DeviceSize leafInstanceBufferSize,
        vk::Buffer snowBuffer = VK_NULL_HANDLE);

    // Update culled leaf descriptor sets (called after cull buffers are created)
    void updateCulledLeafDescriptorSet(
        uint32_t frameIndex,
        const std::string& leafType,
        vk::Buffer uniformBuffer,
        vk::Buffer windBuffer,
        vk::ImageView shadowMapView,
        vk::Sampler shadowSampler,
        vk::ImageView leafAlbedo,
        vk::Sampler leafSampler,
        vk::Buffer snowBuffer = VK_NULL_HANDLE);

    // Get descriptor set for a specific type (returns default if type not found)
    vk::DescriptorSet getBranchDescriptorSet(uint32_t frameIndex, const std::string& barkType) const;
    vk::DescriptorSet getLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const;
    vk::DescriptorSet getCulledLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const;

    // Record compute pass for leaf culling (call before render pass)
    void recordLeafCulling(vk::CommandBuffer cmd, uint32_t frameIndex,
                           const TreeSystem& treeSystem,
                           const TreeLODSystem* lodSystem,
                           const glm::vec3& cameraPos,
                           const glm::vec4* frustumPlanes);

    // Record compute pass for branch shadow culling (call before shadow pass)
    void recordBranchShadowCulling(vk::CommandBuffer cmd, uint32_t frameIndex,
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
    void render(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                const TreeSystem& treeSystem, const TreeLODSystem* lodSystem = nullptr);

    // Render tree shadows (optionally filtering by LOD)
    void renderShadows(vk::CommandBuffer cmd, uint32_t frameIndex,
                       const TreeSystem& treeSystem, int cascadeIndex,
                       const TreeLODSystem* lodSystem = nullptr);

    // Update extent on resize
    void setExtent(vk::Extent2D newExtent);

    // Invalidate descriptor cache (call when bound resources change, e.g., on resize)
    void invalidateDescriptorCache();

    // Check if leaf culling is enabled
    bool isLeafCullingEnabled() const;

    // Enable/disable two-phase culling
    void setTwoPhaseLeafCulling(bool enabled);
    bool isTwoPhaseLeafCullingEnabled() const;

    vk::Device getDevice() const { return device_; }

    explicit TreeRenderer(ConstructToken) {}

private:
    bool initInternal(const InitInfo& info);
    bool createPipelines(const InitInfo& info);
    bool createDescriptorSetLayout();
    bool allocateDescriptorSets(uint32_t maxFramesInFlight);

    const vk::raii::Device* raiiDevice_ = nullptr;
    vk::Device device_;
    vk::PhysicalDevice physicalDevice_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    vk::Extent2D extent_{};
    uint32_t maxFramesInFlight_ = 0;

    // Graphics Pipelines
    std::optional<vk::raii::Pipeline> branchPipeline_;
    std::optional<vk::raii::Pipeline> leafPipeline_;
    std::optional<vk::raii::Pipeline> branchShadowPipeline_;
    std::optional<vk::raii::Pipeline> leafShadowPipeline_;

    // Pipeline layouts
    std::optional<vk::raii::PipelineLayout> branchPipelineLayout_;
    std::optional<vk::raii::PipelineLayout> leafPipelineLayout_;
    std::optional<vk::raii::PipelineLayout> branchShadowPipelineLayout_;
    std::optional<vk::raii::PipelineLayout> leafShadowPipelineLayout_;

    // Descriptor set layouts
    std::optional<vk::raii::DescriptorSetLayout> branchDescriptorSetLayout_;
    std::optional<vk::raii::DescriptorSetLayout> leafDescriptorSetLayout_;

    // Per-frame descriptor sets (indexed by frame, then by texture type)
    std::vector<std::unordered_map<std::string, vk::DescriptorSet>> branchDescriptorSets_;
    std::vector<std::unordered_map<std::string, vk::DescriptorSet>> leafDescriptorSets_;

    // Default descriptor sets for types without registered textures
    std::vector<vk::DescriptorSet> defaultBranchDescriptorSets_;
    std::vector<vk::DescriptorSet> defaultLeafDescriptorSets_;

    // Per-frame, per-type descriptor sets for culled leaf output buffer
    std::vector<std::unordered_map<std::string, vk::DescriptorSet>> culledLeafDescriptorSets_;

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
    std::optional<vk::raii::Pipeline> branchShadowInstancedPipeline_;
    std::optional<vk::raii::PipelineLayout> branchShadowInstancedPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> branchShadowInstancedDescriptorSetLayout_;
    std::vector<vk::DescriptorSet> branchShadowInstancedDescriptorSets_;
};
