#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>

#include "VmaBuffer.h"
#include "interfaces/ITemporalSystem.h"

/**
 * WaterDisplacement - Phase 4: Vector Displacement Maps (Interactive Splashes)
 *
 * Implements dynamic water displacement from objects and particles:
 * - Displacement particle system with projected box decals
 * - Sample water depth buffer to project onto water surface
 * - Animated displacement textures (multi-frame)
 * - Edge fading to prevent displacement explosion at box edges
 * - Max alpha blend: clear texture to -FLT_MAX, blend with max
 *
 * Based on Far Cry 5's water rendering approach (GDC 2018).
 */
class WaterDisplacement : public ITemporalSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit WaterDisplacement(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue computeQueue;
        uint32_t framesInFlight;
        uint32_t displacementResolution = 512;  // Displacement map resolution
        float worldSize = 100.0f;               // World size covered by displacement map
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Splash particle for displacement
    struct SplashParticle {
        glm::vec3 position;       // World position of splash center
        float radius;             // Splash radius in world units
        float intensity;          // Displacement height (positive = up, negative = down)
        float age;                // Current age (0-1, normalized)
        float lifetime;           // Total lifetime in seconds
        float falloff;            // Edge falloff exponent
        uint32_t animFrame;       // Current animation frame (for multi-frame splashes)
    };

    // Push constants for compute shader
    struct DisplacementPushConstants {
        glm::vec4 worldExtent;    // xy = center, zw = size
        float time;
        float deltaTime;
        uint32_t numParticles;
        float decayRate;          // How fast old displacements fade
    };

    /**
     * Factory: Create and initialize WaterDisplacement.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<WaterDisplacement> create(const InitInfo& info);


    ~WaterDisplacement();

    // Non-copyable, non-movable
    WaterDisplacement(const WaterDisplacement&) = delete;
    WaterDisplacement& operator=(const WaterDisplacement&) = delete;
    WaterDisplacement(WaterDisplacement&&) = delete;
    WaterDisplacement& operator=(WaterDisplacement&&) = delete;

    // Add a splash at world position
    void addSplash(const glm::vec3& position, float radius, float intensity, float lifetime = 1.0f);

    // Add ripple (expanding ring)
    void addRipple(const glm::vec3& position, float radius, float intensity, float speed = 2.0f);

    // Update displacement (call each frame)
    void update(float deltaTime);

    // Record compute shader dispatch
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex);

    // Get displacement map for sampling in water shader
    VkImageView getDisplacementMapView() const { return displacementMapView_ ? **displacementMapView_ : VK_NULL_HANDLE; }
    VkSampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    // Get descriptor set for water shader binding
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout_ ? **descriptorSetLayout_ : VK_NULL_HANDLE; }
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

    // Configuration
    void setWorldExtent(const glm::vec2& center, const glm::vec2& size);
    void setDecayRate(float rate) { decayRate = rate; }
    float getDecayRate() const { return decayRate; }

    // Clear all particles and reset displacement map
    void clear();

    // ITemporalSystem: Reset temporal history to prevent ghost frames
    void resetTemporalHistory() override { clear(); }

    // Get current particle count
    size_t getParticleCount() const { return particles.size(); }

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createDisplacementMap();
    bool createComputePipeline();
    bool createParticleBuffer();
    bool createDescriptorSets();
    void updateParticleBuffer(uint32_t frameIndex);

    // Device handles
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Configuration
    uint32_t framesInFlight = 0;
    uint32_t displacementResolution = 512;
    float worldSize = 100.0f;
    glm::vec2 worldCenter = glm::vec2(0.0f);
    float decayRate = 2.0f;  // Displacement decay per second

    // Displacement map (R16F - single channel height)
    VkImage displacementMap = VK_NULL_HANDLE;
    std::optional<vk::raii::ImageView> displacementMapView_;
    VmaAllocation displacementAllocation = VK_NULL_HANDLE;

    // Previous frame displacement (for temporal blending)
    VkImage prevDisplacementMap = VK_NULL_HANDLE;
    std::optional<vk::raii::ImageView> prevDisplacementMapView_;
    VmaAllocation prevDisplacementAllocation = VK_NULL_HANDLE;

    // Sampler (RAII-managed)
    std::optional<vk::raii::Sampler> sampler_;

    // Compute pipeline (RAII-managed)
    std::optional<vk::raii::Pipeline> computePipeline_;
    std::optional<vk::raii::PipelineLayout> computePipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::DescriptorPool> descriptorPool_;
    std::vector<VkDescriptorSet> descriptorSets;

    // Particle buffer (SSBO, RAII-managed)
    static constexpr uint32_t MAX_PARTICLES = 256;
    std::vector<ManagedBuffer> particleBuffers_;
    std::vector<void*> particleMapped;

    // Active particles
    std::vector<SplashParticle> particles;
    float currentTime = 0.0f;
};
