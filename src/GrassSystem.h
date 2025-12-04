#pragma once

#include "EnvironmentSettings.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "BufferUtils.h"
#include "ParticleSystem.h"
#include "UBOs.h"

struct GrassPushConstants {
    float time;
    int cascadeIndex;  // For shadow pass: which cascade we're rendering
};

// Displacement source for grass interaction (player, NPCs, etc.)
struct DisplacementSource {
    glm::vec4 positionAndRadius;   // xyz = world position, w = radius
    glm::vec4 strengthAndVelocity; // x = strength, yzw = velocity (for directional push)
};

// Uniforms for displacement update compute shader
struct DisplacementUniforms {
    glm::vec4 regionCenter;        // xy = world center, z = region size, w = texel size
    glm::vec4 params;              // x = decay rate, y = max displacement, z = delta time, w = num sources
};

struct GrassInstance {
    glm::vec4 positionAndFacing;  // xyz = position, w = facing angle
    glm::vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = clumpId
    glm::vec4 terrainNormal;      // xyz = terrain normal (for tangent alignment), w = unused
};

class GrassSystem {
public:
    struct InitInfo : public ParticleSystem::InitInfo {
        VkRenderPass shadowRenderPass;
        uint32_t shadowMapSize;
    };

    GrassSystem() = default;
    ~GrassSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    void updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& uniformBuffers,
                              VkImageView shadowMapView, VkSampler shadowSampler,
                              const std::vector<VkBuffer>& windBuffers,
                              const std::vector<VkBuffer>& lightBuffers,
                              VkImageView terrainHeightMapView, VkSampler terrainHeightMapSampler,
                              const std::vector<VkBuffer>& snowBuffers,
                              const std::vector<VkBuffer>& cloudShadowBuffers,
                              VkImageView cloudShadowMapView, VkSampler cloudShadowMapSampler);

    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                        float terrainSize, float terrainHeightScale);
    void updateDisplacementSources(const glm::vec3& playerPos, float playerRadius, float deltaTime);
    void recordDisplacementUpdate(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time);
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time);
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex);

    // Double-buffer management: call at frame start to swap compute/render buffer sets
    void advanceBufferSet();

    // Displacement texture accessors (for sharing with LeafSystem)
    VkImageView getDisplacementImageView() const { return displacementImageView; }
    VkSampler getDisplacementSampler() const { return displacementSampler; }

    void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

    // Set snow mask texture for snow on grass blades
    void setSnowMask(VkDevice device, VkImageView snowMaskView, VkSampler snowMaskSampler);

private:
    bool createShadowPipeline();
    bool createBuffers();
    bool createDisplacementResources();
    bool createDisplacementPipeline();
    bool createComputeDescriptorSetLayout();
    bool createComputePipeline();
    bool createGraphicsDescriptorSetLayout();
    bool createGraphicsPipeline();
    bool createDescriptorSets();
    bool createExtraPipelines();
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

    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    uint32_t shadowMapSize = 0;

    // Shadow pipeline (for casting shadows)
    VkDescriptorSetLayout shadowDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;

    // Displacement texture resources (for player/NPC grass interaction)
    static constexpr uint32_t DISPLACEMENT_TEXTURE_SIZE = 512;  // 512x512 texels
    static constexpr float DISPLACEMENT_REGION_SIZE = 50.0f;    // 50m x 50m coverage
    static constexpr uint32_t MAX_DISPLACEMENT_SOURCES = 16;    // Max sources per frame

    VkImage displacementImage = VK_NULL_HANDLE;
    VmaAllocation displacementAllocation = VK_NULL_HANDLE;
    VkImageView displacementImageView = VK_NULL_HANDLE;
    VkSampler displacementSampler = VK_NULL_HANDLE;

    // Displacement update compute pipeline
    VkDescriptorSetLayout displacementDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout displacementPipelineLayout = VK_NULL_HANDLE;
    VkPipeline displacementPipeline = VK_NULL_HANDLE;
    // Per-frame descriptor sets for displacement update (double-buffered)
    std::vector<VkDescriptorSet> displacementDescriptorSets;

    // Displacement sources buffer (per-frame)
    BufferUtils::PerFrameBufferSet displacementSourceBuffers;

    // Displacement uniforms buffer (per-frame)
    BufferUtils::PerFrameBufferSet displacementUniformBuffers;

    // Current displacement region center (follows camera)
    glm::vec2 displacementRegionCenter = glm::vec2(0.0f);

    // Displacement source data for current frame
    std::vector<DisplacementSource> currentDisplacementSources;

    // Double-buffered storage buffers: A/B sets that alternate each frame
    // Set A and Set B alternate: compute writes to one while graphics reads from other
    // Note: We don't need per-frame copies since the set alternation provides isolation
    static constexpr uint32_t BUFFER_SET_COUNT = 2;
    BufferUtils::DoubleBufferedBufferSet instanceBuffers;      // [setIndex]
    BufferUtils::DoubleBufferedBufferSet indirectBuffers;

    // Uniform buffers for culling (per frame, not double-buffered)
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // Descriptor sets: [2] for A/B buffer sets, one per set
    // Per-frame descriptor sets for uniform buffers that DO need per-frame copies
    VkDescriptorSet shadowDescriptorSetsDB[BUFFER_SET_COUNT];

    // Terrain heightmap for grass placement (stored for compute descriptor updates)
    VkImageView terrainHeightMapView = VK_NULL_HANDLE;
    VkSampler terrainHeightMapSampler = VK_NULL_HANDLE;

    static constexpr uint32_t MAX_INSTANCES = 100000;  // ~100k rendered after culling

    const EnvironmentSettings* environmentSettings = nullptr;
};
