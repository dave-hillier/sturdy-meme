#pragma once

#include "EnvironmentSettings.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "ParticleSystem.h"
#include "BufferUtils.h"
#include "UBOs.h"
#include "core/RAIIAdapter.h"
#include <optional>

// Leaf particle states
enum class LeafState : uint32_t {
    INACTIVE = 0,
    FALLING = 1,
    GROUNDED = 2,
    DISTURBED = 3
};

// Leaf particle data (80 bytes, aligned for GPU)
struct LeafParticle {
    glm::vec3 position;         // 12 bytes - World position
    uint32_t state;             // 4 bytes  - LeafState enum
    glm::vec3 velocity;         // 12 bytes - Linear velocity
    float groundTime;           // 4 bytes  - Time spent grounded
    glm::vec4 orientation;      // 16 bytes - Quaternion rotation
    glm::vec3 angularVelocity;  // 12 bytes - Tumbling rate (radians/sec)
    float size;                 // 4 bytes  - Leaf scale (0.02-0.08m)
    float hash;                 // 4 bytes  - Per-particle random seed
    uint32_t leafType;          // 4 bytes  - Leaf variety index (0-3)
    uint32_t flags;             // 4 bytes  - Bit flags (active, visible)
    float padding;              // 4 bytes  - Padding to 80 bytes
};

// Push constants for leaf rendering
struct LeafPushConstants {
    float time;
    float deltaTime;
    int padding[2];
};

class LeafSystem {
public:
    using InitInfo = ParticleSystem::InitInfo;

    LeafSystem() = default;
    ~LeafSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { (*particleSystem)->setExtent(newExtent); }

    // Update descriptor sets with external resources (UBO, wind buffer, heightmap, displacement, tile cache)
    void updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& uniformBuffers,
                              const std::vector<VkBuffer>& windBuffers,
                              VkImageView terrainHeightMapView,
                              VkSampler terrainHeightMapSampler,
                              VkImageView displacementMapView,
                              VkSampler displacementMapSampler,
                              VkImageView tileArrayView = VK_NULL_HANDLE,
                              VkSampler tileSampler = VK_NULL_HANDLE,
                              VkBuffer tileInfoBuffer = VK_NULL_HANDLE);

    // Update leaf uniforms each frame
    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                        const glm::mat4& viewProj, const glm::vec3& playerPos,
                        const glm::vec3& playerVel, float deltaTime, float totalTime,
                        float terrainSize, float terrainHeightScale);

    // Record compute dispatch for particle simulation
    void recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time, float deltaTime);

    // Record draw commands for leaves (after opaque geometry, before weather)
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time);

    // Double-buffer management
    void advanceBufferSet();

    // Leaf control
    void setIntensity(float intensity) { leafIntensity = intensity; }
    float getIntensity() const { return leafIntensity; }
    void setGroundLevel(float level) { groundLevel = level; }
    void setSpawnRegion(const glm::vec3& minBounds, const glm::vec3& maxBounds) {
        spawnRegionMin = minBounds;
        spawnRegionMax = maxBounds;
    }

    // Confetti control
    void spawnConfetti(const glm::vec3& position, float velocity = 8.0f, float count = 100.0f, float coneAngle = 0.5f) {
        confettiSpawnPosition = position;
        confettiSpawnVelocity = velocity;
        confettiToSpawn = count;
        confettiConeAngle = coneAngle;
    }

    void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

private:
    bool createBuffers();
    bool createComputeDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles);
    bool createComputePipeline(SystemLifecycleHelper::PipelineHandles& handles);
    bool createGraphicsDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles);
    bool createGraphicsPipeline(SystemLifecycleHelper::PipelineHandles& handles);
    bool createDescriptorSets();
    void destroyBuffers(VmaAllocator allocator);

    // Accessors - use stored initInfo during init, particleSystem after init completes
    VkDevice getDevice() const { return storedDevice; }
    VmaAllocator getAllocator() const { return storedAllocator; }
    VkRenderPass getRenderPass() const { return storedRenderPass; }
    DescriptorManager::Pool* getDescriptorPool() const { return storedDescriptorPool; }
    const VkExtent2D& getExtent() const { return particleSystem ? (*particleSystem)->getExtent() : storedExtent; }
    const std::string& getShaderPath() const { return storedShaderPath; }
    uint32_t getFramesInFlight() const { return storedFramesInFlight; }

    SystemLifecycleHelper::PipelineHandles& getComputePipelineHandles() { return (*particleSystem)->getComputePipelineHandles(); }
    SystemLifecycleHelper::PipelineHandles& getGraphicsPipelineHandles() { return (*particleSystem)->getGraphicsPipelineHandles(); }

    std::optional<RAIIAdapter<ParticleSystem>> particleSystem;

    // Stored init info (available during initialization before particleSystem is created)
    VkDevice storedDevice = VK_NULL_HANDLE;
    VmaAllocator storedAllocator = VK_NULL_HANDLE;
    VkRenderPass storedRenderPass = VK_NULL_HANDLE;
    DescriptorManager::Pool* storedDescriptorPool = nullptr;
    VkExtent2D storedExtent = {0, 0};
    std::string storedShaderPath;
    uint32_t storedFramesInFlight = 0;

    // Double-buffered storage buffers
    static constexpr uint32_t BUFFER_SET_COUNT = 2;
    BufferUtils::DoubleBufferedBufferSet particleBuffers;
    BufferUtils::DoubleBufferedBufferSet indirectBuffers;

    // Uniform buffers (per frame)
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // Descriptor sets
    // Descriptor sets managed through ParticleSystem helper

    // Leaf parameters
    float leafIntensity = 0.5f;         // 0.0-1.0 intensity
    float groundLevel = 0.0f;           // Ground plane Y coordinate
    glm::vec3 spawnRegionMin = glm::vec3(-50.0f, 10.0f, -50.0f);
    glm::vec3 spawnRegionMax = glm::vec3(50.0f, 20.0f, 50.0f);

    // Confetti parameters
    glm::vec3 confettiSpawnPosition = glm::vec3(0.0f);
    float confettiSpawnVelocity = 0.0f;
    float confettiToSpawn = 0.0f;
    float confettiConeAngle = 0.5f;

    // Displacement texture (shared from GrassSystem)
    VkImageView displacementMapView = VK_NULL_HANDLE;
    VkSampler displacementMapSampler = VK_NULL_HANDLE;

    // Displacement region uniform buffer (per-frame)
    BufferUtils::PerFrameBufferSet displacementRegionBuffers;

    // Displacement region center (updated from camera position)
    glm::vec2 displacementRegionCenter = glm::vec2(0.0f);
    static constexpr float DISPLACEMENT_REGION_SIZE = 50.0f;

    const EnvironmentSettings* environmentSettings = nullptr;

    // Particle counts
    static constexpr uint32_t MAX_PARTICLES = 100000;
    static constexpr uint32_t WORKGROUP_SIZE = 256;
};
