#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <optional>
#include <memory>

#include "BufferUtils.h"
#include "ParticleSystem.h"
#include "UBOs.h"
#include "interfaces/IWeatherState.h"
#include "interfaces/IRecordable.h"

// Forward declarations
class WindSystem;
class LeafSystem;
struct InitContext;

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

// Push constants for weather rendering
struct WeatherPushConstants {
    float time;
    float deltaTime;
    int cascadeIndex;
    int padding;
};

class WeatherSystem : public IWeatherState, public IRecordableAnimated {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit WeatherSystem(ConstructToken) {}

    using InitInfo = ParticleSystem::InitInfo;

    /**
     * Factory: Create and initialize WeatherSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<WeatherSystem> create(const InitInfo& info);

    /**
     * Bundle of weather-related systems
     */
    struct Bundle {
        std::unique_ptr<WeatherSystem> weather;
        std::unique_ptr<LeafSystem> leaf;
    };

    /**
     * Factory: Create WeatherSystem and LeafSystem together.
     * Returns nullopt on failure.
     */
    static std::optional<Bundle> createWithDependencies(
        const InitContext& ctx,
        VkRenderPass hdrRenderPass
    );

    ~WeatherSystem();

    // Non-copyable, non-movable
    WeatherSystem(const WeatherSystem&) = delete;
    WeatherSystem& operator=(const WeatherSystem&) = delete;
    WeatherSystem(WeatherSystem&&) = delete;
    WeatherSystem& operator=(WeatherSystem&&) = delete;

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { particleSystem->setExtent(newExtent); }

    // Update descriptor sets with external resources (UBO, wind buffer)
    void updateDescriptorSets(vk::Device device, const std::vector<vk::Buffer>& uniformBuffers,
                              const std::vector<vk::Buffer>& windBuffers,
                              vk::ImageView depthImageView, vk::Sampler depthSampler,
                              const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO = nullptr);

    // Set froxel volume for fog lighting on particles (Phase 4.3.9)
    void setFroxelVolume(VkImageView volumeView, VkSampler volumeSampler,
                         float farPlane, float depthDist);

    // Update weather uniforms each frame
    void updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                        const glm::mat4& viewProj, float deltaTime, float totalTime,
                        const WindSystem& windSystem);

    // Record compute dispatch for particle simulation
    void recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time, float deltaTime);

    // Record draw commands for weather particles (implements IRecordableAnimated)
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) override;

    // Double-buffer management
    void advanceBufferSet();

    // IWeatherState implementation
    void setIntensity(float intensity) override { weatherIntensity = intensity; }
    float getIntensity() const override { return weatherIntensity; }
    void setWeatherType(uint32_t type) override { weatherType = type; }
    uint32_t getWeatherType() const override { return weatherType; }

    // Weather control
    void setGroundLevel(float level) { groundLevel = level; }


private:
    bool initInternal(const InitInfo& info);
    void cleanup();

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
    const VkExtent2D& getExtent() const { return particleSystem ? particleSystem->getExtent() : storedExtent; }
    const std::string& getShaderPath() const { return storedShaderPath; }
    uint32_t getFramesInFlight() const { return storedFramesInFlight; }

    SystemLifecycleHelper::PipelineHandles& getComputePipelineHandles() { return particleSystem->getComputePipelineHandles(); }
    SystemLifecycleHelper::PipelineHandles& getGraphicsPipelineHandles() { return particleSystem->getGraphicsPipelineHandles(); }

    // RAII-managed subsystem
    std::unique_ptr<ParticleSystem> particleSystem;

    // Stored init info (available during initialization before particleSystem is created)
    VkDevice storedDevice = VK_NULL_HANDLE;
    VmaAllocator storedAllocator = VK_NULL_HANDLE;
    VkRenderPass storedRenderPass = VK_NULL_HANDLE;
    DescriptorManager::Pool* storedDescriptorPool = nullptr;
    VkExtent2D storedExtent = {0, 0};
    std::string storedShaderPath;
    uint32_t storedFramesInFlight = 0;

    // Triple-buffered storage buffers: one per frame in flight
    // Each frame gets its own buffer set to avoid GPU read/CPU write conflicts.
    // Buffer set count MUST match frames in flight (3) to prevent race conditions.
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

    // Dynamic renderer UBO - used with VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
    // to avoid per-frame descriptor set updates
    const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO_ = nullptr;

    // Froxel volume for fog particle lighting (Phase 4.3.9)
    VkImageView froxelVolumeView = VK_NULL_HANDLE;
    VkSampler froxelVolumeSampler = VK_NULL_HANDLE;
    float froxelFarPlane = 200.0f;
    float froxelDepthDist = 1.2f;

    // Particle counts based on intensity
    static constexpr uint32_t MAX_PARTICLES = 150000;
    static constexpr uint32_t WORKGROUP_SIZE = 256;
};
