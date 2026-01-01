#pragma once

#include "EnvironmentSettings.h"
#include "GrassConstants.h"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <optional>

#include "BufferUtils.h"
#include "ParticleSystem.h"
#include "UBOs.h"
#include "RAIIAdapter.h"
#include "VulkanRAII.h"
#include "core/FrameBuffered.h"

// Forward declarations
class WindSystem;
class GrassTileManager;
struct InitContext;

// Legacy push constants for non-tiled mode (and shadow pass)
struct GrassPushConstants {
    float time;
    int cascadeIndex;  // For shadow pass: which cascade we're rendering
};

// Extended push constants for tiled grass mode
struct TiledGrassPushConstants {
    float time;
    float tileOriginX;  // World X origin of this tile
    float tileOriginZ;  // World Z origin of this tile
    float padding;
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
        vk::RenderPass shadowRenderPass;
        uint32_t shadowMapSize;
    };

    /**
     * Factory: Create and initialize GrassSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GrassSystem> create(const InitInfo& info);

    /**
     * Bundle of grass-related systems
     */
    struct Bundle {
        std::unique_ptr<WindSystem> wind;
        std::unique_ptr<GrassSystem> grass;
    };

    /**
     * Factory: Create WindSystem and GrassSystem together.
     * Returns nullopt on failure.
     */
    static std::optional<Bundle> createWithDependencies(
        const InitContext& ctx,
        vk::RenderPass hdrRenderPass,
        vk::RenderPass shadowRenderPass,
        uint32_t shadowMapSize
    );

    ~GrassSystem();

    // Non-copyable, non-movable
    GrassSystem(const GrassSystem&) = delete;
    GrassSystem& operator=(const GrassSystem&) = delete;
    GrassSystem(GrassSystem&&) = delete;
    GrassSystem& operator=(GrassSystem&&) = delete;

    // Update extent for viewport (on window resize)
    void setExtent(vk::Extent2D newExtent) { (*particleSystem)->setExtent(VkExtent2D{newExtent.width, newExtent.height}); }
    void setExtent(VkExtent2D newExtent) { (*particleSystem)->setExtent(newExtent); }  // Backward-compatible overload

    void updateDescriptorSets(vk::Device device, const std::vector<vk::Buffer>& uniformBuffers,
                              vk::ImageView shadowMapView, vk::Sampler shadowSampler,
                              const std::vector<vk::Buffer>& windBuffers,
                              const std::vector<vk::Buffer>& lightBuffers,
                              vk::ImageView terrainHeightMapView, vk::Sampler terrainHeightMapSampler,
                              const std::vector<vk::Buffer>& snowBuffers,
                              const std::vector<vk::Buffer>& cloudShadowBuffers,
                              vk::ImageView cloudShadowMapView, vk::Sampler cloudShadowMapSampler,
                              vk::ImageView tileArrayView = {},
                              vk::Sampler tileSampler = {},
                              const std::array<vk::Buffer, 3>& tileInfoBuffers = {},
                              const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO = nullptr);

    // Backward-compatible overload for raw Vulkan types
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
    void recordDisplacementUpdate(vk::CommandBuffer cmd, uint32_t frameIndex);
    void recordResetAndCompute(vk::CommandBuffer cmd, uint32_t frameIndex, float time);
    void recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time);
    void recordShadowDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex);

    // Double-buffer management: call at frame start to swap compute/render buffer sets
    void advanceBufferSet();

    // Displacement texture accessors (for sharing with LeafSystem)
    vk::ImageView getDisplacementImageView() const { return displacementImageView_; }
    vk::Sampler getDisplacementSampler() const { return displacementSampler_.get(); }

    void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

    // Set snow mask texture for snow on grass blades
    void setSnowMask(vk::Device device, vk::ImageView snowMaskView, vk::Sampler snowMaskSampler);

    // Tiled grass mode accessors
    bool isTiledModeEnabled() const { return tiledModeEnabled_; }
    void setTiledModeEnabled(bool enabled) { tiledModeEnabled_ = enabled; }

    // Get the tile manager (may be null if not in tiled mode)
    GrassTileManager* getTileManager() const { return tileManager_.get(); }

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
    vk::Device getDevice() const { return device_; }
    VmaAllocator getAllocator() const { return allocator_; }
    vk::RenderPass getRenderPass() const { return renderPass_; }
    DescriptorManager::Pool* getDescriptorPool() const { return descriptorPool_; }
    vk::Extent2D getExtent() const { return particleSystem ? vk::Extent2D{(*particleSystem)->getExtent().width, (*particleSystem)->getExtent().height} : extent_; }
    const std::string& getShaderPath() const { return shaderPath_; }
    uint32_t getFramesInFlight() const { return framesInFlight_; }

    SystemLifecycleHelper::PipelineHandles& getComputePipelineHandles() { return (*particleSystem)->getComputePipelineHandles(); }
    SystemLifecycleHelper::PipelineHandles& getGraphicsPipelineHandles() { return (*particleSystem)->getGraphicsPipelineHandles(); }

    std::optional<RAIIAdapter<ParticleSystem>> particleSystem;

    // Stored init info (available during initialization before particleSystem is created)
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    vk::RenderPass renderPass_;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    vk::Extent2D extent_;
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;

    vk::RenderPass shadowRenderPass_;
    uint32_t shadowMapSize_ = 0;

    // Shadow pipeline (for casting shadows)
    ManagedDescriptorSetLayout shadowDescriptorSetLayout_;
    ManagedPipelineLayout shadowPipelineLayout_;
    ManagedPipeline shadowPipeline_;

    // Displacement texture resources use unified constants from GrassConstants.h:
    // GrassConstants::DISPLACEMENT_TEXTURE_SIZE (512x512 texels)
    // GrassConstants::DISPLACEMENT_REGION_SIZE (50m x 50m coverage)
    // GrassConstants::MAX_DISPLACEMENT_SOURCES (max sources per frame)

    vk::Image displacementImage_;
    VmaAllocation displacementAllocation_ = VK_NULL_HANDLE;
    vk::ImageView displacementImageView_;
    ManagedSampler displacementSampler_;

    // Displacement update compute pipeline
    ManagedDescriptorSetLayout displacementDescriptorSetLayout_;
    ManagedPipelineLayout displacementPipelineLayout_;
    ManagedPipeline displacementPipeline_;
    // Per-frame descriptor sets for displacement update (double-buffered)
    std::vector<vk::DescriptorSet> displacementDescriptorSets_;

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
    BufferUtils::PerFrameBufferSet uniformBuffers;    // CullingUniforms at binding 2
    BufferUtils::PerFrameBufferSet paramsBuffers;     // GrassParams at binding 7

    // Descriptor sets: one per buffer set (matches frames in flight)
    // Per-frame descriptor sets for uniform buffers that DO need per-frame copies
    std::vector<vk::DescriptorSet> shadowDescriptorSets_;

    // Terrain heightmap for grass placement (stored for compute descriptor updates)
    vk::ImageView terrainHeightMapView_;
    vk::Sampler terrainHeightMapSampler_;

    // Tile cache resources for high-res terrain sampling
    vk::ImageView tileArrayView_;
    vk::Sampler tileSampler_;
    TripleBuffered<vk::Buffer> tileInfoBuffers_;  // Triple-buffered for frames-in-flight sync

    // Renderer uniform buffers for per-frame descriptor updates (stores reference from updateDescriptorSets)
    std::vector<vk::Buffer> rendererUniformBuffers_;

    // Dynamic renderer UBO - used with VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
    // to avoid per-frame descriptor set updates
    const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO_ = nullptr;

    // MAX_INSTANCES uses unified constant from GrassConstants.h:
    // GrassConstants::MAX_INSTANCES (~100k rendered after culling)

    const EnvironmentSettings* environmentSettings = nullptr;

    // Tiled grass system (for world-scale rendering)
    bool tiledModeEnabled_ = true;  // Enable tiled mode by default
    std::unique_ptr<GrassTileManager> tileManager_;

    // Tiled grass compute pipeline (separate from legacy pipeline)
    ManagedPipeline tiledComputePipeline_;
};
