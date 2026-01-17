#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "DescriptorManager.h"
#include "InitContext.h"
#include "VmaResources.h"
#include "BufferUtils.h"
#include "interfaces/ICloudShadowControl.h"

// Cloud Shadow System
// Generates a world-space cloud shadow map by ray-marching through the cloud layer
// from the sun's perspective. This provides high-fidelity, animated cloud shadows
// that properly account for cloud density, height, and movement.

// Uniforms for cloud shadow compute shader (must match GLSL layout)
// alignas(16) required for SIMD operations on glm::mat4
struct alignas(16) CloudShadowUniforms {
    glm::mat4 worldToShadowUV;   // Transform world XZ to shadow map UV
    glm::vec4 toSunDirection;      // xyz = direction toward sun, w = intensity
    glm::vec4 windOffset;        // xyz = wind offset for cloud animation, w = time
    glm::vec4 shadowParams;      // x = shadow intensity, y = softness, z = cloud height, w = cloud thickness
    glm::vec4 worldBounds;       // xy = world min XZ, zw = world size XZ
    float cloudCoverage;         // Cloud coverage amount
    float cloudDensity;          // Cloud density multiplier
    float shadowBias;            // Shadow bias to prevent acne
    float padding;
};

class CloudShadowSystem : public ICloudShadowControl {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        std::string shaderPath;
        uint32_t framesInFlight;
        VkImageView cloudMapLUTView;  // From AtmosphereLUTSystem
        VkSampler cloudMapLUTSampler; // From AtmosphereLUTSystem
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Cloud shadow map dimensions
    // 1024x1024 provides good balance of quality and performance
    // Covers a 500m x 500m world area (matching terrain size)
    static constexpr uint32_t SHADOW_MAP_SIZE = 1024;

    // World coverage (should match terrain size)
    static constexpr float WORLD_SIZE = 500.0f;

    // Cloud layer parameters (matching sky.frag)
    static constexpr float CLOUD_LAYER_BOTTOM = 1500.0f;  // 1.5km in world units = 1500m
    static constexpr float CLOUD_LAYER_TOP = 4000.0f;     // 4.0km in world units = 4000m

    /**
     * Factory: Create and initialize CloudShadowSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<CloudShadowSystem> create(const InitInfo& info);
    static std::unique_ptr<CloudShadowSystem> create(const InitContext& ctx, VkImageView cloudMapLUTView, VkSampler cloudMapLUTSampler);

    ~CloudShadowSystem();

    // Non-copyable, non-movable
    CloudShadowSystem(const CloudShadowSystem&) = delete;
    CloudShadowSystem& operator=(const CloudShadowSystem&) = delete;
    CloudShadowSystem(CloudShadowSystem&&) = delete;
    CloudShadowSystem& operator=(CloudShadowSystem&&) = delete;

    // Update cloud shadow map (call before scene rendering)
    void recordUpdate(VkCommandBuffer cmd, uint32_t frameIndex,
                      const glm::vec3& sunDir, float sunIntensity,
                      const glm::vec3& windOffset, float windTime,
                      const glm::vec3& cameraPos);

    // Accessors for shader binding
    VkImageView getShadowMapView() const { return shadowMapView_ ? **shadowMapView_ : VK_NULL_HANDLE; }
    VkSampler getShadowMapSampler() const { return shadowMapSampler_ ? **shadowMapSampler_ : VK_NULL_HANDLE; }

    // Get the world-to-shadow-UV matrix for sampling in fragment shaders
    const glm::mat4& getWorldToShadowUV() const { return worldToShadowUV; }

    // ICloudShadowControl implementation
    void setEnabled(bool e) override { enabled = e; }
    bool isEnabled() const override { return enabled; }
    void setShadowIntensity(float intensity) override { shadowIntensity = glm::clamp(intensity, 0.0f, 1.0f); }
    float getShadowIntensity() const override { return shadowIntensity; }

    // Additional control parameters
    void setShadowSoftness(float softness) { shadowSoftness = glm::clamp(softness, 0.0f, 1.0f); }
    float getShadowSoftness() const { return shadowSoftness; }

    void setCloudCoverage(float coverage) { cloudCoverage = glm::clamp(coverage, 0.0f, 1.0f); }
    float getCloudCoverage() const { return cloudCoverage; }

    void setCloudDensity(float density) { cloudDensity = glm::clamp(density, 0.0f, 2.0f); }
    float getCloudDensity() const { return cloudDensity; }

private:
    bool createShadowMap();
    bool createSampler();
    bool createDescriptorSetLayout();
    bool createDescriptorSets();
    bool createUniformBuffers();
    bool createComputePipeline();

    void updateWorldToShadowMatrix(const glm::vec3& sunDir, const glm::vec3& cameraPos);

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // External resources (not owned)
    VkImageView cloudMapLUTView = VK_NULL_HANDLE;
    VkSampler cloudMapLUTSampler = VK_NULL_HANDLE;

    // Cloud shadow map (R16F - stores shadow attenuation 0=full shadow, 1=no shadow)
    ManagedImage shadowMap_;  // VMA-managed image (keep as-is)
    std::optional<vk::raii::ImageView> shadowMapView_;
    std::optional<vk::raii::Sampler> shadowMapSampler_;

    // Compute pipeline (RAII-managed)
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> computePipeline_;
    std::vector<VkDescriptorSet> descriptorSets;

    // Uniform buffers (per frame)
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // World to shadow UV matrix
    glm::mat4 worldToShadowUV = glm::mat4(1.0f);

    // Control parameters
    float shadowIntensity = 0.7f;   // How dark the shadows are
    float shadowSoftness = 0.3f;    // Shadow edge softness
    float cloudCoverage = 0.5f;     // Matches CLOUD_COVERAGE in sky.frag
    float cloudDensity = 0.3f;      // Matches CLOUD_DENSITY in sky.frag

    bool enabled = true;

    // Temporal spreading: update 1/4 of shadow map per frame
    uint32_t quadrantIndex = 0;  // Cycles 0-3

    CloudShadowSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();
};
