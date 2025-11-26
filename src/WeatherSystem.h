#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

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
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkRenderPass renderPass;
        VkDescriptorPool descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
    };

    WeatherSystem() = default;
    ~WeatherSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Update descriptor sets with external resources (UBO, wind buffer)
    void updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& uniformBuffers,
                              const std::vector<VkBuffer>& windBuffers,
                              VkImageView depthImageView, VkSampler depthSampler);

    // Update weather uniforms each frame
    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                        const glm::mat4& viewProj, float deltaTime, float totalTime);

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

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    // Compute pipeline
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;

    // Graphics pipeline
    VkDescriptorSetLayout graphicsDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout graphicsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    // Double-buffered storage buffers
    static constexpr uint32_t BUFFER_SET_COUNT = 2;
    VkBuffer particleBuffers[BUFFER_SET_COUNT];
    VmaAllocation particleAllocations[BUFFER_SET_COUNT];
    VkBuffer indirectBuffers[BUFFER_SET_COUNT];
    VmaAllocation indirectAllocations[BUFFER_SET_COUNT];

    // Uniform buffers (per frame)
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformAllocations;
    std::vector<void*> uniformMappedPtrs;

    // Descriptor sets
    VkDescriptorSet computeDescriptorSets[BUFFER_SET_COUNT];
    VkDescriptorSet graphicsDescriptorSets[BUFFER_SET_COUNT];

    // Double-buffer state
    uint32_t computeBufferSet = 0;
    uint32_t renderBufferSet = 0;

    // Weather parameters
    float weatherIntensity = 0.0f;      // 0.0-1.0 intensity
    uint32_t weatherType = 0;           // 0 = rain, 1 = snow
    float groundLevel = 0.0f;           // Ground plane Y coordinate

    // External buffer references for per-frame descriptor updates
    std::vector<VkBuffer> externalWindBuffers;
    std::vector<VkBuffer> externalRendererUniformBuffers;

    // Particle counts based on intensity
    static constexpr uint32_t MAX_PARTICLES = 150000;
    static constexpr uint32_t WORKGROUP_SIZE = 256;
};
