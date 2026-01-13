#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>
#include <optional>
#include <memory>

#include "Mesh.h"
#include "Texture.h"
#include "DescriptorManager.h"
#include "VmaResources.h"
#include "core/FrameBuffered.h"
#include "core/material/MaterialComponents.h"

class ShadowSystem;

class WaterSystem {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkRenderPass hdrRenderPass;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkExtent2D extent;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        float waterSize = 100.0f;  // Size of water plane in world units
        std::string assetPath;     // Base path for assets (for foam texture)
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Water material properties for blending (Phase 12)
    // Subset of properties that define a water type's appearance
    struct WaterMaterial {
        glm::vec4 waterColor;       // rgb = base water color, a = transparency
        glm::vec4 scatteringCoeffs; // rgb = absorption coefficients, a = turbidity
        float absorptionScale;      // How quickly light is absorbed with depth
        float scatteringScale;      // How much light scatters (turbidity multiplier)
        float specularRoughness;    // Base roughness for specular
        float sssIntensity;         // Subsurface scattering intensity
    };

    // Water uniforms - must match shader layout
    struct WaterUniforms {
        // Primary material properties
        glm::vec4 waterColor;      // rgb = base water color, a = transparency
        glm::vec4 waveParams;      // x = amplitude, y = wavelength, z = steepness, w = speed
        glm::vec4 waveParams2;     // Second wave layer parameters
        glm::vec4 waterExtent;     // xy = position offset, zw = size
        glm::vec4 scatteringCoeffs; // rgb = absorption coefficients, a = turbidity

        // Phase 12: Secondary material for blending
        glm::vec4 waterColor2;      // Secondary water color
        glm::vec4 scatteringCoeffs2; // Secondary scattering coefficients
        glm::vec4 blendCenter;      // xy = world position, z = blend direction angle, w = unused
        float absorptionScale2;     // Secondary absorption scale
        float scatteringScale2;     // Secondary scattering scale
        float specularRoughness2;   // Secondary specular roughness
        float sssIntensity2;        // Secondary SSS intensity
        float blendDistance;        // Distance over which materials blend (world units)
        int blendMode;              // 0 = distance from center, 1 = directional, 2 = radial

        float waterLevel;          // Y height of water plane
        float foamThreshold;       // Wave height threshold for foam
        float fresnelPower;        // Fresnel reflection power
        float terrainSize;         // Terrain size for UV calculation
        float terrainHeightScale;  // Terrain height scale
        float shoreBlendDistance;  // Distance over which shore fades (world units)
        float shoreFoamWidth;      // Width of shore foam band (world units)
        float flowStrength;        // How much flow affects UV offset (world units)
        float flowSpeed;           // Flow animation speed multiplier
        float flowFoamStrength;    // How much flow speed affects foam
        float fbmNearDistance;     // Distance for max FBM detail (9 octaves)
        float fbmFarDistance;      // Distance for min FBM detail (3 octaves)
        float specularRoughness;   // Base roughness for specular (0 = mirror, 1 = diffuse)
        float absorptionScale;     // How quickly light is absorbed with depth
        float scatteringScale;     // How much light scatters (turbidity multiplier)
        float displacementScale;   // Scale for interactive displacement (Phase 4)
        float sssIntensity;        // Subsurface scattering intensity (Phase 17)
        float causticsScale;       // Caustics pattern scale (Phase 9)
        float causticsSpeed;       // Caustics animation speed (Phase 9)
        float causticsIntensity;   // Caustics brightness (Phase 9)
        float nearPlane;           // Camera near plane for depth linearization
        float farPlane;            // Camera far plane for depth linearization
        float padding1;            // Padding for alignment
        float padding2;            // Padding for alignment
    };

