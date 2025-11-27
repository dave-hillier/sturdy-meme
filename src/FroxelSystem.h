#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

// Froxel-based volumetric fog system (Phase 4.3)
// Implements frustum-aligned voxel grid for efficient volumetric rendering

static constexpr uint32_t FROXEL_NUM_CASCADES = 4;

struct FroxelUniforms {
    glm::mat4 invViewProj;        // Inverse view-projection for world pos reconstruction
    glm::mat4 prevViewProj;       // Previous frame's view-proj for temporal reprojection
    glm::mat4 cascadeViewProj[FROXEL_NUM_CASCADES];  // Light-space matrices for shadow cascades
    glm::vec4 cascadeSplits;      // View-space split depths for cascade selection
    glm::vec4 cameraPosition;     // xyz = camera pos, w = unused
    glm::vec4 sunDirection;       // xyz = sun dir, w = sun intensity
    glm::vec4 sunColor;           // rgb = sun color
    glm::vec4 fogParams;          // x = base height, y = scale height, z = density, w = absorption
    glm::vec4 layerParams;        // x = layer height, y = layer thickness, z = layer density, w = unused
    glm::vec4 gridParams;         // x = volumetric far plane, y = depth distribution, z = frame index, w = unused
    glm::vec4 shadowParams;       // x = shadow map size, y = shadow bias, z = pcf radius, w = unused
};

class FroxelSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkDescriptorPool descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkImageView shadowMapView;       // Cascaded shadow map array view
        VkSampler shadowSampler;         // Shadow sampler with comparison
    };

    // Froxel grid dimensions (from Phase 4.3)
    static constexpr uint32_t FROXEL_WIDTH = 128;
    static constexpr uint32_t FROXEL_HEIGHT = 64;
    static constexpr uint32_t FROXEL_DEPTH = 64;

    // Depth distribution factor (each slice ~20% thicker than previous)
    static constexpr float DEPTH_DISTRIBUTION = 1.2f;

    FroxelSystem() = default;
    ~FroxelSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);
    void resize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent);

    // Update froxel volume (call before scene rendering)
    void recordFroxelUpdate(VkCommandBuffer cmd, uint32_t frameIndex,
                           const glm::mat4& view, const glm::mat4& proj,
                           const glm::vec3& cameraPos,
                           const glm::vec3& sunDir, float sunIntensity,
                           const glm::vec3& sunColor,
                           const glm::mat4* cascadeMatrices,
                           const glm::vec4& cascadeSplits);

    // Get the scattering volume for compositing
    VkImageView getScatteringVolumeView() const { return scatteringVolumeView; }
    VkSampler getVolumeSampler() const { return volumeSampler; }

    // Fog parameters
    void setFogBaseHeight(float h) { fogBaseHeight = h; }
    float getFogBaseHeight() const { return fogBaseHeight; }
    void setFogScaleHeight(float h) { fogScaleHeight = h; }
    float getFogScaleHeight() const { return fogScaleHeight; }
    void setFogDensity(float d) { fogDensity = d; }
    float getFogDensity() const { return fogDensity; }
    void setFogAbsorption(float a) { fogAbsorption = a; }
    float getFogAbsorption() const { return fogAbsorption; }

    // Ground fog layer parameters
    void setLayerHeight(float h) { layerHeight = h; }
    float getLayerHeight() const { return layerHeight; }
    void setLayerThickness(float t) { layerThickness = t; }
    float getLayerThickness() const { return layerThickness; }
    void setLayerDensity(float d) { layerDensity = d; }
    float getLayerDensity() const { return layerDensity; }

    void setVolumetricFarPlane(float f) { volumetricFarPlane = f; }
    float getVolumetricFarPlane() const { return volumetricFarPlane; }

    // Is the system enabled?
    void setEnabled(bool e) { enabled = e; }
    bool isEnabled() const { return enabled; }

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
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    // External resources (not owned)
    VkImageView shadowMapView = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;

    // Scattering volume (stores in-scattered light / opacity)
    // Format: RGB11F - L/alpha for anti-aliasing
    VkImage scatteringVolume = VK_NULL_HANDLE;
    VmaAllocation scatteringAllocation = VK_NULL_HANDLE;
    VkImageView scatteringVolumeView = VK_NULL_HANDLE;

    // Integrated scattering volume (front-to-back integrated)
    VkImage integratedVolume = VK_NULL_HANDLE;
    VmaAllocation integratedAllocation = VK_NULL_HANDLE;
    VkImageView integratedVolumeView = VK_NULL_HANDLE;

    // Volume sampler (trilinear filtering)
    VkSampler volumeSampler = VK_NULL_HANDLE;

    // Compute pipelines
    VkDescriptorSetLayout froxelDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout froxelPipelineLayout = VK_NULL_HANDLE;
    VkPipeline froxelUpdatePipeline = VK_NULL_HANDLE;
    VkPipeline integrationPipeline = VK_NULL_HANDLE;

    std::vector<VkDescriptorSet> froxelDescriptorSets;

    // Uniform buffers (per frame)
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformAllocations;
    std::vector<void*> uniformMappedPtrs;

    // Previous view-proj for temporal reprojection
    glm::mat4 prevViewProj = glm::mat4(1.0f);
    uint32_t frameCounter = 0;

    // Fog parameters
    float fogBaseHeight = 0.0f;
    float fogScaleHeight = 50.0f;
    float fogDensity = 0.01f;
    float fogAbsorption = 0.01f;

    // Ground fog layer
    float layerHeight = 0.0f;
    float layerThickness = 10.0f;
    float layerDensity = 0.02f;

    // Volumetric range
    float volumetricFarPlane = 200.0f;

    bool enabled = true;
};
