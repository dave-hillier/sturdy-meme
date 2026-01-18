#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "UBOs.h"
#include "PerFrameBuffer.h"
#include "DescriptorManager.h"
#include "InitContext.h"
#include "VmaImage.h"
#include "interfaces/IFogControl.h"

// Froxel-based volumetric fog system (Phase 4.3)
// Implements frustum-aligned voxel grid for efficient volumetric rendering

static constexpr uint32_t FROXEL_NUM_CASCADES = 4;

class FroxelSystem : public IFogControl {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit FroxelSystem(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkImageView shadowMapView;       // Cascaded shadow map array view
        VkSampler shadowSampler;         // Shadow sampler with comparison
        std::vector<VkBuffer> lightBuffers;  // Per-frame light buffers for local light contribution
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Froxel grid dimensions (from Phase 4.3)
    static constexpr uint32_t FROXEL_WIDTH = 128;
    static constexpr uint32_t FROXEL_HEIGHT = 64;
    static constexpr uint32_t FROXEL_DEPTH = 64;

    // Depth distribution factor (each slice ~20% thicker than previous)
    static constexpr float DEPTH_DISTRIBUTION = 1.2f;

    /**
     * Factory: Create and initialize FroxelSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<FroxelSystem> create(const InitInfo& info);
    static std::unique_ptr<FroxelSystem> create(const InitContext& ctx, VkImageView shadowMapView, VkSampler shadowSampler,
                                                 const std::vector<VkBuffer>& lightBuffers);


    ~FroxelSystem();

    // Non-copyable, non-movable
    FroxelSystem(const FroxelSystem&) = delete;
    FroxelSystem& operator=(const FroxelSystem&) = delete;
    FroxelSystem(FroxelSystem&&) = delete;
    FroxelSystem& operator=(FroxelSystem&&) = delete;
    void resize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent);

    // Update froxel volume (call before scene rendering)
    void recordFroxelUpdate(VkCommandBuffer cmd, uint32_t frameIndex,
                           const glm::mat4& view, const glm::mat4& proj,
                           const glm::vec3& cameraPos,
                           const glm::vec3& sunDir, float sunIntensity,
                           const glm::vec3& sunColor,
                           const glm::mat4* cascadeMatrices,
                           const glm::vec4& cascadeSplits);

    // Get the scattering volume (raw, pre-integration) - returns current frame's output
    VkImageView getScatteringVolumeView() const { return scatteringVolumeViews_[frameCounter % 2] ? **scatteringVolumeViews_[frameCounter % 2] : VK_NULL_HANDLE; }
    // Get the integrated volume for compositing (front-to-back integrated result)
    VkImageView getIntegratedVolumeView() const { return integratedVolumeView_ ? **integratedVolumeView_ : VK_NULL_HANDLE; }
    VkSampler getVolumeSampler() const { return volumeSampler_ ? **volumeSampler_ : VK_NULL_HANDLE; }

    // IFogControl implementation (reset temporal history on change for immediate feedback)
    void setEnabled(bool e) override { enabled = e; }
    bool isEnabled() const override { return enabled; }
    void setFogDensity(float d) override { fogDensity = d; resetTemporalHistory(); }
    float getFogDensity() const override { return fogDensity; }
    void setFogAbsorption(float a) override { fogAbsorption = a; resetTemporalHistory(); }
    float getFogAbsorption() const override { return fogAbsorption; }
    void setFogBaseHeight(float h) override { fogBaseHeight = h; resetTemporalHistory(); }
    float getFogBaseHeight() const override { return fogBaseHeight; }
    void setFogScaleHeight(float h) override { fogScaleHeight = h; resetTemporalHistory(); }
    float getFogScaleHeight() const override { return fogScaleHeight; }
    void setVolumetricFarPlane(float f) override { volumetricFarPlane = f; }
    float getVolumetricFarPlane() const override { return volumetricFarPlane; }
    void setTemporalBlend(float b) override { temporalBlend = b; }
    float getTemporalBlend() const override { return temporalBlend; }

    // Height fog layer parameters (IFogControl)
    void setLayerHeight(float h) override { layerHeight = h; resetTemporalHistory(); }
    float getLayerHeight() const override { return layerHeight; }
    void setLayerThickness(float t) override { layerThickness = t; resetTemporalHistory(); }
    float getLayerThickness() const override { return layerThickness; }
    void setLayerDensity(float d) override { layerDensity = d; resetTemporalHistory(); }
    float getLayerDensity() const override { return layerDensity; }

    // Underwater fog parameters
    void setWaterLevel(float level) { waterLevel = level; }
    float getWaterLevel() const { return waterLevel; }
    void setUnderwaterEnabled(bool e) { underwaterEnabled = e; resetTemporalHistory(); }
    bool isUnderwaterEnabled() const { return underwaterEnabled; }
    void setUnderwaterDensity(float d) { underwaterDensity = d; resetTemporalHistory(); }
    float getUnderwaterDensity() const { return underwaterDensity; }
    void setUnderwaterAbsorptionScale(float s) { underwaterAbsorptionScale = s; resetTemporalHistory(); }
    float getUnderwaterAbsorptionScale() const { return underwaterAbsorptionScale; }
    void setUnderwaterColorMult(float m) { underwaterColorMult = m; resetTemporalHistory(); }
    float getUnderwaterColorMult() const { return underwaterColorMult; }

    // Reset temporal history (call when fog parameters change significantly)
    void resetTemporalHistory() { frameCounter = 0; }

private:
    bool createScatteringVolume();
    bool createIntegratedVolume();
    bool createSampler();
    bool createDescriptorSetLayout();
    bool createDescriptorSets();
    bool createUniformBuffers();
    bool createFroxelUpdatePipeline();
    bool createIntegrationPipeline();

    void destroyVolumeResources();

    // Convert linear depth to froxel slice index
    float depthToSlice(float linearDepth) const {
        float normalized = linearDepth / volumetricFarPlane;
        return std::log(1.0f + normalized * (std::pow(DEPTH_DISTRIBUTION, FROXEL_DEPTH) - 1.0f)) /
               std::log(DEPTH_DISTRIBUTION);
    }

    // Convert slice index to linear depth
    float sliceToDepth(float slice) const {
        return volumetricFarPlane *
               (std::pow(DEPTH_DISTRIBUTION, slice) - 1.0f) /
               (std::pow(DEPTH_DISTRIBUTION, FROXEL_DEPTH) - 1.0f);
    }

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // External resources (not owned)
    VkImageView shadowMapView = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    std::vector<VkBuffer> lightBuffers;  // Per-frame light buffers

    // Double-buffered scattering volumes for temporal reprojection (ping-pong) - RAII-managed
    // Format: RGBA16F - stores in-scattered light / opacity
    // [0] = current write target, [1] = previous frame history (swapped each frame)
    ManagedImage scatteringVolumes_[2];
    std::optional<vk::raii::ImageView> scatteringVolumeViews_[2];

    // Integrated scattering volume (front-to-back integrated) - RAII-managed
    ManagedImage integratedVolume_;
    std::optional<vk::raii::ImageView> integratedVolumeView_;

    // Volume sampler (trilinear filtering)
    std::optional<vk::raii::Sampler> volumeSampler_;

    // Compute pipelines
    std::optional<vk::raii::DescriptorSetLayout> froxelDescriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> froxelPipelineLayout_;
    std::optional<vk::raii::Pipeline> froxelUpdatePipeline_;
    std::optional<vk::raii::Pipeline> integrationPipeline_;

    std::vector<VkDescriptorSet> froxelDescriptorSets;

    // Uniform buffers (per frame)
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // Previous view-proj for temporal reprojection
    glm::mat4 prevViewProj = glm::mat4(1.0f);
    uint32_t frameCounter = 0;

    // Fog parameters (matching previous constants_common.glsl values for large world)
    float fogBaseHeight = 0.0f;           // Ground level
    float fogScaleHeight = 300.0f;        // Exponential falloff height in scene units (large world)
    float fogDensity = 0.003f;            // Base fog density (reduced for large world visibility)
    float fogAbsorption = 0.003f;         // Match fog density

    // Ground fog layer
    float layerHeight = 0.0f;
    float layerThickness = 30.0f;         // Low-lying fog layer thickness
    float layerDensity = 0.008f;          // Low-lying fog density (reduced for large world)

    // Volumetric range
    float volumetricFarPlane = 200.0f;

    // Temporal filtering (0 = disabled, 0.9 = typical value for stable fog)
    float temporalBlend = 0.9f;

    // Underwater fog parameters
    float waterLevel = 0.0f;              // Water surface Y position
    bool underwaterEnabled = false;       // Is camera underwater
    float underwaterDensity = 0.02f;      // Base underwater fog density
    float underwaterAbsorptionScale = 0.5f;  // How quickly fog thickens with depth
    float underwaterColorMult = 1.5f;     // Color intensity multiplier

    bool enabled = true;

    bool initInternal(const InitInfo& info);
    void cleanup();
};
