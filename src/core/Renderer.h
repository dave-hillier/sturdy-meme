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

// Forward declarations for types used in public API
class PostProcessSystem;
class TimeSystem;
class TerrainSystem;
class CatmullClarkSystem;
class WindSystem;
class WaterSystem;
class WaterTileCull;
class SceneManager;
class TreeEditSystem;
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

#ifdef JPH_DEBUG_RENDERER
class PhysicsDebugRenderer;
#endif

// PBR texture flags - indicates which optional PBR textures are bound
// Must match definitions in push_constants_common.glsl
constexpr uint32_t PBR_HAS_ROUGHNESS_MAP = (1u << 0);
constexpr uint32_t PBR_HAS_METALLIC_MAP  = (1u << 1);
constexpr uint32_t PBR_HAS_AO_MAP        = (1u << 2);
constexpr uint32_t PBR_HAS_HEIGHT_MAP    = (1u << 3);


class Renderer {
public:
    struct InitInfo {
        SDL_Window* window;
        std::string resourcePath;
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

    uint32_t getWidth() const { return vulkanContext.getWidth(); }
    uint32_t getHeight() const { return vulkanContext.getHeight(); }

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

    void setTimeScale(float scale);
    float getTimeScale() const;
    void setTimeOfDay(float time);
    void resumeAutoTime();
    float getTimeOfDay() const;
    TimeSystem& getTimeSystem();
    const TimeSystem& getTimeSystem() const;

    void toggleCascadeDebug() { showCascadeDebug = !showCascadeDebug; }
    bool isShowingCascadeDebug() const { return showCascadeDebug; }

    void toggleSnowDepthDebug() { showSnowDepthDebug = !showSnowDepthDebug; }
    bool isShowingSnowDepthDebug() const { return showSnowDepthDebug; }

    // Cloud style toggle (procedural vs paraboloid LUT hybrid)
    void toggleCloudStyle() { useParaboloidClouds = !useParaboloidClouds; }
    bool isUsingParaboloidClouds() const { return useParaboloidClouds; }

    // Cloud coverage and density (synced to sky shader, cloud shadows, and cloud map LUT)
    void setCloudCoverage(float coverage);
    float getCloudCoverage() const { return cloudCoverage; }

    void setCloudDensity(float density);
    float getCloudDensity() const { return cloudDensity; }

    // Cloud shadow control
    void setCloudShadowEnabled(bool enabled);
    bool isCloudShadowEnabled() const;

    // HDR/Post-processing control
    void setHDREnabled(bool enabled) { hdrEnabled = enabled; }
    bool isHDREnabled() const { return hdrEnabled; }

    // HDR pass control (skips entire scene rendering to HDR target)
    void setHDRPassEnabled(bool enabled) { hdrPassEnabled = enabled; }
    bool isHDRPassEnabled() const { return hdrPassEnabled; }

    // Bloom control
    void setBloomEnabled(bool enabled);
    bool isBloomEnabled() const;

    // Auto-exposure control
    void setAutoExposureEnabled(bool enabled);
    bool isAutoExposureEnabled() const;
    void setManualExposure(float ev);
    float getManualExposure() const;
    float getCurrentExposure() const;

    void setCloudShadowIntensity(float intensity);
    float getCloudShadowIntensity() const;

    // God ray quality control
    void setGodRaysEnabled(bool enabled);
    bool isGodRaysEnabled() const;
    void setGodRayQuality(int quality);  // 0=Low, 1=Medium, 2=High
    int getGodRayQuality() const;

    // Froxel volumetric fog quality control
    void setFroxelFilterQuality(bool highQuality);
    bool isFroxelFilterHighQuality() const;

    // Terrain control
    void setTerrainEnabled(bool enabled) { terrainEnabled = enabled; }
    bool isTerrainEnabled() const { return terrainEnabled; }
    void toggleTerrainWireframe();
    bool isTerrainWireframeMode() const;
    float getTerrainHeightAt(float x, float z) const;
    uint32_t getTerrainNodeCount() const;

    // Terrain data access for physics integration
    const TerrainSystem& getTerrainSystem() const;
    TerrainSystem& getTerrainSystem();

    // Catmull-Clark subdivision control
    void toggleCatmullClarkWireframe();
    bool isCatmullClarkWireframeMode() const;
    CatmullClarkSystem& getCatmullClarkSystem();

    // Weather control
    void setWeatherIntensity(float intensity);
    void setWeatherType(uint32_t type);
    uint32_t getWeatherType() const;
    float getIntensity() const;

    // Fog control - Froxel volumetric fog
    void setFogDensity(float density);
    float getFogDensity() const;
    void setFogEnabled(bool enabled);
    bool isFogEnabled() const;

    // Froxel fog extended parameters
    void setFogBaseHeight(float h);
    float getFogBaseHeight() const;
    void setFogScaleHeight(float h);
    float getFogScaleHeight() const;
    void setFogAbsorption(float a);
    float getFogAbsorption() const;
    void setVolumetricFarPlane(float f);
    float getVolumetricFarPlane() const;
    void setTemporalBlend(float b);
    float getTemporalBlend() const;

