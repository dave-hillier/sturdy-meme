#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>

#include "BufferUtils.h"
#include "SystemLifecycleHelper.h"
#include "EnvironmentSettings.h"

/**
 * VolumetricSnowSystem - Cascaded heightfield snow accumulation
 *
 * Implements volumetric snow using a multi-resolution cascade approach:
 * - Near cascade:  256x256 @ 1m/texel  (256m coverage)
 * - Mid cascade:   256x256 @ 4m/texel  (1024m coverage)
 * - Far cascade:   256x256 @ 16m/texel (4096m coverage)
 *
 * Key features:
 * - Height accumulation (R16F stores height in meters, 0-10m range)
 * - Wind-driven drift accumulation
 * - Cascade blending for smooth LOD transitions
 * - Supports vertex displacement and parallax occlusion mapping
 */

// Cascade configuration
static constexpr uint32_t NUM_SNOW_CASCADES = 3;
static constexpr uint32_t SNOW_CASCADE_SIZE = 256;  // Texture resolution per cascade

// Cascade world coverage (meters per cascade)
static constexpr float SNOW_CASCADE_COVERAGE[NUM_SNOW_CASCADES] = {
    256.0f,   // Near: 256m  (1m/texel)
    1024.0f,  // Mid:  1024m (4m/texel)
    4096.0f   // Far:  4096m (16m/texel)
};

// Maximum snow height in meters
static constexpr float MAX_SNOW_HEIGHT = 2.0f;

// Uniforms for volumetric snow compute shader
struct VolumetricSnowUniforms {
    // Cascade 0 (near)
    glm::vec4 cascade0Region;      // xy = world origin, z = size, w = texel size
    // Cascade 1 (mid)
    glm::vec4 cascade1Region;      // xy = world origin, z = size, w = texel size
    // Cascade 2 (far)
    glm::vec4 cascade2Region;      // xy = world origin, z = size, w = texel size

    // Accumulation parameters
    glm::vec4 accumulationParams;  // x = rate, y = melt rate, z = delta time, w = is snowing (0/1)
    glm::vec4 snowParams;          // x = target height, y = weather intensity, z = num interactions, w = max height

    // Wind parameters for drift
    glm::vec4 windParams;          // xy = wind direction (normalized), z = wind strength, w = drift rate

    // Camera position for cascade centering
    glm::vec4 cameraPosition;      // xyz = position, w = unused

    float padding[4];              // Align to 128 bytes
};

// Interaction source for snow clearing (footprints, vehicles, etc.)
struct VolumetricSnowInteraction {
    glm::vec4 positionAndRadius;   // xyz = world position, w = radius
    glm::vec4 strengthAndDepth;    // x = clearing strength, y = depth factor, z = shape, w = unused
};

class VolumetricSnowSystem {
public:
    using InitInfo = SystemLifecycleHelper::InitInfo;

    VolumetricSnowSystem() = default;
    ~VolumetricSnowSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Update uniforms for compute shader
    void updateUniforms(uint32_t frameIndex, float deltaTime, bool isSnowing, float weatherIntensity,
                        const EnvironmentSettings& settings);

    // Add interaction source (footprint, vehicle track, etc.)
    void addInteraction(const glm::vec3& position, float radius, float strength, float depthFactor = 1.0f);
    void clearInteractions();

    // Record compute dispatch for snow accumulation update
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex);

    // Set camera position (cascades center around this)
    void setCameraPosition(const glm::vec3& worldPos);

    // Accessors for cascade textures (for terrain/object shaders to bind)
    VkImageView getCascadeView(uint32_t cascade) const {
        return cascade < NUM_SNOW_CASCADES ? cascadeViews[cascade] : VK_NULL_HANDLE;
    }
    VkSampler getCascadeSampler() const { return cascadeSampler; }

    // Get cascade parameters for shader uniforms (origin, size)
    glm::vec2 getCascadeOrigin(uint32_t cascade) const {
        return cascade < NUM_SNOW_CASCADES ? cascadeOrigins[cascade] : glm::vec2(0.0f);
    }
    float getCascadeSize(uint32_t cascade) const {
        return cascade < NUM_SNOW_CASCADES ? SNOW_CASCADE_COVERAGE[cascade] : 0.0f;
    }

    // Get all cascade data packed for shader uniform
    // Returns vec4 per cascade: xy = origin, z = size, w = texel size
    std::array<glm::vec4, NUM_SNOW_CASCADES> getCascadeParams() const;

    // Environment settings
    void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

    // Wind direction for drift (normalized XZ direction)
    void setWindDirection(const glm::vec2& dir) { windDirection = glm::normalize(dir); }
    void setWindStrength(float strength) { windStrength = strength; }

private:
    bool createBuffers();
    bool createCascadeTextures();
    bool createComputeDescriptorSetLayout();
    bool createComputePipeline();
    bool createDescriptorSets();
    void destroyBuffers(VmaAllocator allocator);

    void updateCascadeOrigins(const glm::vec3& cameraPos);

    VkDevice getDevice() const { return lifecycle.getDevice(); }
    VmaAllocator getAllocator() const { return lifecycle.getAllocator(); }
    VkDescriptorPool getDescriptorPool() const { return lifecycle.getDescriptorPool(); }
    const std::string& getShaderPath() const { return lifecycle.getShaderPath(); }
    uint32_t getFramesInFlight() const { return lifecycle.getFramesInFlight(); }

    SystemLifecycleHelper::PipelineHandles& getComputePipelineHandles() { return lifecycle.getComputePipeline(); }

    SystemLifecycleHelper lifecycle;

    // Cascade textures (R16F height in meters)
    std::array<VkImage, NUM_SNOW_CASCADES> cascadeImages{};
    std::array<VmaAllocation, NUM_SNOW_CASCADES> cascadeAllocations{};
    std::array<VkImageView, NUM_SNOW_CASCADES> cascadeViews{};
    VkSampler cascadeSampler = VK_NULL_HANDLE;

    // Cascade world-space parameters (updated based on camera position)
    std::array<glm::vec2, NUM_SNOW_CASCADES> cascadeOrigins{};
    glm::vec3 lastCameraPosition = glm::vec3(0.0f);

    // Uniform buffers (per frame)
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // Interaction sources buffer (per frame)
    static constexpr uint32_t MAX_INTERACTIONS = 32;
    BufferUtils::PerFrameBufferSet interactionBuffers;

    // Descriptor sets (per frame)
    std::vector<VkDescriptorSet> computeDescriptorSets;

    // Current frame interaction sources
    std::vector<VolumetricSnowInteraction> currentInteractions;

    // Environment settings reference
    const EnvironmentSettings* environmentSettings = nullptr;

    // Wind parameters for drift
    glm::vec2 windDirection = glm::vec2(1.0f, 0.0f);
    float windStrength = 0.0f;
    float driftRate = 0.02f;  // Base drift rate per second

    static constexpr uint32_t WORKGROUP_SIZE = 16;  // 16x16 workgroups

    std::array<bool, NUM_SNOW_CASCADES> isFirstFrame{true, true, true};
};
