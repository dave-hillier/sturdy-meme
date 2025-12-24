#pragma once

#include <vulkan/vulkan.h>
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
#include "RenderPipeline.h"
#include "VulkanRAII.h"
#include "RendererSystems.h"
#include "PerformanceToggles.h"

// GUI-facing interfaces
#include "interfaces/ILocationControl.h"
#include "interfaces/IWeatherControl.h"
#include "interfaces/IEnvironmentControl.h"
#include "interfaces/IPostProcessControl.h"
#include "interfaces/ITerrainControl.h"
#include "interfaces/IWaterControl.h"
#include "interfaces/ITreeControl.h"
#include "interfaces/IDebugControl.h"
#include "interfaces/IProfilerControl.h"
#include "interfaces/IPerformanceControl.h"
#include "interfaces/ISceneControl.h"
#include "interfaces/IPlayerControl.h"

// Forward declarations for types used in public API
class PostProcessSystem;
class TimeSystem;
class TerrainSystem;
class CatmullClarkSystem;
class WindSystem;
class WaterSystem;
class WaterTileCull;
class SceneManager;
class DebugLineSystem;
class Profiler;
class HiZSystem;
class AtmosphereLUTSystem;
struct AtmosphereParams;
struct EnvironmentSettings;
struct GeographicLocation;
class CelestialCalculator;
struct WaterPlacementData;
class ErosionDataLoader;
class SceneBuilder;
class Mesh;
class PhysicsWorld;
class TreeSystem;
class DetritusSystem;

#ifdef JPH_DEBUG_RENDERER
class PhysicsDebugRenderer;
#endif

// PBR texture flags - indicates which optional PBR textures are bound
// Must match definitions in push_constants_common.glsl
constexpr uint32_t PBR_HAS_ROUGHNESS_MAP = (1u << 0);
constexpr uint32_t PBR_HAS_METALLIC_MAP  = (1u << 1);
constexpr uint32_t PBR_HAS_AO_MAP        = (1u << 2);
constexpr uint32_t PBR_HAS_HEIGHT_MAP    = (1u << 3);


