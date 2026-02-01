#pragma once

#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <optional>
#include <memory>

#include "Camera.h"
#include "VulkanContext.h"
#include "RendererSystems.h"
#include "InitContext.h"
#include "PerformanceToggles.h"
#include "TripleBuffering.h"
#include "RendererCore.h"
#include "RenderingInfrastructure.h"
#include "DescriptorInfrastructure.h"
#include "passes/ShadowPassRecorder.h"
#include "passes/HDRPassRecorder.h"

// Forward declarations
class PhysicsWorld;

namespace ecs {
class World;
}

namespace Loading {
    class AsyncSystemLoader;
}

// PBR texture flags - indicates which optional PBR textures are bound
// Must match definitions in push_constants_common.glsl
constexpr uint32_t PBR_HAS_ROUGHNESS_MAP = (1u << 0);
constexpr uint32_t PBR_HAS_METALLIC_MAP  = (1u << 1);
constexpr uint32_t PBR_HAS_AO_MAP        = (1u << 2);
constexpr uint32_t PBR_HAS_HEIGHT_MAP    = (1u << 3);


class Renderer {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit Renderer(ConstructToken);

    // Configuration for renderer initialization
    struct Config {
        uint32_t setsPerPool = 64;
        DescriptorPoolSizes descriptorPoolSizes = DescriptorPoolSizes::standard();
    };

    // Progress callback for async loading feedback
    // Called during initialization with progress (0.0-1.0) and phase description
    using ProgressCallback = std::function<void(float progress, const char* phase)>;

    struct InitInfo {
        SDL_Window* window;
        std::string resourcePath;
        Config config{};  // Optional renderer configuration

        // Optional: pre-initialized VulkanContext (instance already created)
        // If provided, Renderer takes ownership and completes device init.
        // If nullptr, Renderer creates and fully initializes a new VulkanContext.
        std::unique_ptr<VulkanContext> vulkanContext;

        // Optional: progress callback for async loading feedback
        // When provided, renderer will call this between initialization phases
        // to allow the caller to render a loading screen with progress updates
        ProgressCallback progressCallback;

        // Enable async subsystem initialization (non-blocking loading screen)
        // When true, heavy subsystems are loaded on background threads while
        // the loading screen continues to render. This improves perceived startup time.
        bool asyncInit = false;
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

    // Notify renderer that window lost focus (user clicked another app)
    // On macOS, this can cause compositor to cache stale content
    void notifyWindowFocusLost() { windowFocusLost_ = true; }

    // Notify renderer that window regained focus - invalidate all temporal history
    // to prevent ghost frames from blending with stale compositor-cached content
    void notifyWindowFocusGained();

    // Vulkan handle getters for GUI integration
    vk::Instance getInstance() const { return vulkanContext_->getVkInstance(); }
    vk::PhysicalDevice getPhysicalDevice() const { return vulkanContext_->getVkPhysicalDevice(); }
    vk::Device getDevice() const { return vulkanContext_->getVkDevice(); }
    vk::Queue getGraphicsQueue() const { return vulkanContext_->getVkGraphicsQueue(); }
    uint32_t getGraphicsQueueFamily() const { return vulkanContext_->getGraphicsQueueFamily(); }
    VkRenderPass getSwapchainRenderPass() const { return vulkanContext_->getRenderPass(); }
    uint32_t getSwapchainImageCount() const { return vulkanContext_->getSwapchainImageCount(); }

    // Access to VulkanContext
    VulkanContext& getVulkanContext() { return *vulkanContext_; }
    const VulkanContext& getVulkanContext() const { return *vulkanContext_; }

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

    // Debug visualization toggles (local state)
    void toggleCascadeDebug() { showCascadeDebug = !showCascadeDebug; }
    bool isShowingCascadeDebug() const { return showCascadeDebug; }
    void toggleSnowDepthDebug() { showSnowDepthDebug = !showSnowDepthDebug; }
    bool isShowingSnowDepthDebug() const { return showSnowDepthDebug; }

    // Resource access
    VkCommandPool getCommandPool() const { return vulkanContext_->getCommandPool(); }
    DescriptorManager::Pool* getDescriptorPool();
    std::string getShaderPath() const { return resourcePath + "/shaders"; }
    const std::string& getResourcePath() const { return resourcePath; }

    // Multi-threading infrastructure (delegated to RenderingInfrastructure)
    AsyncTransferManager& getAsyncTransferManager() { return renderingInfra_.asyncTransferManager(); }
    const AsyncTransferManager& getAsyncTransferManager() const { return renderingInfra_.asyncTransferManager(); }
    ThreadedCommandPool& getThreadedCommandPool() { return renderingInfra_.threadedCommandPool(); }
    const ThreadedCommandPool& getThreadedCommandPool() const { return renderingInfra_.threadedCommandPool(); }
    FrameGraph& getFrameGraph() { return renderingInfra_.frameGraph(); }
    const FrameGraph& getFrameGraph() const { return renderingInfra_.frameGraph(); }
    Loading::AsyncTextureUploader& getAsyncTextureUploader() { return renderingInfra_.asyncTextureUploader(); }
    const Loading::AsyncTextureUploader& getAsyncTextureUploader() const { return renderingInfra_.asyncTextureUploader(); }
    AssetRegistry& getAssetRegistry() { return renderingInfra_.assetRegistry(); }
    const AssetRegistry& getAssetRegistry() const { return renderingInfra_.assetRegistry(); }

