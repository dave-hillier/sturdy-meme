#pragma once

#include "GrassConstants.h"
#include "PerFrameBuffer.h"
#include "DescriptorManager.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <optional>

struct InitContext;
struct EnvironmentSettings;

/**
 * Displacement source for vegetation interaction (player, NPCs, etc.)
 * Used by both grass and leaf systems to respond to entity movement.
 */
struct DisplacementSource {
    glm::vec4 positionAndRadius;   // xyz = world position, w = radius
    glm::vec4 strengthAndVelocity; // x = strength, yzw = velocity (for directional push)
};

/**
 * DisplacementSystem - Standalone system for vegetation displacement
 *
 * Manages a displacement texture that tracks how vegetation should bend
 * in response to player/NPC movement. The texture is updated each frame
 * via compute shader and sampled by grass and leaf systems.
 *
 * Extracted from GrassSystem to:
 * - Clarify resource ownership (single owner instead of shared via getters)
 * - Enable easy addition of new systems that respond to displacement
 * - Improve testability by isolating displacement logic
 *
 * Usage:
 *   auto displacement = DisplacementSystem::create(ctx);
 *   displacement->setEnvironmentSettings(&settings);
 *
 *   // Each frame:
 *   displacement->updateSources(playerPos, playerRadius, deltaTime);
 *   displacement->recordUpdate(cmd, frameIndex);
 *
 *   // Other systems sample via descriptor info:
 *   auto info = displacement->getDescriptorInfo();
 */
class DisplacementSystem {
public:
    struct ConstructToken { explicit ConstructToken() = default; };

    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator = VK_NULL_HANDLE;
        DescriptorManager::Pool* descriptorPool = nullptr;
        std::string shaderPath;
        uint32_t framesInFlight = 3;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    /**
     * Factory: Create and initialize DisplacementSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<DisplacementSystem> create(const InitInfo& info);

    /**
     * Factory: Create from InitContext (convenience).
     */
    static std::unique_ptr<DisplacementSystem> create(const InitContext& ctx);

    explicit DisplacementSystem(ConstructToken);
    ~DisplacementSystem();

    // Non-copyable, non-movable
    DisplacementSystem(const DisplacementSystem&) = delete;
    DisplacementSystem& operator=(const DisplacementSystem&) = delete;
    DisplacementSystem(DisplacementSystem&&) = delete;
    DisplacementSystem& operator=(DisplacementSystem&&) = delete;

    /**
     * Set environment settings for decay/max displacement parameters.
     * Must be called before recordUpdate for correct behavior.
     */
    void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings_ = settings; }

    /**
     * Update displacement sources for this frame.
     * Call before recordUpdate each frame.
     *
     * @param playerPos World position of player
     * @param playerRadius Collision radius of player
     * @param deltaTime Time since last frame (currently unused, decay is in shader)
     */
    void updateSources(const glm::vec3& playerPos, float playerRadius, float deltaTime);

    /**
     * Add a custom displacement source (NPC, projectile, etc.)
     * Call after updateSources to add additional sources.
     */
    void addSource(const DisplacementSource& source);

    /**
     * Update the region center to follow camera.
     * Call each frame before recordUpdate.
     */
    void updateRegionCenter(const glm::vec3& cameraPos);

    /**
     * Record compute shader dispatch to update displacement texture.
     * Must be called after updateSources and before grass/leaf compute.
     */
    void recordUpdate(vk::CommandBuffer cmd, uint32_t frameIndex);

    // ========================================================================
    // Accessors for consumers (GrassSystem, LeafSystem, etc.)
    // ========================================================================

    /**
     * Get descriptor info for binding displacement texture in other systems.
     * Returns combined image sampler info ready for descriptor writes.
     */
    vk::DescriptorImageInfo getDescriptorInfo() const;

    /**
     * Get image view for displacement texture.
     * Prefer getDescriptorInfo() for most use cases.
     */
    vk::ImageView getImageView() const { return imageView_; }

    /**
     * Get sampler for displacement texture.
     * Prefer getDescriptorInfo() for most use cases.
     */
    vk::Sampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    /**
     * Get current region center (world XZ coordinates).
     */
    glm::vec2 getRegionCenter() const { return regionCenter_; }

    /**
     * Get region size in world units.
     */
    float getRegionSize() const { return GrassConstants::DISPLACEMENT_REGION_SIZE; }

    /**
     * Get texel size (world units per texel).
     */
    float getTexelSize() const { return GrassConstants::DISPLACEMENT_TEXEL_SIZE; }

    /**
     * Get displacement region as vec4 for shader uniforms.
     * xy = center, z = region size, w = texel size
     */
    glm::vec4 getRegionVec4() const;

private:
    bool init(const InitInfo& info);
    void cleanup();

    bool createTexture();
    bool createPipeline();
    bool createBuffers();

    // Vulkan handles
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Displacement texture
    vk::Image image_;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    vk::ImageView imageView_;
    std::optional<vk::raii::Sampler> sampler_;

    // Compute pipeline
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;
    std::vector<vk::DescriptorSet> descriptorSets_;

    // Per-frame buffers
    BufferUtils::PerFrameBufferSet sourceBuffers_;
    BufferUtils::PerFrameBufferSet uniformBuffers_;

    // Runtime state
    glm::vec2 regionCenter_ = glm::vec2(0.0f);
    std::vector<DisplacementSource> currentSources_;
    const EnvironmentSettings* environmentSettings_ = nullptr;
};
