#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "BufferUtils.h"
#include "ParticleSystem.h"

// Forward declaration
class WindSystem;

// Weather particle data (32 bytes, matches GPU struct)
struct WeatherParticle {
    glm::vec3 position;     // World-space position
    float lifetime;         // Remaining lifetime in seconds
    glm::vec3 velocity;     // Current velocity vector
    float size;             // Particle scale factor
    float rotation;         // For rain splash angle
    float hash;             // Per-particle random seed
    uint32_t type;          // 0 = rain, 1 = snow, 2 = splash
    uint32_t flags;         // State flags (active, collided, etc.)
};

// Weather uniforms for compute shader (256 bytes, aligned)
struct WeatherUniforms {
    glm::vec4 cameraPosition;           // xyz = position, w = unused
    glm::vec4 frustumPlanes[6];         // 6 frustum planes for culling
    glm::vec4 windDirectionStrength;    // xy = direction, z = strength, w = turbulence
    glm::vec4 gravity;                  // xyz = gravity vector, w = terminal velocity
    glm::vec4 spawnRegion;              // xyz = center, w = radius
    float spawnHeight;                  // Height above camera to spawn particles
    float groundLevel;                  // Y coordinate of ground plane
    float particleDensity;              // Particles per cubic meter
    float maxDrawDistance;              // Culling distance
    float time;                         // Current simulation time
    float deltaTime;                    // Frame delta time
    uint32_t weatherType;               // 0 = rain, 1 = snow
    float intensity;                    // 0.0-1.0 precipitation strength
    float nearZoneRadius;               // Radius of near zone (8m default)
    float padding[3];                   // Alignment padding
};

// Push constants for weather rendering
struct WeatherPushConstants {
    float time;
    float deltaTime;
    int cascadeIndex;
    int padding;
};

class WeatherSystem {
public:
    using InitInfo = ParticleSystem::InitInfo;

    WeatherSystem() = default;
    ~WeatherSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Update descriptor sets with external resources (UBO, wind buffer)
    void updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& uniformBuffers,
                              const std::vector<VkBuffer>& windBuffers,
                              VkImageView depthImageView, VkSampler depthSampler);

    // Set froxel volume for fog lighting on particles (Phase 4.3.9)
    void setFroxelVolume(VkImageView volumeView, VkSampler volumeSampler,
                         float farPlane, float depthDist);

    // Update weather uniforms each frame
    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                        const glm::mat4& viewProj, float deltaTime, float totalTime,
                        const WindSystem& windSystem);

    // Record compute dispatch for particle simulation
    void recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time, float deltaTime);

    // Record draw commands for weather particles (after opaque geometry)
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time);

    // Double-buffer management
    void advanceBufferSet();

    // Weather control
    void setIntensity(float intensity) { weatherIntensity = intensity; }
    float getIntensity() const { return weatherIntensity; }
    void setWeatherType(uint32_t type) { weatherType = type; }
    uint32_t getWeatherType() const { return weatherType; }
    void setGroundLevel(float level) { groundLevel = level; }

private:
    bool createBuffers();
    bool createComputeDescriptorSetLayout();
    bool createComputePipeline();
    bool createGraphicsDescriptorSetLayout();
    bool createGraphicsPipeline();
    bool createDescriptorSets();
    void destroyBuffers(VmaAllocator allocator);

    VkDevice getDevice() const { return particleSystem.getDevice(); }
    VmaAllocator getAllocator() const { return particleSystem.getAllocator(); }
    VkRenderPass getRenderPass() const { return particleSystem.getRenderPass(); }
    VkDescriptorPool getDescriptorPool() const { return particleSystem.getDescriptorPool(); }
    const VkExtent2D& getExtent() const { return particleSystem.getExtent(); }
    const std::string& getShaderPath() const { return particleSystem.getShaderPath(); }
    uint32_t getFramesInFlight() const { return particleSystem.getFramesInFlight(); }

    SystemLifecycleHelper::PipelineHandles& getComputePipelineHandles() { return particleSystem.getComputePipelineHandles(); }
    SystemLifecycleHelper::PipelineHandles& getGraphicsPipelineHandles() { return particleSystem.getGraphicsPipelineHandles(); }

    ParticleSystem particleSystem;

    // Double-buffered storage buffers
    static constexpr uint32_t BUFFER_SET_COUNT = 2;
    BufferUtils::DoubleBufferedBufferSet particleBuffers;
    BufferUtils::DoubleBufferedBufferSet indirectBuffers;

    // Uniform buffers (per frame)
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // Descriptor sets
    // Descriptor sets managed through ParticleSystem helper

    // Weather parameters
    float weatherIntensity = 0.0f;      // 0.0-1.0 intensity
    uint32_t weatherType = 0;           // 0 = rain, 1 = snow
    float groundLevel = 0.0f;           // Ground plane Y coordinate

    // External buffer references for per-frame descriptor updates
    std::vector<VkBuffer> externalWindBuffers;
    std::vector<VkBuffer> externalRendererUniformBuffers;

    // Froxel volume for fog particle lighting (Phase 4.3.9)
    VkImageView froxelVolumeView = VK_NULL_HANDLE;
    VkSampler froxelVolumeSampler = VK_NULL_HANDLE;
    float froxelFarPlane = 200.0f;
    float froxelDepthDist = 1.2f;

    // Particle counts based on intensity
    static constexpr uint32_t MAX_PARTICLES = 150000;
    static constexpr uint32_t WORKGROUP_SIZE = 256;
};
