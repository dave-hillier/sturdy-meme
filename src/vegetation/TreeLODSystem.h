#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

#include "TreeImpostorAtlas.h"
#include "core/VulkanRAII.h"
#include "core/DescriptorManager.h"
#include "core/BufferUtils.h"

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
// Must match shader struct ImpostorInstance in tree_impostor_instance.glsl
// Layout: 3 vec4s = 48 bytes
struct ImpostorInstanceGPU {
    // vec4 positionAndScale
    glm::vec3 position;         // offset 0
    float scale;                // offset 12

    // vec4 rotationAndArchetype
    float rotation;             // offset 16
    float archetypeIndex;       // offset 20 (stored as float, shader reads as uint)
    float blendFactor;          // offset 24
    float _reserved1;           // offset 28 (shader's rotationAndArchetype.w)

    // vec4 sizeAndOffset
    float hSize;                // offset 32
    float vSize;                // offset 36
    float baseOffset;           // offset 40
    float _reserved2;           // offset 44
};

class TreeLODSystem {
public:
    struct InitInfo {
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
    void update(uint32_t frameIndex, float deltaTime, const glm::vec3& cameraPos, const TreeSystem& treeSystem,
                const ScreenParams& screenParams = ScreenParams());

    // Render impostors (unified method - uses GPU culling when buffers provided)
    // @param gpuInstanceBuffer - GPU-culled instance buffer (null for CPU path)
    // @param indirectDrawBuffer - indirect draw buffer (null for CPU path)
    void renderImpostors(VkCommandBuffer cmd, uint32_t frameIndex,
                         VkBuffer uniformBuffer, VkImageView shadowMap, VkSampler shadowSampler,
                         VkBuffer gpuInstanceBuffer = VK_NULL_HANDLE,
                         VkBuffer indirectDrawBuffer = VK_NULL_HANDLE);

    // Render impostor shadows (unified method - uses GPU culling when buffers provided)
    void renderImpostorShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                               int cascadeIndex, VkBuffer uniformBuffer,
                               VkBuffer gpuInstanceBuffer = VK_NULL_HANDLE,
                               VkBuffer indirectDrawBuffer = VK_NULL_HANDLE);

    // Get LOD state for a specific tree
    const TreeLODState& getTreeLODState(uint32_t treeIndex) const;

    // Check if tree should be rendered as full geometry
    bool shouldRenderFullGeometry(uint32_t treeIndex) const;

    // Check if tree should render impostor
    bool shouldRenderImpostor(uint32_t treeIndex) const;

    // Get blend factor for cross-fade (0 = full detail visible, 1 = impostor visible)
    float getBlendFactor(uint32_t treeIndex) const;

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

    // Update descriptor sets
    void updateDescriptorSets(uint32_t frameIndex, VkBuffer uniformBuffer,
                              VkImageView shadowMap, VkSampler shadowSampler);

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

    // Billboard quad mesh (RAII - auto-cleanup on destruction)
    ManagedBuffer billboardVertexBuffer_;
    ManagedBuffer billboardIndexBuffer_;
    uint32_t billboardIndexCount_ = 0;

    // Instance buffers for impostor rendering (per-frame to avoid GPU race conditions)
    BufferUtils::PerFrameBufferSet instanceBuffers_;
    VkDeviceSize instanceBufferSize_ = 0;
    size_t maxInstances_ = 0;
    uint32_t currentFrameIndex_ = 0;  // Track which frame we're updating

    // Current frame data
    std::vector<ImpostorInstanceGPU> visibleImpostors_;
    glm::vec3 lastCameraPos_{0.0f};

    // GPU culling mode - when true, skip building CPU impostor list
    bool gpuCullingEnabled_ = false;

    // Debug info
    DebugInfo debugInfo_;
};