class Renderer : public ILocationControl,
                  public IWeatherControl,
                  public IEnvironmentControl,
                  public IPostProcessControl,
                  public ITerrainControl,
                  public IWaterControl,
                  public ITreeControl,
                  public IDebugControl,
                  public IProfilerControl,
                  public IPerformanceControl,
                  public ISceneControl,
                  public IPlayerControl {
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

    // ISceneControl implementation (viewport dimensions)
    uint32_t getWidth() const override { return vulkanContext.getWidth(); }
    uint32_t getHeight() const override { return vulkanContext.getHeight(); }

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
    VkInstance getInstance() const { return vulkanContext.getInstance(); }
    VkPhysicalDevice getPhysicalDevice() const { return vulkanContext.getPhysicalDevice(); }
    VkDevice getDevice() const { return vulkanContext.getDevice(); }
    VkQueue getGraphicsQueue() const { return vulkanContext.getGraphicsQueue(); }
    uint32_t getGraphicsQueueFamily() const { return vulkanContext.getGraphicsQueueFamily(); }
    VkRenderPass getSwapchainRenderPass() const { return renderPass.get(); }
    uint32_t getSwapchainImageCount() const { return vulkanContext.getSwapchainImageCount(); }

    // Access to VulkanContext
    VulkanContext& getVulkanContext() { return vulkanContext; }
    const VulkanContext& getVulkanContext() const { return vulkanContext; }

    // GUI rendering callback (called during swapchain render pass)
    using GuiRenderCallback = std::function<void(VkCommandBuffer)>;
    void setGuiRenderCallback(GuiRenderCallback callback) { guiRenderCallback = callback; }

    // Time system access (TimeSystem implements ITimeSystem directly)
    void setTimeScale(float scale);
    float getTimeScale() const;
    void setTimeOfDay(float time);
    void resumeAutoTime();
    float getTimeOfDay() const;
    TimeSystem& getTimeSystem();
    const TimeSystem& getTimeSystem() const;

    // IDebugControl implementation (partial)
    void toggleCascadeDebug() override { showCascadeDebug = !showCascadeDebug; }
    bool isShowingCascadeDebug() const override { return showCascadeDebug; }

    void toggleSnowDepthDebug() override { showSnowDepthDebug = !showSnowDepthDebug; }
    bool isShowingSnowDepthDebug() const override { return showSnowDepthDebug; }

    // IEnvironmentControl implementation (partial)
    void toggleCloudStyle() override { useParaboloidClouds = !useParaboloidClouds; }
    bool isUsingParaboloidClouds() const override { return useParaboloidClouds; }

    void setCloudCoverage(float coverage) override;
    float getCloudCoverage() const override { return cloudCoverage; }

    void setCloudDensity(float density) override;
    float getCloudDensity() const override { return cloudDensity; }

    void setSkyExposure(float exposure) override;
    float getSkyExposure() const override;

    // IPostProcessControl implementation
    void setCloudShadowEnabled(bool enabled) override;
    bool isCloudShadowEnabled() const override;

    void setHDREnabled(bool enabled) override { hdrEnabled = enabled; }
    bool isHDREnabled() const override { return hdrEnabled; }

    void setHDRPassEnabled(bool enabled) override { hdrPassEnabled = enabled; }
    bool isHDRPassEnabled() const override { return hdrPassEnabled; }

    void setBloomEnabled(bool enabled) override;
    bool isBloomEnabled() const override;

    void setAutoExposureEnabled(bool enabled) override;
    bool isAutoExposureEnabled() const override;
    void setManualExposure(float ev) override;
    float getManualExposure() const override;
    float getCurrentExposure() const override;

    void setCloudShadowIntensity(float intensity) override;
    float getCloudShadowIntensity() const override;

    void setGodRaysEnabled(bool enabled) override;
    bool isGodRaysEnabled() const override;
    void setGodRayQuality(int quality) override;  // 0=Low, 1=Medium, 2=High
    int getGodRayQuality() const override;

    void setFroxelFilterQuality(bool highQuality) override;
    bool isFroxelFilterHighQuality() const override;

    void setLocalToneMapEnabled(bool enabled) override;
    bool isLocalToneMapEnabled() const override;
    void setLocalToneMapContrast(float c) override;
    float getLocalToneMapContrast() const override;
    void setLocalToneMapDetail(float d) override;
    float getLocalToneMapDetail() const override;
    void setBilateralBlend(float b) override;
    float getBilateralBlend() const override;

    // ITerrainControl implementation
    void setTerrainEnabled(bool enabled) override { terrainEnabled = enabled; }
    bool isTerrainEnabled() const override { return terrainEnabled; }
    void toggleTerrainWireframe() override;
    bool isTerrainWireframeMode() const override;
    float getTerrainHeightAt(float x, float z) const override;
    uint32_t getTerrainNodeCount() const override;

    const TerrainSystem& getTerrainSystem() const override;
    TerrainSystem& getTerrainSystem() override;

    // Catmull-Clark subdivision control
    void toggleCatmullClarkWireframe();
    bool isCatmullClarkWireframeMode() const;
    CatmullClarkSystem& getCatmullClarkSystem();

    // IWeatherControl implementation
    void setWeatherIntensity(float intensity) override;
    void setWeatherType(uint32_t type) override;
    uint32_t getWeatherType() const override;
    float getIntensity() const override;

    // IEnvironmentControl implementation (fog)
    void setFogDensity(float density) override;
    float getFogDensity() const override;
    void setFogEnabled(bool enabled) override;
    bool isFogEnabled() const override;

    void setFogBaseHeight(float h) override;
    float getFogBaseHeight() const override;
    void setFogScaleHeight(float h) override;
    float getFogScaleHeight() const override;
    void setFogAbsorption(float a) override;
    float getFogAbsorption() const override;
    void setVolumetricFarPlane(float f) override;
    float getVolumetricFarPlane() const override;
    void setTemporalBlend(float b) override;
    float getTemporalBlend() const override;

    void setLayerHeight(float h) override;
    float getLayerHeight() const override;
    void setLayerThickness(float t) override;
    float getLayerThickness() const override;
    void setLayerDensity(float d) override;
    float getLayerDensity() const override;

    void setAtmosphereParams(const AtmosphereParams& params) override;
    const AtmosphereParams& getAtmosphereParams() const override;
    AtmosphereLUTSystem& getAtmosphereLUTSystem();

    void setLeafIntensity(float intensity) override;
    float getLeafIntensity() const override;
    void spawnConfetti(const glm::vec3& position, float velocity = 8.0f, float count = 100.0f, float coneAngle = 0.5f);

    // IWeatherControl implementation (snow)
    void setSnowAmount(float amount) override;
    float getSnowAmount() const override;
    void setSnowColor(const glm::vec3& color) override;
    const glm::vec3& getSnowColor() const override;
    void addSnowInteraction(const glm::vec3& position, float radius, float strength);
    EnvironmentSettings& getEnvironmentSettings() override;

    // Scene access
    SceneManager& getSceneManager();
    const SceneManager& getSceneManager() const;

    // Rock system access for physics integration
    const RockSystem& getRockSystem() const;

    // Detritus system access for physics integration
    const DetritusSystem* getDetritusSystem() const;

    // ITreeControl implementation
    TreeSystem* getTreeSystem() override;
    const TreeSystem* getTreeSystem() const override;

    RendererSystems& getSystems() override { return *systems_; }
    const RendererSystems& getSystems() const override { return *systems_; }

    // Player position for grass interaction (xyz = position, w = capsule radius)
    void setPlayerPosition(const glm::vec3& position, float radius);
    void setPlayerState(const glm::vec3& position, const glm::vec3& velocity, float radius);

    // Access to systems for simulation
    WindSystem& getWindSystem();
    const WindSystem& getWindSystem() const;

    // IWaterControl implementation
    WaterSystem& getWaterSystem() override;
    const WaterSystem& getWaterSystem() const override;
    WaterTileCull& getWaterTileCull() override;
    const WaterTileCull& getWaterTileCull() const override;
    const WaterPlacementData& getWaterPlacementData() const;

    // ISceneControl / IPlayerControl implementation
    SceneBuilder& getSceneBuilder() override;
    const SceneBuilder& getSceneBuilder() const override;
    Mesh& getFlagClothMesh();
    Mesh& getFlagPoleMesh();
    void uploadFlagClothMesh();

    // Animated character update
    // movementSpeed: horizontal speed for animation state selection
    // isGrounded: whether on the ground
    // isJumping: whether just started jumping
    void updateAnimatedCharacter(float deltaTime, float movementSpeed = 0.0f, bool isGrounded = true, bool isJumping = false);

    // Start a jump with trajectory prediction for animation sync
    void startCharacterJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics);

    // ILocationControl implementation
    void setLocation(const GeographicLocation& location) override;
    const GeographicLocation& getLocation() const override;
    const CelestialCalculator& getCelestialCalculator() const;

    // Time system delegates (TimeSystem implements ITimeSystem)
    void setDate(int year, int month, int day);
    int getCurrentYear() const;
    int getCurrentMonth() const;
    int getCurrentDay() const;

    void setMoonPhaseOverride(bool enabled);
    bool isMoonPhaseOverrideEnabled() const;
    void setMoonPhase(float phase);
    float getMoonPhase() const;
    float getCurrentMoonPhase() const;

    void setMoonBrightness(float brightness);
    float getMoonBrightness() const;
    void setMoonDiscIntensity(float intensity);
    float getMoonDiscIntensity() const;
    void setMoonEarthshine(float earthshine);
    float getMoonEarthshine() const;

    void setEclipseEnabled(bool enabled);
    bool isEclipseEnabled() const;
    void setEclipseAmount(float amount);
    float getEclipseAmount() const;

    // IDebugControl implementation (Hi-Z culling)
    void setHiZCullingEnabled(bool enabled) override;
    bool isHiZCullingEnabled() const override;

    // Note: CullingStats defined in IDebugControl, we use that
    IDebugControl::CullingStats getHiZCullingStats() const override;
    uint32_t getVisibleObjectCount() const;

    // IProfilerControl implementation
    Profiler& getProfiler() override;
    const Profiler& getProfiler() const override;
    void setProfilingEnabled(bool enabled);
    bool isProfilingEnabled() const;

    // Resource access
    VkCommandPool getCommandPool() const { return commandPool.get(); }
    DescriptorManager::Pool* getDescriptorPool();
    std::string getShaderPath() const { return resourcePath + "/shaders"; }
    const std::string& getResourcePath() const { return resourcePath; }

    // IDebugControl implementation (physics debug)
    DebugLineSystem& getDebugLineSystem() override;
    const DebugLineSystem& getDebugLineSystem() const override;
    void setPhysicsDebugEnabled(bool enabled) override { physicsDebugEnabled = enabled; }
    bool isPhysicsDebugEnabled() const override { return physicsDebugEnabled; }

    // IPerformanceControl implementation
    PerformanceToggles& getPerformanceToggles() override { return perfToggles; }
    const PerformanceToggles& getPerformanceToggles() const override { return perfToggles; }
    void syncPerformanceToggles() override;
