#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "Mesh.h"
#include "Texture.h"
#include "DescriptorManager.h"

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
    };

    // Water uniforms - must match shader layout
    struct WaterUniforms {
        glm::vec4 waterColor;      // rgb = base water color, a = transparency
        glm::vec4 waveParams;      // x = amplitude, y = wavelength, z = steepness, w = speed
        glm::vec4 waveParams2;     // Second wave layer parameters
        glm::vec4 waterExtent;     // xy = position offset, zw = size
        glm::vec4 scatteringCoeffs; // rgb = absorption coefficients, a = turbidity
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
    };

    WaterSystem() = default;
    ~WaterSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

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
                              VkSampler displacementMapSampler);

    // Update water uniforms (call each frame)
    void updateUniforms(uint32_t frameIndex);

    // Record water rendering commands
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex);

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

private:
    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createWaterMesh();
    bool createUniformBuffers();
    bool loadFoamTexture();

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

    // Pipeline resources
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Water mesh (a subdivided plane for wave animation)
    Mesh waterMesh;
    glm::mat4 waterModelMatrix = glm::mat4(1.0f);

    // Water uniforms
    WaterUniforms waterUniforms{};
    std::vector<VkBuffer> waterUniformBuffers;
    std::vector<VmaAllocation> waterUniformAllocations;
    std::vector<void*> waterUniformMapped;

    // Foam texture (tileable Worley noise)
    Texture foamTexture;

    // Tidal parameters
    float baseWaterLevel = 0.0f;  // Mean sea level
    float tidalRange = 2.0f;      // Max tide height variation in meters
};