    /**
     * Factory: Create and initialize WaterSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<WaterSystem> create(const InitInfo& info);

    ~WaterSystem();

    // Non-copyable, non-movable
    WaterSystem(const WaterSystem&) = delete;
    WaterSystem& operator=(const WaterSystem&) = delete;
    WaterSystem(WaterSystem&&) = delete;
    WaterSystem& operator=(WaterSystem&&) = delete;

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { extent = newExtent; }

    // Create descriptor sets after main UBO is ready
    bool createDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                              VkDeviceSize uniformBufferSize,
                              ShadowSystem& shadowSystem,
                              VkImageView terrainHeightMapView,
                              VkSampler terrainHeightMapSampler,
                              VkImageView flowMapView,
                              VkSampler flowMapSampler,
                              VkImageView displacementMapView,
                              VkSampler displacementMapSampler,
                              VkImageView temporalFoamView,
                              VkSampler temporalFoamSampler,
                              VkImageView ssrView,
                              VkSampler ssrSampler,
                              VkImageView sceneDepthView,
                              VkSampler sceneDepthSampler,
                              VkImageView tileArrayView = VK_NULL_HANDLE,
                              VkSampler tileSampler = VK_NULL_HANDLE,
                              const std::array<VkBuffer, 3>& tileInfoBuffers = {},
                              VkImageView envCubemapView = VK_NULL_HANDLE,
                              VkSampler envCubemapSampler = VK_NULL_HANDLE);

    // Update water uniforms (call each frame)
    void updateUniforms(uint32_t frameIndex);

    // Record water rendering commands
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex);

    // Record just mesh draw (for G-buffer pass with external pipeline)
    void recordMeshDraw(VkCommandBuffer cmd);

    // Configuration
    void setWaterLevel(float level) { baseWaterLevel = level; waterUniforms.waterLevel = level; }
    void setWaterColor(const glm::vec4& color) { waterUniforms.waterColor = color; }
    void setWaveAmplitude(float amplitude) { waterUniforms.waveParams.x = amplitude; }
    void setWaveLength(float wavelength) { waterUniforms.waveParams.y = wavelength; }
    void setWaveSteepness(float steepness) { waterUniforms.waveParams.z = steepness; }
    void setWaveSpeed(float speed) { waterUniforms.waveParams.w = speed; }
    void setWaterExtent(const glm::vec2& position, const glm::vec2& size);

    // Tidal configuration
    void setTidalRange(float range) { tidalRange = range; }
    void updateTide(float tideHeight);  // tideHeight is -1 to +1, scaled by tidalRange

    // Getters for UI
    float getWaterLevel() const { return waterUniforms.waterLevel; }
    float getBaseWaterLevel() const { return baseWaterLevel; }
    float getTidalRange() const { return tidalRange; }

    // Foam texture accessors (can be used for terrain caustics)
    VkImageView getFoamTextureView() const { return foamTexture ? foamTexture->getImageView() : VK_NULL_HANDLE; }
    VkSampler getFoamTextureSampler() const { return foamTexture ? foamTexture->getSampler() : VK_NULL_HANDLE; }
    glm::vec4 getWaterColor() const { return waterUniforms.waterColor; }
    float getWaveAmplitude() const { return waterUniforms.waveParams.x; }
    float getWaveLength() const { return waterUniforms.waveParams.y; }
    float getWaveSteepness() const { return waterUniforms.waveParams.z; }
    float getWaveSpeed() const { return waterUniforms.waveParams.w; }
    float getFoamThreshold() const { return waterUniforms.foamThreshold; }
    float getFresnelPower() const { return waterUniforms.fresnelPower; }

    void setFoamThreshold(float threshold) { waterUniforms.foamThreshold = threshold; }
    void setFresnelPower(float power) { waterUniforms.fresnelPower = power; }

    // Terrain integration
    void setTerrainParams(float size, float heightScale) {
        waterUniforms.terrainSize = size;
        waterUniforms.terrainHeightScale = heightScale;
    }
    void setShoreBlendDistance(float distance) { waterUniforms.shoreBlendDistance = distance; }
    void setShoreFoamWidth(float width) { waterUniforms.shoreFoamWidth = width; }
    float getShoreBlendDistance() const { return waterUniforms.shoreBlendDistance; }
    float getShoreFoamWidth() const { return waterUniforms.shoreFoamWidth; }

    // Flow map parameters
    void setFlowStrength(float strength) { waterUniforms.flowStrength = strength; }
    void setFlowSpeed(float speed) { waterUniforms.flowSpeed = speed; }
    void setFlowFoamStrength(float strength) { waterUniforms.flowFoamStrength = strength; }
    float getFlowStrength() const { return waterUniforms.flowStrength; }
    float getFlowSpeed() const { return waterUniforms.flowSpeed; }
    float getFlowFoamStrength() const { return waterUniforms.flowFoamStrength; }

    // FBM LOD parameters
    void setFBMLODDistances(float nearDist, float farDist) {
        waterUniforms.fbmNearDistance = nearDist;
        waterUniforms.fbmFarDistance = farDist;
    }
    float getFBMNearDistance() const { return waterUniforms.fbmNearDistance; }
    float getFBMFarDistance() const { return waterUniforms.fbmFarDistance; }

    // PBR Scattering parameters (Phase 8)
    void setScatteringCoeffs(const glm::vec3& absorption, float turbidity) {
        waterUniforms.scatteringCoeffs = glm::vec4(absorption, turbidity);
    }
    glm::vec3 getAbsorptionCoeffs() const { return glm::vec3(waterUniforms.scatteringCoeffs); }
    float getTurbidity() const { return waterUniforms.scatteringCoeffs.a; }
    void setAbsorptionScale(float scale) { waterUniforms.absorptionScale = scale; }
    void setScatteringScale(float scale) { waterUniforms.scatteringScale = scale; }
    float getAbsorptionScale() const { return waterUniforms.absorptionScale; }
    float getScatteringScale() const { return waterUniforms.scatteringScale; }

    // Specular parameters (Phase 6)
    void setSpecularRoughness(float roughness) { waterUniforms.specularRoughness = roughness; }
    float getSpecularRoughness() const { return waterUniforms.specularRoughness; }

    // Displacement parameters (Phase 4)
    void setDisplacementScale(float scale) { waterUniforms.displacementScale = scale; }
    float getDisplacementScale() const { return waterUniforms.displacementScale; }

    // Subsurface scattering parameters (Phase 17)
    void setSSSIntensity(float intensity) { waterUniforms.sssIntensity = intensity; }
    float getSSSIntensity() const { return waterUniforms.sssIntensity; }

    // Caustics parameters (Phase 9)
    void setCausticsScale(float scale) { waterUniforms.causticsScale = scale; }
    void setCausticsSpeed(float speed) { waterUniforms.causticsSpeed = speed; }
    void setCausticsIntensity(float intensity) { waterUniforms.causticsIntensity = intensity; }
    float getCausticsScale() const { return waterUniforms.causticsScale; }
    float getCausticsSpeed() const { return waterUniforms.causticsSpeed; }
    float getCausticsIntensity() const { return waterUniforms.causticsIntensity; }

    // Camera planes for depth linearization (needed for soft edges, intersection foam)
    void setCameraPlanes(float near, float far) { waterUniforms.nearPlane = near; waterUniforms.farPlane = far; }
    float getNearPlane() const { return waterUniforms.nearPlane; }
    float getFarPlane() const { return waterUniforms.farPlane; }

    // FFT Ocean mode (Tessendorf simulation vs Gerstner waves)
    void setUseFFTOcean(bool enabled, float size0 = 256.0f, float size1 = 64.0f, float size2 = 16.0f) {
        pushConstants.useFFTOcean = enabled ? 1 : 0;
        pushConstants.oceanSize0 = size0;
        pushConstants.oceanSize1 = size1;
        pushConstants.oceanSize2 = size2;
    }
    bool getUseFFTOcean() const { return pushConstants.useFFTOcean != 0; }
    float getOceanSize0() const { return pushConstants.oceanSize0; }
    float getOceanSize1() const { return pushConstants.oceanSize1; }
    float getOceanSize2() const { return pushConstants.oceanSize2; }

    // Tessellation mode (GPU tessellation for wave geometry detail)
    void setUseTessellation(bool enabled) { useTessellation_ = enabled; }
    bool getUseTessellation() const { return useTessellation_; }
    bool isTessellationSupported() const { return tessellationPipeline_.has_value(); }

    // Get uniform buffers (for G-buffer pass descriptor sets)
    VkBuffer getUniformBuffer(size_t frameIndex) const { return waterUniformBuffers_[frameIndex].get(); }
    std::vector<VkBuffer> getUniformBuffers() const {
        std::vector<VkBuffer> buffers;
        for (const auto& buf : waterUniformBuffers_) {
            buffers.push_back(buf.get());
        }
        return buffers;
    }
    static VkDeviceSize getUniformBufferSize() { return sizeof(WaterUniforms); }

    // Water type presets (based on Far Cry 5 approach)
    enum class WaterType {
        Ocean,          // Deep blue, low turbidity, clear
        CoastalOcean,   // Blue-green, medium turbidity
        River,          // Green-blue, variable turbidity
        MuddyRiver,     // Brown, high turbidity
        ClearStream,    // Very clear, low absorption
        Lake,           // Dark blue-green, medium
        Swamp,          // Dark green-brown, high turbidity
        Tropical        // Turquoise, very clear
    };
    void setWaterType(WaterType type);

    // Phase 12: Material blending
    // Blend modes for material transitions
    enum class BlendMode {
        Distance,    // Blend based on distance from center point
        Directional, // Blend along a direction (e.g., river to ocean)
        Radial       // Blend radially outward from center
    };

    // Get material preset by water type
    WaterMaterial getMaterialPreset(WaterType type) const;

    // Set primary and secondary materials for blending
    void setPrimaryMaterial(const WaterMaterial& material);
    void setSecondaryMaterial(const WaterMaterial& material);
    void setPrimaryMaterial(WaterType type);
    void setSecondaryMaterial(WaterType type);

    // New composable material API using LiquidComponent
    void setPrimaryLiquid(const material::LiquidComponent& liquid);
    void setSecondaryLiquid(const material::LiquidComponent& liquid);

    // Get current materials as LiquidComponent
    material::LiquidComponent getPrimaryLiquid() const;
    material::LiquidComponent getSecondaryLiquid() const;

    // Configure blend parameters
    void setBlendCenter(const glm::vec2& worldPos) { waterUniforms.blendCenter.x = worldPos.x; waterUniforms.blendCenter.y = worldPos.y; }
    void setBlendDirection(float angleRadians) { waterUniforms.blendCenter.z = angleRadians; }
    void setBlendDistance(float distance) { waterUniforms.blendDistance = distance; }
    void setBlendMode(BlendMode mode) { waterUniforms.blendMode = static_cast<int>(mode); }

    // Getters for blend parameters
    glm::vec2 getBlendCenter() const { return glm::vec2(waterUniforms.blendCenter.x, waterUniforms.blendCenter.y); }
    float getBlendDirection() const { return waterUniforms.blendCenter.z; }
    float getBlendDistance() const { return waterUniforms.blendDistance; }
    BlendMode getBlendMode() const { return static_cast<BlendMode>(waterUniforms.blendMode); }

    // Convenience: set up a transition between two water types
    void setupMaterialTransition(WaterType from, WaterType to, const glm::vec2& center,
                                  float distance, BlendMode mode = BlendMode::Distance);

    // =========================================================================
    // Water Volume Renderer - Underwater Detection (Phase 2)
    // =========================================================================

    // Check if a world position is underwater (below water surface level)
    bool isPositionUnderwater(const glm::vec3& worldPos) const {
        return worldPos.y < waterUniforms.waterLevel;
    }

    // Get underwater depth (positive = below water, negative = above)
    float getUnderwaterDepth(const glm::vec3& worldPos) const {
        return waterUniforms.waterLevel - worldPos.y;
    }

    // Get water parameters for underwater rendering
    // These are used by PostProcessSystem for underwater fog/absorption
    struct UnderwaterParams {
        bool isUnderwater;          // Is camera underwater
        float depth;                // Camera depth below water surface
        glm::vec3 absorptionCoeffs; // Beer-Lambert absorption (RGB)
        float turbidity;            // Scattering/turbidity factor
        glm::vec4 waterColor;       // Base water color
        float waterLevel;           // Water surface height
    };

    UnderwaterParams getUnderwaterParams(const glm::vec3& cameraPos) const {
        UnderwaterParams params{};
        params.isUnderwater = isPositionUnderwater(cameraPos);
        params.depth = getUnderwaterDepth(cameraPos);
        params.absorptionCoeffs = glm::vec3(waterUniforms.scatteringCoeffs);
        params.turbidity = waterUniforms.scatteringCoeffs.a;
        params.waterColor = waterUniforms.waterColor;
        params.waterLevel = waterUniforms.waterLevel;
        return params;
    }

private:
    WaterSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createWaterMesh();
    bool createUniformBuffers();
    bool loadFoamTexture();
    bool loadCausticsTexture();

    // Initialization info
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkRenderPass hdrRenderPass = VK_NULL_HANDLE;
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    VkExtent2D extent = {0, 0};
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    float waterSize = 100.0f;
    std::string assetPath;

    // Pipeline resources (RAII-managed)
    std::optional<vk::raii::Pipeline> pipeline_;
    std::optional<vk::raii::Pipeline> tessellationPipeline_;  // GPU tessellation pipeline for wave detail
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::vector<VkDescriptorSet> descriptorSets;
    bool useTessellation_ = false;  // Whether to use tessellation when supported

    // RAII device pointer
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Water mesh (a subdivided plane for wave animation) - RAII-managed
    std::unique_ptr<Mesh> waterMesh;
    glm::mat4 waterModelMatrix = glm::mat4(1.0f);

    // Water uniforms (RAII-managed)
    WaterUniforms waterUniforms{};
    std::vector<ManagedBuffer> waterUniformBuffers_;
    std::vector<void*> waterUniformMapped;

    // Foam texture (tileable Worley noise) - RAII-managed
    std::unique_ptr<Texture> foamTexture;

    // Caustics texture (Phase 9) - RAII-managed
    std::unique_ptr<Texture> causticsTexture;

    // Tidal parameters
    float baseWaterLevel = 0.0f;  // Mean sea level
    float tidalRange = 2.0f;      // Max tide height variation in meters

    // Tile cache resources for high-res terrain sampling
    TripleBuffered<VkBuffer> tileInfoBuffers_;  // Triple-buffered for frames-in-flight sync

    // Push constants - must match shader layout exactly
    struct PushConstants {
        glm::mat4 model;
        int32_t useFFTOcean;   // 0 = Gerstner, 1 = FFT ocean
        float oceanSize0;      // FFT cascade 0 patch size
        float oceanSize1;      // FFT cascade 1 patch size
        float oceanSize2;      // FFT cascade 2 patch size
    };
    PushConstants pushConstants{};
};
