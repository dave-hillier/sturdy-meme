#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <vector>
#include <string>

#include "DescriptorManager.h"
#include "InitContext.h"
#include "VmaImage.h"
#include "PerFrameBuffer.h"

/**
 * ScreenSpaceShadowSystem - Pre-computes shadows into a screen-space buffer
 *
 * Instead of each HDR fragment shader independently computing cascaded shadow
 * maps (9-18 texture reads per fragment per shader), this system resolves
 * shadows once per pixel in a compute pass. HDR shaders then sample a single
 * R8 texture for shadow values.
 *
 * Uses previous frame's depth buffer for world position reconstruction.
 * Runs between Shadow pass and HDR pass in the frame graph.
 */
class ScreenSpaceShadowSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit ScreenSpaceShadowSystem(ConstructToken) {}

    /**
     * Factory: Create and initialize the system.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<ScreenSpaceShadowSystem> create(const InitContext& ctx);

    ~ScreenSpaceShadowSystem();

    // Non-copyable, non-movable
    ScreenSpaceShadowSystem(const ScreenSpaceShadowSystem&) = delete;
    ScreenSpaceShadowSystem& operator=(const ScreenSpaceShadowSystem&) = delete;
    ScreenSpaceShadowSystem(ScreenSpaceShadowSystem&&) = delete;
    ScreenSpaceShadowSystem& operator=(ScreenSpaceShadowSystem&&) = delete;

    /**
     * Update per-frame uniforms. Call before recording.
     * Tracks previous frame's viewProj internally for depth reconstruction.
     */
    void updatePerFrame(uint32_t frameIndex,
                        const glm::mat4& view,
                        const glm::mat4& proj,
                        const glm::mat4 cascadeViewProj[4],
                        const glm::vec4& cascadeSplits,
                        const glm::vec3& lightDir,
                        float shadowMapSize);

    /**
     * Set the depth buffer source (previous frame's depth).
     * Must be called before the first record() and whenever depth resources change.
     */
    void setDepthSource(VkImageView depthView, VkSampler depthSampler);

    /**
     * Set the shadow map source (cascaded shadow map array).
     * Must be called before the first record() and whenever shadow resources change.
     */
    void setShadowMapSource(VkImageView shadowMapView, VkSampler shadowMapSampler);

    /**
     * Record the compute dispatch to resolve shadows.
     * The shadow buffer will be transitioned to GENERAL for writing,
     * then to SHADER_READ_ONLY_OPTIMAL for HDR shaders.
     */
    void record(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * Handle window resize.
     */
    void resize(VkExtent2D newExtent);

    // Accessors for the shadow buffer (for HDR shader descriptor binding)
    VkImageView getShadowBufferView() const;
    VkSampler getShadowBufferSampler() const;

private:
    bool initInternal(const InitContext& ctx);
    void cleanup();

    bool createShadowBuffer();
    bool createPipeline();
    bool createDescriptorSets();
    bool createUniformBuffers();
    void updateDescriptorSets();

    // UBO layout matching shadow_resolve.comp
    struct alignas(16) ShadowResolveUBO {
        glm::mat4 prevInvViewProj;
        glm::mat4 view;
        glm::mat4 cascadeViewProj[4];
        glm::vec4 cascadeSplits;
        glm::vec4 lightDir;  // xyz = to-sun direction, w = shadow map size
    };

    // Core Vulkan handles
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    const vk::raii::Device* raiiDevice_ = nullptr;
    VkExtent2D extent_ = {0, 0};
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;

    // Shadow buffer (R8_UNORM, screen resolution)
    ManagedImage shadowBufferImage_;
    std::optional<vk::raii::ImageView> shadowBufferView_;
    std::optional<vk::raii::Sampler> shadowBufferSampler_;

    // Compute pipeline
    std::optional<vk::raii::DescriptorSetLayout> descSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;

    // Per-frame descriptor sets and uniform buffers
    std::vector<VkDescriptorSet> descriptorSets_;
    BufferUtils::PerFrameBufferSet uniformBuffers_;

    // External resource references
    VkImageView depthView_ = VK_NULL_HANDLE;
    VkSampler depthSampler_ = VK_NULL_HANDLE;
    VkImageView shadowMapView_ = VK_NULL_HANDLE;
    VkSampler shadowMapSampler_ = VK_NULL_HANDLE;
    bool descriptorsNeedUpdate_ = true;

    // Previous frame tracking for temporal reprojection
    glm::mat4 prevViewProj_ = glm::mat4(1.0f);
    bool hasPrevFrame_ = false;

    static constexpr VkFormat SHADOW_BUFFER_FORMAT = VK_FORMAT_R8_UNORM;
    static constexpr uint32_t WORKGROUP_SIZE = 16;
};
