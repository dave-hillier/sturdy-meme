#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <optional>
#include <memory>
#include <unordered_map>
#include <entt/fwd.hpp>

#include "Camera.h"
#include "DescriptorManager.h"
#include "VulkanContext.h"
#include "UBOs.h"
#include "FrameData.h"
#include "RenderContext.h"
#include "RenderPipeline.h"
#include "VmaResources.h"
#include "RendererSystems.h"
#include "PerformanceToggles.h"
#include "TripleBuffering.h"
#include "vulkan/AsyncTransferManager.h"
#include "vulkan/ThreadedCommandPool.h"
#include "pipeline/FrameGraph.h"
#include "loading/LoadJobFactory.h"

// Forward declarations
class PhysicsWorld;

// PBR texture flags - indicates which optional PBR textures are bound
// Must match definitions in push_constants_common.glsl
constexpr uint32_t PBR_HAS_ROUGHNESS_MAP = (1u << 0);
constexpr uint32_t PBR_HAS_METALLIC_MAP  = (1u << 1);
constexpr uint32_t PBR_HAS_AO_MAP        = (1u << 2);
constexpr uint32_t PBR_HAS_HEIGHT_MAP    = (1u << 3);


class Renderer {
public:
    // Configuration for renderer initialization
    struct Config {
        uint32_t setsPerPool = 64;
        DescriptorPoolSizes descriptorPoolSizes = DescriptorPoolSizes::standard();
    };

    struct InitInfo {
        SDL_Window* window;
        std::string resourcePath;
        Config config{};  // Optional renderer configuration

        // Optional: pre-initialized VulkanContext (instance already created)
        // If provided, Renderer takes ownership and completes device init.
        // If nullptr, Renderer creates and fully initializes a new VulkanContext.
        std::unique_ptr<VulkanContext> vulkanContext;
    };

