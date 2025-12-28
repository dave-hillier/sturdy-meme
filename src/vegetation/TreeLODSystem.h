#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

#include "TreeImpostorAtlas.h"
#include "VulkanRAII.h"
#include "DescriptorManager.h"

class TreeSystem;

// Per-tree LOD state
struct TreeLODState {
    enum class Level {
        FullDetail,     // Full geometry rendering
        Impostor,       // Billboard impostor only
        Blending        // Cross-fade between detail and impostor
    };

    Level currentLevel = Level::FullDetail;
    Level targetLevel = Level::FullDetail;
    float blendFactor = 0.0f;       // 0 = full detail, 1 = full impostor
    float lastDistance = 0.0f;
    uint32_t archetypeIndex = 0;    // Index into impostor atlas
};

// GPU instance data for impostor rendering
struct ImpostorInstanceGPU {
    glm::vec3 position;
    float scale;
    float rotation;
    uint32_t archetypeIndex;
    float blendFactor;
    float hSize;        // Horizontal half-size (from archetype, pre-scaled)
    float vSize;        // Vertical half-size (from archetype, pre-scaled)
    float baseOffset;   // Base offset (from archetype, pre-scaled)
    float _padding;
};

class TreeLODSystem {
public:
    struct InitInfo {
        const vk::raii::Device* raiiDevice;  // vulkan-hpp RAII device
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkRenderPass hdrRenderPass;
        VkRenderPass shadowRenderPass;  // For shadow casting
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        std::string resourcePath;
        uint32_t maxFramesInFlight;
        uint32_t shadowMapSize = 2048;  // Shadow map dimensions
    };

    static std::unique_ptr<TreeLODSystem> create(const InitInfo& info);
    ~TreeLODSystem();

    // Non-copyable, non-movable
    TreeLODSystem(const TreeLODSystem&) = delete;
    TreeLODSystem& operator=(const TreeLODSystem&) = delete;
    TreeLODSystem(TreeLODSystem&&) = delete;
    TreeLODSystem& operator=(TreeLODSystem&&) = delete;

    // Screen parameters for screen-space error LOD
    struct ScreenParams {
        float screenHeight;
        float tanHalfFOV;  // tan(fov/2)

        ScreenParams() : screenHeight(1080.0f), tanHalfFOV(1.0f) {}
        ScreenParams(float height, float tanFov) : screenHeight(height), tanHalfFOV(tanFov) {}
    };

    // Update LOD states based on camera position
    void update(float deltaTime, const glm::vec3& cameraPos, const TreeSystem& treeSystem,
                const ScreenParams& screenParams = ScreenParams());

    // Render impostors (called after full geometry trees are rendered)
    void renderImpostors(VkCommandBuffer cmd, uint32_t frameIndex,
                         VkBuffer uniformBuffer, VkImageView shadowMap, VkSampler shadowSampler);

    // Render impostors using GPU-culled data (indirect drawing with Hi-Z occlusion)
    // Call this instead of renderImpostors when ImpostorCullSystem is available
    void renderImpostorsGPUCulled(VkCommandBuffer cmd, uint32_t frameIndex,
                                  VkBuffer uniformBuffer, VkImageView shadowMap, VkSampler shadowSampler,
                                  VkBuffer gpuInstanceBuffer, VkBuffer indirectDrawBuffer);

