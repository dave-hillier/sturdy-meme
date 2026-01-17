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

#include "Camera.h"
#include "DescriptorManager.h"
#include "VulkanContext.h"
#include "UBOs.h"
#include "FrameData.h"
#include "RenderContext.h"
#include "VmaResources.h"
#include "RendererSystems.h"
#include "PerformanceToggles.h"
#include "TripleBuffering.h"
#include "RendererCore.h"
#include "vulkan/AsyncTransferManager.h"
#include "vulkan/ThreadedCommandPool.h"
#include "pipeline/FrameGraph.h"
#include "loading/LoadJobFactory.h"
#include "asset/AssetRegistry.h"
#include "passes/ShadowPassRecorder.h"
#include "passes/HDRPassRecorder.h"

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
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit Renderer(ConstructToken) {}

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


    // Physics debug (local state)
    void setPhysicsDebugEnabled(bool enabled) { physicsDebugEnabled = enabled; }
    bool isPhysicsDebugEnabled() const { return physicsDebugEnabled; }

    // Resource access
    VkCommandPool getCommandPool() const { return vulkanContext_->getCommandPool(); }
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
    AssetRegistry& getAssetRegistry() { return assetRegistry_; }
    const AssetRegistry& getAssetRegistry() const { return assetRegistry_; }

    // Performance control
    PerformanceToggles& getPerformanceToggles() { return perfToggles; }
    const PerformanceToggles& getPerformanceToggles() const { return perfToggles; }

#ifdef JPH_DEBUG_RENDERER
    // Update physics debug visualization (call before render)
    void updatePhysicsDebug(PhysicsWorld& physics, const glm::vec3& cameraPos);
#endif

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    // High-level initialization phases
    bool initCoreVulkanResources();       // swapchain resources, command pool, threading
    bool initDescriptorInfrastructure();  // layouts, pools, sets
    bool initSubsystems(const InitContext& initCtx);  // terrain, grass, weather, snow, water, etc.
    void initResizeCoordinator();         // resize registration

    bool createSyncObjects();
    bool createDescriptorSetLayout();
    void addCommonDescriptorBindings(DescriptorManager::LayoutBuilder& builder);
    bool createGraphicsPipeline();
    bool createDescriptorPool();
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

    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> graphicsPipeline_;

    bool physicsDebugEnabled = false;
    glm::mat4 lastViewProj{1.0f};  // Cached view-projection for debug rendering
    bool useVolumetricSnow = true;  // Use new volumetric system by default

    // Performance toggles for debugging
    PerformanceToggles perfToggles;


    std::optional<DescriptorManager::Pool> descriptorManagerPool;

    // Triple buffering: frame synchronization and indexing
    TripleBuffering frameSync_;

    // Core frame execution (owns the frame loop mechanics)
    RendererCore rendererCore_;

    // Pass recorders (encapsulate pass recording logic extracted from Renderer)
    std::unique_ptr<ShadowPassRecorder> shadowPassRecorder_;
    std::unique_ptr<HDRPassRecorder> hdrPassRecorder_;

    // Multi-threading infrastructure
    AsyncTransferManager asyncTransferManager_;
    ThreadedCommandPool threadedCommandPool_;
    FrameGraph frameGraph_;
    Loading::AsyncTextureUploader asyncTextureUploader_;

    // Centralized asset management
    AssetRegistry assetRegistry_;

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


    // Dynamic lights
    float lightCullRadius = 100.0f;        // Radius from camera for light culling

    // GUI rendering callback
    GuiRenderCallback guiRenderCallback;

    // Skinned mesh rendering
    bool initSkinnedMeshRenderer();
    bool createSkinnedMeshRendererDescriptorSets();

    // Initialize control subsystems (called after systems are ready)
    void initControlSubsystems();
};
