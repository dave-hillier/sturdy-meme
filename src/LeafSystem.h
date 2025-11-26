#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

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

// Leaf uniforms for compute shader (256 bytes, aligned)
struct LeafUniforms {
    glm::vec4 cameraPosition;           // xyz = position, w = unused
    glm::vec4 frustumPlanes[6];         // 6 frustum planes for culling
    glm::vec4 playerPosition;           // xyz = player position, w = player radius
    glm::vec4 playerVelocity;           // xyz = player velocity, w = speed magnitude
    glm::vec4 spawnRegionMin;           // xyz = AABB minimum
    glm::vec4 spawnRegionMax;           // xyz = AABB maximum
    glm::vec4 confettiSpawnPos;         // xyz = confetti spawn position, w = cone angle
    float groundLevel;                  // Y coordinate of ground plane
    float deltaTime;                    // Frame delta time
    float time;                         // Accumulated time
    float maxDrawDistance;              // Culling distance
    float disruptionRadius;             // Player interaction range
    float disruptionStrength;           // Force applied on disruption
    float gustThreshold;                // Wind strength to lift grounded leaves
    float targetFallingCount;           // Target number of falling leaves
    float targetGroundedCount;          // Target number of grounded leaves
    float confettiSpawnCount;           // Number of confetti to spawn this frame
    float confettiVelocity;             // Initial upward velocity for confetti
    float padding[1];                   // Alignment padding
};

// Push constants for leaf rendering
struct LeafPushConstants {
    float time;
    float deltaTime;
    int padding[2];
};

class LeafSystem {
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

    LeafSystem() = default;
    ~LeafSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Update descriptor sets with external resources (UBO, wind buffer)
    void updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& uniformBuffers,
                              const std::vector<VkBuffer>& windBuffers);

    // Update leaf uniforms each frame
    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                        const glm::mat4& viewProj, const glm::vec3& playerPos,
                        const glm::vec3& playerVel, float deltaTime, float totalTime);

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

    // Particle counts
    static constexpr uint32_t MAX_PARTICLES = 100000;
    static constexpr uint32_t WORKGROUP_SIZE = 256;
};
