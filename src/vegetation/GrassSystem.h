#pragma once

#include "EnvironmentSettings.h"
#include <vulkan/vulkan.hpp>  // Vulkan-Hpp for type-safe enums and structs
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>
#include <memory>

#include "BufferUtils.h"
#include "ParticleSystem.h"
#include "UBOs.h"
#include "RAIIAdapter.h"
#include "VulkanRAII.h"
#include <optional>

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

    /**
     * Factory: Create and initialize GrassSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GrassSystem> create(const InitInfo& info);

    ~GrassSystem();

    // Non-copyable, non-movable
    GrassSystem(const GrassSystem&) = delete;
    GrassSystem& operator=(const GrassSystem&) = delete;
    GrassSystem(GrassSystem&&) = delete;
    GrassSystem& operator=(GrassSystem&&) = delete;

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { (*particleSystem)->setExtent(newExtent); }

    void updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& uniformBuffers,
                              VkImageView shadowMapView, VkSampler shadowSampler,
                              const std::vector<VkBuffer>& windBuffers,
                              const std::vector<VkBuffer>& lightBuffers,
                              VkImageView terrainHeightMapView, VkSampler terrainHeightMapSampler,
                              const std::vector<VkBuffer>& snowBuffers,
                              const std::vector<VkBuffer>& cloudShadowBuffers,
                              VkImageView cloudShadowMapView, VkSampler cloudShadowMapSampler,
                              VkImageView tileArrayView = VK_NULL_HANDLE,
                              VkSampler tileSampler = VK_NULL_HANDLE,
                              const std::array<VkBuffer, 3>& tileInfoBuffers = {},
                              const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO = nullptr);

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
    VkSampler getDisplacementSampler() const { return displacementSampler_.get(); }

    void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

    // Set snow mask texture for snow on grass blades
    void setSnowMask(VkDevice device, VkImageView snowMaskView, VkSampler snowMaskSampler);

private:
    GrassSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createShadowPipeline();
    bool createBuffers();
    bool createDisplacementResources();
    bool createDisplacementPipeline();
    bool createComputeDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles);
    bool createComputePipeline(SystemLifecycleHelper::PipelineHandles& handles);
    bool createGraphicsDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles);
    bool createGraphicsPipeline(SystemLifecycleHelper::PipelineHandles& handles);
    bool createDescriptorSets();
    bool createExtraPipelines();
    void writeComputeDescriptorSets();  // Called after init to write compute descriptor sets
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

    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    uint32_t shadowMapSize = 0;

    // Shadow pipeline (for casting shadows)
    ManagedDescriptorSetLayout shadowDescriptorSetLayout_;
    ManagedPipelineLayout shadowPipelineLayout_;
    ManagedPipeline shadowPipeline_;

    // Displacement texture resources (for player/NPC grass interaction)
    static constexpr uint32_t DISPLACEMENT_TEXTURE_SIZE = 512;  // 512x512 texels
    static constexpr float DISPLACEMENT_REGION_SIZE = 50.0f;    // 50m x 50m coverage
    static constexpr uint32_t MAX_DISPLACEMENT_SOURCES = 16;    // Max sources per frame

    VkImage displacementImage = VK_NULL_HANDLE;
    VmaAllocation displacementAllocation = VK_NULL_HANDLE;
    VkImageView displacementImageView = VK_NULL_HANDLE;
    ManagedSampler displacementSampler_;

    // Displacement update compute pipeline
    ManagedDescriptorSetLayout displacementDescriptorSetLayout_;
    ManagedPipelineLayout displacementPipelineLayout_;
    ManagedPipeline displacementPipeline_;
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

    // Triple-buffered storage buffers: one per frame in flight
    // Each frame gets its own buffer set to avoid GPU read/CPU write conflicts.
    // Buffer set count MUST match frames in flight (3) to prevent race conditions.
    BufferUtils::DoubleBufferedBufferSet instanceBuffers;      // [setIndex]
    BufferUtils::DoubleBufferedBufferSet indirectBuffers;

    // Uniform buffers for culling (per frame, not double-buffered)
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // Descriptor sets: one per buffer set (matches frames in flight)
    // Per-frame descriptor sets for uniform buffers that DO need per-frame copies
    std::vector<VkDescriptorSet> shadowDescriptorSetsDB;

    // Terrain heightmap for grass placement (stored for compute descriptor updates)
    VkImageView terrainHeightMapView = VK_NULL_HANDLE;
    VkSampler terrainHeightMapSampler = VK_NULL_HANDLE;

    // Tile cache resources for high-res terrain sampling
    VkImageView tileArrayView = VK_NULL_HANDLE;
    VkSampler tileSampler = VK_NULL_HANDLE;
    std::array<VkBuffer, 3> tileInfoBuffers = {};  // Triple-buffered for frames-in-flight sync

    // Renderer uniform buffers for per-frame descriptor updates (stores reference from updateDescriptorSets)
    std::vector<VkBuffer> rendererUniformBuffers_;

    // Dynamic renderer UBO - used with VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
    // to avoid per-frame descriptor set updates
    const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO_ = nullptr;

    static constexpr uint32_t MAX_INSTANCES = 100000;  // ~100k rendered after culling

    const EnvironmentSettings* environmentSettings = nullptr;
};