    // Performance control
    PerformanceToggles& getPerformanceToggles() { return perfToggles; }
    const PerformanceToggles& getPerformanceToggles() const { return perfToggles; }

    // ECS integration for light updates
    void setECSWorld(ecs::World* world) { ecsWorld_ = world; }
    ecs::World* getECSWorld() const { return ecsWorld_; }

#ifdef JPH_DEBUG_RENDERER
    // Update physics debug visualization (call before render)
    void updatePhysicsDebug(PhysicsWorld& physics, const glm::vec3& cameraPos);
#endif

    /**
     * Create async system loader for background initialization
     * Returns a loader that the caller can poll while rendering loading screen
     * Caller must call pollAsyncInit() until isAsyncInitComplete() returns true
     */
    static std::unique_ptr<class Loading::AsyncSystemLoader> createAsyncLoader(const InitInfo& info);

    /**
     * Check if async initialization is complete
     */
    bool isAsyncInitComplete() const { return asyncInitComplete_; }

    /**
     * Poll async loader for completions - call from main thread
     * Returns true if all initialization is complete
     */
    bool pollAsyncInit();

private:
    bool initInternal(const InitInfo& info);
    bool initInternalAsync(const InitInfo& info);  // Async initialization path
    void cleanup();

    // High-level initialization phases
    bool initCoreVulkanResources();       // swapchain resources, command pool, threading
    bool initDescriptorInfrastructure();  // layouts, pools, sets
    bool initSubsystems(const InitContext& initCtx);  // terrain, grass, weather, snow, water, etc.
    bool initSubsystemsAsync();  // Async version using AsyncSystemLoader (uses asyncInitContext_)
    void initResizeCoordinator();         // resize registration
    void initTemporalSystems();           // temporal system registration (for ghost frame prevention)

    bool createSyncObjects();
    bool createDescriptorSets();

    // Render pass recording helpers (pure - only record commands, no state mutation)
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime, const glm::vec3& cameraPosition);
    void recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime);
    void recordHDRPassWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime,
                                      const std::vector<vk::CommandBuffer>& secondaries);
    void recordHDRPassSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime, uint32_t slot);

    // Setup frame graph passes with dependencies
    void setupFrameGraph();

    std::string resourcePath;
    Config config_;  // Renderer configuration

    std::unique_ptr<VulkanContext> vulkanContext_;

    // All rendering subsystems - managed with automatic lifecycle
    std::unique_ptr<RendererSystems> systems_;

    // Descriptor and pipeline infrastructure (extracted from Renderer)
    DescriptorInfrastructure descriptorInfra_;

    glm::mat4 lastViewProj{1.0f};  // Cached view-projection for debug rendering
    bool useVolumetricSnow = true;  // Use new volumetric system by default

    // Performance toggles for debugging
    PerformanceToggles perfToggles;


    // Triple buffering: frame synchronization and indexing
    TripleBuffering frameSync_;

    // Core frame execution (owns the frame loop mechanics)
    RendererCore rendererCore_;

    // Pass recorders (encapsulate pass recording logic extracted from Renderer)
    std::unique_ptr<ShadowPassRecorder> shadowPassRecorder_;
    std::unique_ptr<HDRPassRecorder> hdrPassRecorder_;

    // Multi-threading and asset infrastructure (extracted from Renderer)
    RenderingInfrastructure renderingInfra_;

    // Convenience accessor for frame count (matches TripleBuffering::DEFAULT_FRAME_COUNT)
    static constexpr int MAX_FRAMES_IN_FLIGHT = TripleBuffering::DEFAULT_FRAME_COUNT;

    float lastSunIntensity = 1.0f;

    bool showCascadeDebug = false;         // true = show cascade colors overlay
    bool showSnowDepthDebug = false;       // true = show snow depth heat map overlay
    bool hdrEnabled = true;                // true = HDR tonemapping/bloom, false = bypass
    bool hdrPassEnabled = true;            // true = render HDR pass, false = skip entire HDR scene rendering
    bool terrainEnabled = true;            // true = render terrain, false = skip terrain rendering

    // Note: Cloud parameters (cloudCoverage, cloudDensity, skyExposure, useParaboloidClouds)
    // are now managed by EnvironmentControlSubsystem as the authoritative source.

    bool framebufferResized = false;       // true = window resized, need to recreate swapchain
    bool windowSuspended = false;          // true = window minimized/hidden (macOS screen lock)
    bool windowFocusLost_ = false;         // true = window lost focus, need to invalidate temporal on regain


    // Dynamic lights
    float lightCullRadius = 100.0f;        // Radius from camera for light culling

    // ECS world for light updates
    ecs::World* ecsWorld_ = nullptr;
    float lastDeltaTime_ = 0.016f;         // For flicker animation

    // GUI rendering callback
    GuiRenderCallback guiRenderCallback;

    // Progress callback for async loading (stored during init)
    ProgressCallback progressCallback_;

    // Async initialization state
    std::unique_ptr<Loading::AsyncSystemLoader> asyncLoader_;
    InitContext asyncInitContext_;  // Stored for async task access
    bool asyncInitComplete_ = true;  // True when not using async, or when async is done
    bool asyncInitStarted_ = false;

    // Skinned mesh rendering
    bool initSkinnedMeshRenderer();
    bool createSkinnedMeshRendererDescriptorSets();

    // Initialize control subsystems (called after systems are ready)
    void initControlSubsystems();
};