#ifdef JPH_DEBUG_RENDERER
    PhysicsDebugRenderer* getPhysicsDebugRenderer() override;
    const PhysicsDebugRenderer* getPhysicsDebugRenderer() const override;

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
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime);
    void recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime);
    void recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex);

    // Setup render pipeline stages with lambdas (called once during init)
    void setupRenderPipeline();

    // Pure calculation helpers (no state mutation)
    glm::vec2 calculateSunScreenPos(const Camera& camera, const glm::vec3& sunDir) const;

    // Build per-frame shared state from camera and timing
    FrameData buildFrameData(const Camera& camera, float deltaTime, float time) const;

    // Build render resources snapshot for pipeline stages
    RenderResources buildRenderResources(uint32_t swapchainImageIndex) const;

    SDL_Window* window = nullptr;
    std::string resourcePath;
    Config config_;  // Renderer configuration

    VulkanContext vulkanContext;

    // All rendering subsystems - managed with automatic lifecycle
    std::unique_ptr<RendererSystems> systems_;

    ManagedRenderPass renderPass;
    ManagedDescriptorSetLayout descriptorSetLayout;
    ManagedPipelineLayout pipelineLayout;
    ManagedPipeline graphicsPipeline;

    bool physicsDebugEnabled = false;
    glm::mat4 lastViewProj{1.0f};  // Cached view-projection for debug rendering
    bool useVolumetricSnow = true;  // Use new volumetric system by default

    // Performance toggles for debugging
    PerformanceToggles perfToggles;

    // Render pipeline (stages abstraction - for future refactoring)
    RenderPipeline renderPipeline;

    std::vector<ManagedFramebuffer> framebuffers;
    ManagedCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    ManagedImage depthImage;
    ManagedImageView depthImageView;
    ManagedSampler depthSampler;  // For Hi-Z pyramid generation
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    std::optional<DescriptorManager::Pool> descriptorManagerPool;

    std::vector<ManagedSemaphore> imageAvailableSemaphores;
    std::vector<ManagedSemaphore> renderFinishedSemaphores;
    std::vector<ManagedFence> inFlightFences;

    // Rock descriptor sets (RockSystem has its own textures, not in MaterialRegistry)
    std::vector<VkDescriptorSet> rockDescriptorSets;

    // Detritus descriptor sets (DetritusSystem has its own bark textures)
    std::vector<VkDescriptorSet> detritusDescriptorSets;

    // Tree descriptor sets per texture type (keyed by type name string)
    // Each map has entries for each frame: map[typeName][frameIndex]
    std::unordered_map<std::string, std::vector<VkDescriptorSet>> treeBarkDescriptorSets;
    std::unordered_map<std::string, std::vector<VkDescriptorSet>> treeLeafDescriptorSets;

    uint32_t currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 3;

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

    // Player position for grass displacement
    glm::vec3 playerPosition = glm::vec3(0.0f);
    glm::vec3 playerVelocity = glm::vec3(0.0f);
    float playerCapsuleRadius = 0.3f;      // Default capsule radius

    // Dynamic lights
    float lightCullRadius = 100.0f;        // Radius from camera for light culling

    // GUI rendering callback
    GuiRenderCallback guiRenderCallback;

    void updateLightBuffer(uint32_t currentImage, const Camera& camera);

    // Skinned mesh rendering
    bool initSkinnedMeshRenderer();
    bool createSkinnedMeshRendererDescriptorSets();

    // Hi-Z occlusion culling
    void updateHiZObjectData();
};
