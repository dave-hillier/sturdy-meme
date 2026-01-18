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
#include "SystemLifecycleHelper.h"
#include "BufferSetManager.h"
#include "UBOs.h"
#include "VmaResources.h"
#include "core/FrameBuffered.h"
#include "interfaces/IRecordable.h"
#include "interfaces/IShadowCaster.h"

// Forward declarations
class WindSystem;
class GrassTileManager;
class DisplacementSystem;
struct InitContext;

// Legacy push constants for non-tiled mode (and shadow pass)
struct GrassPushConstants {
    float time;
    int cascadeIndex;  // For shadow pass: which cascade we're rendering
};

// Push constants for tiled grass with continuous stochastic culling
// Tiles provide coarse culling, continuous distance-based culling handles density
struct TiledGrassPushConstants {
    float time;
    float tileOriginX;   // World X origin of this tile
    float tileOriginZ;   // World Z origin of this tile
    float tileSize;      // Tile size in world units
    float spacing;       // Blade spacing (always base spacing, no LOD multiplier)
    uint32_t tileIndex;  // Tile index for debugging
    float unused1;       // Padding
    float unused2;       // Padding
};

struct GrassInstance {
    glm::vec4 positionAndFacing;  // xyz = position, w = facing angle
    glm::vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = clumpId
    glm::vec4 terrainNormal;      // xyz = terrain normal (for tangent alignment), w = unused
};

class GrassSystem : public IRecordableAnimated, public IShadowCasterAnimated {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };

    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator;
        vk::RenderPass renderPass;
        DescriptorManager::Pool* descriptorPool;
        vk::Extent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        const vk::raii::Device* raiiDevice = nullptr;
        vk::RenderPass shadowRenderPass;
        uint32_t shadowMapSize;
    };

    /**
     * Factory: Create and initialize GrassSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GrassSystem> create(const InitInfo& info);

    explicit GrassSystem(ConstructToken);

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
    void setExtent(vk::Extent2D newExtent) { extent_ = newExtent; }
    void setExtent(VkExtent2D newExtent) { extent_ = vk::Extent2D{newExtent.width, newExtent.height}; }  // Backward-compatible overload

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

    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                        float terrainSize, float terrainHeightScale, float time);
    void recordResetAndCompute(vk::CommandBuffer cmd, uint32_t frameIndex, float time);
    void recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time);
    void recordShadowDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex);

    // IRecordableAnimated interface implementation
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) override {
        recordDraw(vk::CommandBuffer(cmd), frameIndex, time);
    }

    // IShadowCasterAnimated interface implementation
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time, int cascade) override {
        recordShadowDraw(vk::CommandBuffer(cmd), frameIndex, time, static_cast<uint32_t>(cascade));
    }

    // Double-buffer management: call at frame start to swap compute/render buffer sets
    void advanceBufferSet();

    // Displacement system setter (required before recordResetAndCompute)
    void setDisplacementSystem(DisplacementSystem* displacement) { displacementSystem_ = displacement; }

    // Displacement texture accessors (delegate to DisplacementSystem)
    vk::ImageView getDisplacementImageView() const;
    vk::Sampler getDisplacementSampler() const;

    void setEnvironmentSettings(const EnvironmentSettings* settings) { environmentSettings = settings; }

    // Set snow mask texture for snow on grass blades
    void setSnowMask(vk::Device device, vk::ImageView snowMaskView, vk::Sampler snowMaskSampler);

    // Tiled grass mode accessors
    bool isTiledModeEnabled() const { return tiledModeEnabled_; }
    void setTiledModeEnabled(bool enabled) { tiledModeEnabled_ = enabled; }

    // Get the tile manager (may be null if not in tiled mode)
    GrassTileManager* getTileManager() const { return tileManager_.get(); }

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createShadowPipeline();
    bool createBuffers();
    bool createComputeDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles);
    bool createComputePipeline(SystemLifecycleHelper::PipelineHandles& handles);
    bool createGraphicsDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles);
    bool createGraphicsPipeline(SystemLifecycleHelper::PipelineHandles& handles);
    bool createDescriptorSets();
    bool createExtraPipelines(SystemLifecycleHelper::PipelineHandles& computeHandles,
                               SystemLifecycleHelper::PipelineHandles& graphicsHandles);
    void writeComputeDescriptorSets();  // Called after init to write compute descriptor sets
    void destroyBuffers(VmaAllocator allocator);

    // Accessors
    vk::Device getDevice() const { return device_; }
    VmaAllocator getAllocator() const { return allocator_; }
    vk::RenderPass getRenderPass() const { return renderPass_; }
    DescriptorManager::Pool* getDescriptorPool() const { return descriptorPool_; }
    vk::Extent2D getExtent() const { return extent_; }
    const std::string& getShaderPath() const { return shaderPath_; }
    uint32_t getFramesInFlight() const { return framesInFlight_; }

    SystemLifecycleHelper::PipelineHandles& getComputePipelineHandles() { return lifecycle_.getComputePipeline(); }
    SystemLifecycleHelper::PipelineHandles& getGraphicsPipelineHandles() { return lifecycle_.getGraphicsPipeline(); }

    // Buffer set management (double/triple-buffered resources)
    uint32_t getComputeBufferSet() const { return bufferSets_.getComputeSet(); }
    uint32_t getRenderBufferSet() const { return bufferSets_.getRenderSet(); }
    uint32_t getBufferSetCount() const { return bufferSets_.getSetCount(); }

    // Descriptor set access
    VkDescriptorSet getComputeDescriptorSet(uint32_t index) const { return computeDescriptorSets_[index]; }
    VkDescriptorSet getGraphicsDescriptorSet(uint32_t index) const { return graphicsDescriptorSets_[index]; }

    // Core components (same as ParticleSystem - composed from identical parts)
    SystemLifecycleHelper lifecycle_;
    BufferSetManager bufferSets_;

    // Compute and graphics descriptor sets (one per buffer set)
    std::vector<VkDescriptorSet> computeDescriptorSets_;
    std::vector<VkDescriptorSet> graphicsDescriptorSets_;

    // Stored init info
    vk::Device device_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    vk::RenderPass renderPass_;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    vk::Extent2D extent_;
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;

    vk::RenderPass shadowRenderPass_;
    uint32_t shadowMapSize_ = 0;

    // RAII device pointer
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Shadow pipeline (for casting shadows)
    std::optional<vk::raii::DescriptorSetLayout> shadowDescriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> shadowPipelineLayout_;
    std::optional<vk::raii::Pipeline> shadowPipeline_;

    // Displacement system (owned externally, provides displacement texture)
    DisplacementSystem* displacementSystem_ = nullptr;

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
    std::optional<vk::raii::Pipeline> tiledComputePipeline_;

    // Frame counter for tile unloading (ensures GPU isn't using tile before freeing)
    uint64_t frameCounter_ = 0;

    // Camera position for camera-centered dispatch
    glm::vec3 lastCameraPos_{0.0f};
};