    // Height fog layer parameters
    void setLayerHeight(float h);
    float getLayerHeight() const;
    void setLayerThickness(float t);
    float getLayerThickness() const;
    void setLayerDensity(float d);
    float getLayerDensity() const;

    // Atmospheric scattering parameters
    void setAtmosphereParams(const AtmosphereParams& params);
    const AtmosphereParams& getAtmosphereParams() const;
    AtmosphereLUTSystem& getAtmosphereLUTSystem();

    // Leaf control
    void setLeafIntensity(float intensity);
    float getLeafIntensity() const;
    void spawnConfetti(const glm::vec3& position, float velocity = 8.0f, float count = 100.0f, float coneAngle = 0.5f);

    // Snow control
    void setSnowAmount(float amount);
    float getSnowAmount() const;
    void setSnowColor(const glm::vec3& color);
    const glm::vec3& getSnowColor() const;
    void addSnowInteraction(const glm::vec3& position, float radius, float strength);
    EnvironmentSettings& getEnvironmentSettings();

    // Scene access
    SceneManager& getSceneManager();
    const SceneManager& getSceneManager() const;

    // Rock system access for physics integration
    const RockSystem& getRockSystem() const;

    // Player position for grass interaction (xyz = position, w = capsule radius)
    void setPlayerPosition(const glm::vec3& position, float radius);
    void setPlayerState(const glm::vec3& position, const glm::vec3& velocity, float radius);

    // Access to systems for simulation
    WindSystem& getWindSystem();
    const WindSystem& getWindSystem() const;
    WaterSystem& getWaterSystem();
    const WaterSystem& getWaterSystem() const;
    WaterTileCull& getWaterTileCull();
    const WaterTileCull& getWaterTileCull() const;
    const WaterPlacementData& getWaterPlacementData() const;
    SceneBuilder& getSceneBuilder();
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

    // Celestial/astronomical settings
    void setLocation(const GeographicLocation& location);
    const GeographicLocation& getLocation() const;
    void setDate(int year, int month, int day);
    int getCurrentYear() const;
    int getCurrentMonth() const;
    int getCurrentDay() const;
    const CelestialCalculator& getCelestialCalculator() const;

    // Moon phase override controls
    void setMoonPhaseOverride(bool enabled);
    bool isMoonPhaseOverrideEnabled() const;
    void setMoonPhase(float phase);
    float getMoonPhase() const;
    float getCurrentMoonPhase() const;  // Actual phase (auto or manual)

    // Moon brightness controls
    void setMoonBrightness(float brightness);
    float getMoonBrightness() const;
    void setMoonDiscIntensity(float intensity);
    float getMoonDiscIntensity() const;
    void setMoonEarthshine(float earthshine);
    float getMoonEarthshine() const;

    // Eclipse controls
    void setEclipseEnabled(bool enabled);
    bool isEclipseEnabled() const;
    void setEclipseAmount(float amount);
    float getEclipseAmount() const;

    // Hi-Z occlusion culling control
    void setHiZCullingEnabled(bool enabled);
    bool isHiZCullingEnabled() const;

    // Culling stats (mirrors HiZSystem::CullingStats)
    struct CullingStats {
        uint32_t totalObjects;
        uint32_t visibleObjects;
        uint32_t frustumCulled;
        uint32_t occlusionCulled;
    };
    CullingStats getHiZCullingStats() const;
    uint32_t getVisibleObjectCount() const;

    // Profiling access
    Profiler& getProfiler();
    const Profiler& getProfiler() const;
    void setProfilingEnabled(bool enabled);
    bool isProfilingEnabled() const;

    // Tree edit system access
    TreeEditSystem& getTreeEditSystem();
    const TreeEditSystem& getTreeEditSystem() const;
    bool isTreeEditMode() const;
    void setTreeEditMode(bool enabled);
    void toggleTreeEditMode();

    // Resource access for billboard capture
    VkCommandPool getCommandPool() const { return commandPool.get(); }
    DescriptorManager::Pool* getDescriptorPool();
    std::string getShaderPath() const { return resourcePath + "/shaders"; }
    const std::string& getResourcePath() const { return resourcePath; }

    // Physics debug visualization
    DebugLineSystem& getDebugLineSystem();
    const DebugLineSystem& getDebugLineSystem() const;
    void setPhysicsDebugEnabled(bool enabled) { physicsDebugEnabled = enabled; }
    bool isPhysicsDebugEnabled() const { return physicsDebugEnabled; }

    // Performance toggles for debugging synchronization and bottlenecks
    PerformanceToggles& getPerformanceToggles() { return perfToggles; }
    const PerformanceToggles& getPerformanceToggles() const { return perfToggles; }
    void syncPerformanceToggles();  // Apply toggle state to render pipeline stages
#ifdef JPH_DEBUG_RENDERER
    PhysicsDebugRenderer* getPhysicsDebugRenderer();
    const PhysicsDebugRenderer* getPhysicsDebugRenderer() const;

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
