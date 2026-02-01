#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <optional>

#include "SkinnedMesh.h"
#include "CharacterLOD.h"
#include "DescriptorManager.h"
#include "MaterialDescriptorFactory.h"
#include "RenderableBuilder.h"
#include "GlobalBufferManager.h"
#include "DynamicUniformBuffer.h"

class AnimatedCharacter;

// Maximum number of skinned characters that can be rendered per frame
// Each character needs a separate bone matrix slot in the dynamic uniform buffer
constexpr uint32_t MAX_SKINNED_CHARACTERS = 64;

// Skinned mesh renderer - handles GPU skinning pipeline and bone matrices
class SkinnedMeshRenderer {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit SkinnedMeshRenderer(ConstructToken) {}

    // Callback type for adding common descriptor bindings
    using AddCommonBindingsCallback = std::function<void(DescriptorManager::LayoutBuilder&)>;

    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;  // For minUniformBufferOffsetAlignment
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        VkRenderPass renderPass;  // HDR render pass
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        AddCommonBindingsCallback addCommonBindings;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Resources needed for descriptor set writing
    struct DescriptorResources {
        const GlobalBufferManager* globalBufferManager;

        // Shadow system resources
        VkImageView shadowMapView;
        VkSampler shadowMapSampler;
        VkImageView emissiveMapView;
        VkSampler emissiveMapSampler;
        std::vector<VkImageView>* pointShadowViews;  // Per-frame
        VkSampler pointShadowSampler;
        std::vector<VkImageView>* spotShadowViews;   // Per-frame
        VkSampler spotShadowSampler;
        VkImageView snowMaskView;
        VkSampler snowMaskSampler;

        // Placeholder textures
        VkImageView whiteTextureView;
        VkSampler whiteTextureSampler;

        // Player material textures (from MaterialRegistry based on player's materialId)
        VkImageView playerDiffuseView;
        VkSampler playerDiffuseSampler;
        VkImageView playerNormalView;
        VkSampler playerNormalSampler;
    };

    /**
     * Factory: Create and initialize SkinnedMeshRenderer.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<SkinnedMeshRenderer> create(const InitInfo& info);


    ~SkinnedMeshRenderer();

    // Non-copyable, non-movable
    SkinnedMeshRenderer(const SkinnedMeshRenderer&) = delete;
    SkinnedMeshRenderer& operator=(const SkinnedMeshRenderer&) = delete;
    SkinnedMeshRenderer(SkinnedMeshRenderer&&) = delete;
    SkinnedMeshRenderer& operator=(SkinnedMeshRenderer&&) = delete;

    // Create descriptor sets after all resources are ready
    bool createDescriptorSets(const DescriptorResources& resources);

    // Update cloud shadow binding after cloud shadow system is initialized
    void updateCloudShadowBinding(VkImageView cloudShadowView, VkSampler cloudShadowSampler);

    /**
     * Update bone matrices for a character at a specific slot.
     * @param frameIndex Current frame index for triple-buffered resources
     * @param slotIndex Character slot (0 to MAX_SKINNED_CHARACTERS-1)
     * @param character Character to get bone matrices from (can be null for identity)
     */
    void updateBoneMatrices(uint32_t frameIndex, uint32_t slotIndex, AnimatedCharacter* character);

    /**
     * Record draw commands for skinned character using dynamic offset.
     * Uses VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC to select the correct
     * bone matrix slot for this character at draw time.
     *
     * @param cmd Command buffer to record to
     * @param frameIndex Current frame index
     * @param slotIndex Character slot containing this character's bone matrices
     * @param playerObj Renderable with transform and material properties
     * @param character Character for mesh data
     */
    void record(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t slotIndex,
                const Renderable& playerObj, AnimatedCharacter& character);

    /**
     * Record draw commands with explicit transform (Phase 6: ECS-compatible).
     * This version does not require a Renderable, only the transform matrix.
     *
     * @param cmd Command buffer to record to
     * @param frameIndex Current frame index
     * @param slotIndex Character slot containing this character's bone matrices
     * @param transform World transform matrix for the character
     * @param character Character for mesh data
     */
    void record(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t slotIndex,
                const glm::mat4& transform, AnimatedCharacter& character);

    /**
     * Record draw commands with explicit LOD mesh using dynamic offset.
     */
    void recordWithLOD(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t slotIndex,
                       const Renderable& playerObj, AnimatedCharacter& character,
                       const CharacterLODMesh& lodMesh);

    /**
     * Record draw commands with explicit LOD mesh (Phase 6: ECS-compatible).
     */
    void recordWithLOD(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t slotIndex,
                       const glm::mat4& transform, AnimatedCharacter& character,
                       const CharacterLODMesh& lodMesh);

    // Get the maximum number of character slots
    static constexpr uint32_t getMaxSlots() { return MAX_SKINNED_CHARACTERS; }

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { extent = newExtent; }

    // Accessors for ShadowSystem integration
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout_ ? **descriptorSetLayout_ : VK_NULL_HANDLE; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout_ ? **pipelineLayout_ : VK_NULL_HANDLE; }
    VkPipeline getPipeline() const { return pipeline_ ? **pipeline_ : VK_NULL_HANDLE; }
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

private:
    bool initInternal(const InitInfo& info);
    void cleanup();
    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createBoneMatricesBuffers();

    // Vulkan handles (stored, not owned)
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkExtent2D extent{};
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    AddCommonBindingsCallback addCommonBindings;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Created resources (RAII-managed)
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;

    std::vector<VkDescriptorSet> descriptorSets;

    // Multi-slot dynamic buffer for bone matrices
    // Supports MAX_SKINNED_CHARACTERS slots per frame, selected via dynamic offset
    BufferUtils::MultiSlotDynamicBuffer boneMatricesBuffer_;
};