    /**
     * Factory: Create and initialize Renderer.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<Renderer> create(const InitInfo& info);

    ~Renderer();

    // Non-copyable, non-movable
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Returns true if frame was rendered, false if skipped (caller must handle GUI frame cancellation)
    bool render(const Camera& camera);
    void waitIdle();

    // Wait for the previous frame's GPU work to complete.
    // MUST be called before destroying/updating any mesh buffers that the previous frame used.
    // This prevents race conditions where GPU is reading buffers we're about to destroy.
    void waitForPreviousFrame();

    // Viewport dimensions
    uint32_t getWidth() const { return vulkanContext_->getWidth(); }
    uint32_t getHeight() const { return vulkanContext_->getHeight(); }

    // Handle window resize (recreate swapchain and dependent resources)
    bool handleResize();

    // Notify renderer that window was resized (will trigger resize on next render)
    void notifyWindowResized() { framebufferResized = true; }

    // Notify renderer that window was minimized/hidden (e.g., screen lock on macOS)
    void notifyWindowSuspended() { windowSuspended = true; }

    // Notify renderer that window was restored (e.g., screen unlock on macOS)
    void notifyWindowRestored() {
        windowSuspended = false;
        framebufferResized = true;  // Force swapchain recreation after restore
    }

    bool isWindowSuspended() const { return windowSuspended; }

    // Vulkan handle getters for GUI integration
    vk::Instance getInstance() const { return vulkanContext_->getVkInstance(); }
    vk::PhysicalDevice getPhysicalDevice() const { return vulkanContext_->getVkPhysicalDevice(); }
    vk::Device getDevice() const { return vulkanContext_->getVkDevice(); }
    vk::Queue getGraphicsQueue() const { return vulkanContext_->getVkGraphicsQueue(); }
    uint32_t getGraphicsQueueFamily() const { return vulkanContext_->getGraphicsQueueFamily(); }
    VkRenderPass getSwapchainRenderPass() const { return **renderPass_; }
    uint32_t getSwapchainImageCount() const { return vulkanContext_->getSwapchainImageCount(); }

    // Access to VulkanContext
    VulkanContext& getVulkanContext() { return *vulkanContext_; }
    const VulkanContext& getVulkanContext() const { return *vulkanContext_; }

    // Interface accessors - provide access to GUI-facing control interfaces via RendererSystems
    ILocationControl& getLocationControl() { return systems_->locationControl(); }
    const ILocationControl& getLocationControl() const { return systems_->locationControl(); }
    IWeatherState& getWeatherState() { return systems_->weatherState(); }
    const IWeatherState& getWeatherState() const { return systems_->weatherState(); }
    IEnvironmentControl& getEnvironmentControl() { return systems_->environmentControl(); }
    const IEnvironmentControl& getEnvironmentControl() const { return systems_->environmentControl(); }
    IPostProcessState& getPostProcessState() { return systems_->postProcessState(); }
    const IPostProcessState& getPostProcessState() const { return systems_->postProcessState(); }
    ICloudShadowControl& getCloudShadowControl() { return systems_->cloudShadowControl(); }
    const ICloudShadowControl& getCloudShadowControl() const { return systems_->cloudShadowControl(); }
    ITerrainControl& getTerrainControl() { return systems_->terrainControl(); }
    const ITerrainControl& getTerrainControl() const { return systems_->terrainControl(); }
    IWaterControl& getWaterControl() { return systems_->waterControl(); }
    const IWaterControl& getWaterControl() const { return systems_->waterControl(); }
    ITreeControl& getTreeControl() { return systems_->treeControl(); }
    const ITreeControl& getTreeControl() const { return systems_->treeControl(); }
    IDebugControl& getDebugControl() { return systems_->debugControl(); }
    const IDebugControl& getDebugControl() const { return systems_->debugControl(); }
    IProfilerControl& getProfilerControl() { return systems_->profilerControl(); }
    const IProfilerControl& getProfilerControl() const { return systems_->profilerControl(); }
    IPerformanceControl& getPerformanceControl() { return systems_->performanceControl(); }
    const IPerformanceControl& getPerformanceControl() const { return systems_->performanceControl(); }
    ISceneControl& getSceneControl() { return systems_->sceneControl(); }
    const ISceneControl& getSceneControl() const { return systems_->sceneControl(); }
    IPlayerControl& getPlayerControl() { return systems_->playerControl(); }
    const IPlayerControl& getPlayerControl() const { return systems_->playerControl(); }

    // GUI rendering callback (called during swapchain render pass)
    using GuiRenderCallback = std::function<void(VkCommandBuffer)>;
    void setGuiRenderCallback(GuiRenderCallback callback) { guiRenderCallback = callback; }

    // RendererSystems access - use this for all subsystem access
    RendererSystems& getSystems() { return *systems_; }
    const RendererSystems& getSystems() const { return *systems_; }

    // Local rendering state (affects render loop directly)
    void setHDREnabled(bool enabled) { hdrEnabled = enabled; }
    bool isHDREnabled() const { return hdrEnabled; }
    void setHDRPassEnabled(bool enabled) { hdrPassEnabled = enabled; }
    bool isHDRPassEnabled() const { return hdrPassEnabled; }
    void setTerrainEnabled(bool enabled) { terrainEnabled = enabled; }
    bool isTerrainEnabled() const { return terrainEnabled; }

    // Cloud parameters (synced to multiple subsystems in render loop)
    void setCloudCoverage(float coverage);
    float getCloudCoverage() const { return cloudCoverage; }
    void setCloudDensity(float density);
    float getCloudDensity() const { return cloudDensity; }
    void setSkyExposure(float exposure);
    float getSkyExposure() const { return skyExposure; }

    // Debug visualization toggles (local state)
    void toggleCascadeDebug() { showCascadeDebug = !showCascadeDebug; }
    bool isShowingCascadeDebug() const { return showCascadeDebug; }
    void toggleSnowDepthDebug() { showSnowDepthDebug = !showSnowDepthDebug; }
    bool isShowingSnowDepthDebug() const { return showSnowDepthDebug; }
    void toggleCloudStyle() { useParaboloidClouds = !useParaboloidClouds; }
    bool isUsingParaboloidClouds() const { return useParaboloidClouds; }

    // Player state for grass/snow interaction (used in render loop)
    void setPlayerPosition(const glm::vec3& position, float radius);
    void setPlayerState(const glm::vec3& position, const glm::vec3& velocity, float radius);

    // ECS integration - set registry for ECS-based lighting
    // When set, lights from ECS take precedence over LightManager
    void setECSRegistry(entt::registry* registry) { ecsRegistry_ = registry; }
    entt::registry* getECSRegistry() const { return ecsRegistry_; }

    // Physics debug (local state)
    void setPhysicsDebugEnabled(bool enabled) { physicsDebugEnabled = enabled; }
    bool isPhysicsDebugEnabled() const { return physicsDebugEnabled; }

    // Road/river visualization (uses debug line system)
    // Delegates to DebugControlSubsystem - use getDebugControl() for GUI access
    void updateRoadRiverVisualization();

    // Resource access
    VkCommandPool getCommandPool() const { return **commandPool_; }
    DescriptorManager::Pool* getDescriptorPool();
    std::string getShaderPath() const { return resourcePath + "/shaders"; }
    const std::string& getResourcePath() const { return resourcePath; }

    // Multi-threading infrastructure (from video: async transfers, threaded command pools, frame graph)
    AsyncTransferManager& getAsyncTransferManager() { return asyncTransferManager_; }
    const AsyncTransferManager& getAsyncTransferManager() const { return asyncTransferManager_; }
    ThreadedCommandPool& getThreadedCommandPool() { return threadedCommandPool_; }
    const ThreadedCommandPool& getThreadedCommandPool() const { return threadedCommandPool_; }
    FrameGraph& getFrameGraph() { return frameGraph_; }
    const FrameGraph& getFrameGraph() const { return frameGraph_; }
    Loading::AsyncTextureUploader& getAsyncTextureUploader() { return asyncTextureUploader_; }
    const Loading::AsyncTextureUploader& getAsyncTextureUploader() const { return asyncTextureUploader_; }

    // Performance control
    PerformanceToggles& getPerformanceToggles() { return perfToggles; }
    const PerformanceToggles& getPerformanceToggles() const { return perfToggles; }
    void syncPerformanceToggles();

#ifdef JPH_DEBUG_RENDERER
    // Update physics debug visualization (call before render)
    void updatePhysicsDebug(PhysicsWorld& physics, const glm::vec3& cameraPos);
#endif

private:
    Renderer() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    // High-level initialization phases
    bool initCoreVulkanResources();       // render pass, depth, framebuffers, command pool
    bool initDescriptorInfrastructure();  // layouts, pools, sets
    bool initSubsystems(const InitContext& initCtx);  // terrain, grass, weather, snow, water, etc.
    void initResizeCoordinator();         // resize registration

    bool createRenderPass();
    void destroyRenderResources();
    void destroyDepthImageAndView();  // Helper for resize (keeps sampler)
    void destroyFramebuffers();       // Helper for resize
    bool recreateDepthResources(VkExtent2D newExtent);  // Helper for resize
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createDescriptorSetLayout();
    void addCommonDescriptorBindings(DescriptorManager::LayoutBuilder& builder);
    bool createGraphicsPipeline();
    bool createDescriptorPool();
    bool createDescriptorSets();
    bool createDepthResources();


    void updateUniformBuffer(uint32_t currentImage, const Camera& camera);

    // Render pass recording helpers (pure - only record commands, no state mutation)
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime, const glm::vec3& cameraPosition);
    void recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime);
    void recordHDRPassWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime,
                                      const std::vector<vk::CommandBuffer>& secondaries);
    void recordHDRPassSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime, uint32_t slot);
    void recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex);

    // Setup render pipeline stages with lambdas (called once during init)
    void setupRenderPipeline();

    // Setup frame graph passes with dependencies (Phase 3: frame graph integration)
    void setupFrameGraph();

    // Pure calculation helpers (no state mutation)
    glm::vec2 calculateSunScreenPos(const Camera& camera, const glm::vec3& sunDir) const;

    // Build per-frame shared state from camera and timing
    FrameData buildFrameData(const Camera& camera, float deltaTime, float time) const;

    // Build render resources snapshot for pipeline stages
    RenderResources buildRenderResources(uint32_t swapchainImageIndex) const;

    SDL_Window* window = nullptr;
    std::string resourcePath;
    Config config_;  // Renderer configuration

    std::unique_ptr<VulkanContext> vulkanContext_;

    // All rendering subsystems - managed with automatic lifecycle
    std::unique_ptr<RendererSystems> systems_;

    std::optional<vk::raii::RenderPass> renderPass_;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> graphicsPipeline_;

    bool physicsDebugEnabled = false;
    glm::mat4 lastViewProj{1.0f};  // Cached view-projection for debug rendering
    bool useVolumetricSnow = true;  // Use new volumetric system by default

    // Performance toggles for debugging
    PerformanceToggles perfToggles;

    // Render pipeline (stages abstraction - for future refactoring)
    RenderPipeline renderPipeline;

    std::vector<vk::raii::Framebuffer> framebuffers_;
    std::optional<vk::raii::CommandPool> commandPool_;
    std::vector<VkCommandBuffer> commandBuffers;

    ManagedImage depthImage_;
    std::optional<vk::raii::ImageView> depthImageView_;
    std::optional<vk::raii::Sampler> depthSampler_;  // For Hi-Z pyramid generation
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    std::optional<DescriptorManager::Pool> descriptorManagerPool;

    // Triple buffering: frame synchronization and indexing
    TripleBuffering frameSync_;

    // Multi-threading infrastructure
    AsyncTransferManager asyncTransferManager_;
    ThreadedCommandPool threadedCommandPool_;
    FrameGraph frameGraph_;
    Loading::AsyncTextureUploader asyncTextureUploader_;

    // Rock descriptor sets (RockSystem has its own textures, not in MaterialRegistry)
    std::vector<VkDescriptorSet> rockDescriptorSets;

    // Detritus descriptor sets (DetritusSystem has its own bark textures)
    std::vector<VkDescriptorSet> detritusDescriptorSets;

    // Tree descriptor sets per texture type (keyed by type name string)
    // Each map has entries for each frame: map[typeName][frameIndex]
    std::unordered_map<std::string, std::vector<VkDescriptorSet>> treeBarkDescriptorSets;
    std::unordered_map<std::string, std::vector<VkDescriptorSet>> treeLeafDescriptorSets;

    // Convenience accessor for frame count (matches TripleBuffering::DEFAULT_FRAME_COUNT)
    static constexpr int MAX_FRAMES_IN_FLIGHT = TripleBuffering::DEFAULT_FRAME_COUNT;

    float lastSunIntensity = 1.0f;

    bool showCascadeDebug = false;         // true = show cascade colors overlay
    bool showSnowDepthDebug = false;       // true = show snow depth heat map overlay
    bool useParaboloidClouds = true;       // true = paraboloid LUT hybrid, false = procedural
    bool hdrEnabled = true;                // true = HDR tonemapping/bloom, false = bypass
    bool hdrPassEnabled = true;            // true = render HDR pass, false = skip entire HDR scene rendering
    bool terrainEnabled = true;            // true = render terrain, false = skip terrain rendering

    // Cloud parameters (synced to UBO, cloud shadows, and cloud map LUT)
    float cloudCoverage = 0.5f;            // 0-1 cloud coverage amount
    float cloudDensity = 0.3f;             // Base density multiplier

    // Sky parameters (synced to UBO)
    float skyExposure = 5.0f;              // Sky brightness multiplier (1-20)
    bool framebufferResized = false;       // true = window resized, need to recreate swapchain
    bool windowSuspended = false;          // true = window minimized/hidden (macOS screen lock)
    bool useFrameGraph = true;             // true = use FrameGraph for pass scheduling (Phase 3)

    // Player position for grass displacement
    glm::vec3 playerPosition = glm::vec3(0.0f);
    glm::vec3 playerVelocity = glm::vec3(0.0f);
    float playerCapsuleRadius = 0.3f;      // Default capsule radius

    // Dynamic lights
    float lightCullRadius = 100.0f;        // Radius from camera for light culling

    // ECS integration for lights
    entt::registry* ecsRegistry_ = nullptr;  // Optional ECS registry for ECS-based lighting

    // GUI rendering callback
    GuiRenderCallback guiRenderCallback;

    void updateLightBuffer(uint32_t currentImage, const Camera& camera);

    // Skinned mesh rendering
    bool initSkinnedMeshRenderer();
    bool createSkinnedMeshRendererDescriptorSets();

    // Hi-Z occlusion culling
    void updateHiZObjectData();

    // Initialize control subsystems (called after systems are ready)
    void initControlSubsystems();
};