    // Render impostor shadows for a specific cascade
    void renderImpostorShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                               int cascadeIndex, VkBuffer uniformBuffer);

    // Render impostor shadows using GPU-culled data (indirect drawing with Hi-Z occlusion)
    void renderImpostorShadowsGPUCulled(VkCommandBuffer cmd, uint32_t frameIndex,
                                        int cascadeIndex, VkBuffer uniformBuffer,
                                        VkBuffer gpuInstanceBuffer, VkBuffer indirectDrawBuffer);

    // Get LOD state for a specific tree
    const TreeLODState& getTreeLODState(uint32_t treeIndex) const;

    // Check if tree should be rendered as full geometry
    bool shouldRenderFullGeometry(uint32_t treeIndex) const;

    // Check if tree should render impostor
    bool shouldRenderImpostor(uint32_t treeIndex) const;

    // Get blend factor for cross-fade (0 = full detail visible, 1 = impostor visible)
    float getBlendFactor(uint32_t treeIndex) const;

    // Cascade-aware shadow LOD queries
    // These consider both per-tree LOD state AND cascade-specific settings
    bool shouldRenderBranchShadow(uint32_t treeIndex, uint32_t cascadeIndex) const;
    bool shouldRenderLeafShadow(uint32_t treeIndex, uint32_t cascadeIndex) const;

    // Access to impostor atlas
    TreeImpostorAtlas* getImpostorAtlas() { return impostorAtlas_.get(); }
    const TreeImpostorAtlas* getImpostorAtlas() const { return impostorAtlas_.get(); }

    // LOD settings access
    TreeLODSettings& getLODSettings() { return impostorAtlas_->getLODSettings(); }
    const TreeLODSettings& getLODSettings() const { return impostorAtlas_->getLODSettings(); }

    // Generate impostor for a tree archetype
    int32_t generateImpostor(const std::string& name, const struct TreeOptions& options,
                             const struct Mesh& branchMesh,
                             const std::vector<struct LeafInstanceGPU>& leafInstances,
                             VkImageView barkAlbedo, VkImageView barkNormal,
                             VkImageView leafAlbedo, VkSampler sampler);

    // Update tree count (call when trees are added/removed)
    void updateTreeCount(size_t count);

    // Update extent on resize
    void setExtent(VkExtent2D newExtent);

    // Enable/disable GPU culling mode
    // When enabled, update() skips building CPU impostor list (GPU handles it)
    void setGPUCullingEnabled(bool enabled) { gpuCullingEnabled_ = enabled; }
    bool isGPUCullingEnabled() const { return gpuCullingEnabled_; }

    // Initialize descriptor sets with static bindings (call once during init)
    void initializeDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                                  VkImageView shadowMap, VkSampler shadowSampler);

    // Initialize GPU-culled descriptor sets with the GPU instance buffer (call once when GPU culling is enabled)
    void initializeGPUCulledDescriptors(VkBuffer gpuInstanceBuffer);

    VkDevice getDevice() const { return device_; }

    // Debug info
    struct DebugInfo {
        glm::vec3 cameraPos{0.0f};
        glm::vec3 nearestTreePos{0.0f};
        float nearestTreeDistance = 0.0f;
        float calculatedElevation = 0.0f;
    };
    const DebugInfo& getDebugInfo() const { return debugInfo_; }

private:
    TreeLODSystem() = default;
    bool initInternal(const InitInfo& info);
    bool createPipeline();
    bool createShadowPipeline();
    bool createDescriptorSetLayout();
    bool createShadowDescriptorSetLayout();
    bool allocateDescriptorSets();
    bool allocateShadowDescriptorSets();
    bool createInstanceBuffer(size_t maxInstances);
    bool createBillboardMesh();
    void updateInstanceBuffer(const std::vector<ImpostorInstanceGPU>& instances);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkRenderPass hdrRenderPass_ = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    VkExtent2D extent_{};
    uint32_t maxFramesInFlight_ = 0;
    uint32_t shadowMapSize_ = 2048;

    // Impostor atlas generator
    std::unique_ptr<TreeImpostorAtlas> impostorAtlas_;

    // Per-tree LOD states
    std::vector<TreeLODState> lodStates_;

    // Impostor rendering pipeline
    ManagedPipeline impostorPipeline_;
    ManagedPipelineLayout impostorPipelineLayout_;
    ManagedDescriptorSetLayout impostorDescriptorSetLayout_;

    // Shadow rendering pipeline
    ManagedPipeline shadowPipeline_;
    ManagedPipelineLayout shadowPipelineLayout_;
    ManagedDescriptorSetLayout shadowDescriptorSetLayout_;

    // Per-frame descriptor sets
    std::vector<VkDescriptorSet> impostorDescriptorSets_;
    std::vector<VkDescriptorSet> shadowDescriptorSets_;

    // Billboard quad mesh
    VkBuffer billboardVertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation billboardVertexAllocation_ = VK_NULL_HANDLE;
    VkBuffer billboardIndexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation billboardIndexAllocation_ = VK_NULL_HANDLE;
    uint32_t billboardIndexCount_ = 0;

    // Instance buffer for impostor rendering
    VkBuffer instanceBuffer_ = VK_NULL_HANDLE;
    VmaAllocation instanceAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize instanceBufferSize_ = 0;
    size_t maxInstances_ = 0;

    // Current frame data
    std::vector<ImpostorInstanceGPU> visibleImpostors_;
    glm::vec3 lastCameraPos_{0.0f};

    // GPU culling mode - when true, skip building CPU impostor list
    bool gpuCullingEnabled_ = false;

    // Debug info
    DebugInfo debugInfo_;
};
