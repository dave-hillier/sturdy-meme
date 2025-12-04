#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "Mesh.h"
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
    };

    // Water uniforms - must match shader layout
    struct WaterUniforms {
        glm::vec4 waterColor;      // rgb = base water color, a = transparency
        glm::vec4 waveParams;      // x = amplitude, y = wavelength, z = steepness, w = speed
        glm::vec4 waveParams2;     // Second wave layer parameters
        glm::vec4 waterExtent;     // xy = position offset, zw = size
        float waterLevel;          // Y height of water plane
        float foamThreshold;       // Wave height threshold for foam
        float fresnelPower;        // Fresnel reflection power
        float padding;
    };

    WaterSystem() = default;
    ~WaterSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Create descriptor sets after main UBO is ready
    bool createDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                              VkDeviceSize uniformBufferSize,
                              ShadowSystem& shadowSystem);

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

private:
    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createWaterMesh();
    bool createUniformBuffers();

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

    // Tidal parameters
    float baseWaterLevel = 0.0f;  // Mean sea level
    float tidalRange = 2.0f;      // Max tide height variation in meters
};
