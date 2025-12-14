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

#include "Camera.h"
#include "GrassSystem.h"
#include "CelestialCalculator.h"
#include "WindSystem.h"
#include "WeatherSystem.h"
#include "LeafSystem.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "SkySystem.h"
#include "SceneManager.h"
#include "TerrainSystem.h"
#include "ErosionDataLoader.h"
#include "CatmullClarkSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "EnvironmentSettings.h"
#include "DescriptorManager.h"
#include "ShadowSystem.h"
#include "VulkanContext.h"
#include "UBOs.h"
#include "RockSystem.h"
#include "CloudShadowSystem.h"
#include "SkinnedMesh.h"
#include "SkinnedMeshRenderer.h"
#include "HiZSystem.h"
#include "FrameData.h"
#include "RenderContext.h"
#include "TimeSystem.h"
#include "RenderPipeline.h"
#include "GlobalBufferManager.h"
#include "Profiler.h"
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "TreeEditSystem.h"
#include "SSRSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "DebugLineSystem.h"
#include "UBOBuilder.h"
#include "ResizeCoordinator.h"

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

// PBR texture flags - indicates which optional PBR textures are bound
// Must match definitions in push_constants_common.glsl
constexpr uint32_t PBR_HAS_ROUGHNESS_MAP = (1u << 0);
constexpr uint32_t PBR_HAS_METALLIC_MAP  = (1u << 1);
constexpr uint32_t PBR_HAS_AO_MAP        = (1u << 2);
constexpr uint32_t PBR_HAS_HEIGHT_MAP    = (1u << 3);


class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    bool init(SDL_Window* window, const std::string& resourcePath);
    void shutdown();

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
    VkRenderPass getSwapchainRenderPass() const { return renderPass; }
    uint32_t getSwapchainImageCount() const { return vulkanContext.getSwapchainImageCount(); }

    // Access to VulkanContext
    VulkanContext& getVulkanContext() { return vulkanContext; }
    const VulkanContext& getVulkanContext() const { return vulkanContext; }

    // GUI rendering callback (called during swapchain render pass)
    using GuiRenderCallback = std::function<void(VkCommandBuffer)>;
    void setGuiRenderCallback(GuiRenderCallback callback) { guiRenderCallback = callback; }

    void setTimeScale(float scale) { timeSystem.setTimeScale(scale); }
    float getTimeScale() const { return timeSystem.getTimeScale(); }
    void setTimeOfDay(float time) { timeSystem.setTimeOfDay(time); }
    void resumeAutoTime() { timeSystem.resumeAutoTime(); }
    float getTimeOfDay() const { return timeSystem.getTimeOfDay(); }
    TimeSystem& getTimeSystem() { return timeSystem; }
    const TimeSystem& getTimeSystem() const { return timeSystem; }

    void toggleCascadeDebug() { showCascadeDebug = !showCascadeDebug; }
    bool isShowingCascadeDebug() const { return showCascadeDebug; }

    void toggleSnowDepthDebug() { showSnowDepthDebug = !showSnowDepthDebug; }
    bool isShowingSnowDepthDebug() const { return showSnowDepthDebug; }

    // Cloud style toggle (procedural vs paraboloid LUT hybrid)
    void toggleCloudStyle() { useParaboloidClouds = !useParaboloidClouds; }
    bool isUsingParaboloidClouds() const { return useParaboloidClouds; }

    // Cloud coverage and density (synced to sky shader, cloud shadows, and cloud map LUT)
    void setCloudCoverage(float coverage) {
        cloudCoverage = glm::clamp(coverage, 0.0f, 1.0f);
        cloudShadowSystem.setCloudCoverage(cloudCoverage);
        atmosphereLUTSystem.setCloudCoverage(cloudCoverage);
    }
    float getCloudCoverage() const { return cloudCoverage; }

    void setCloudDensity(float density) {
        cloudDensity = glm::clamp(density, 0.0f, 1.0f);
        cloudShadowSystem.setCloudDensity(cloudDensity);
        atmosphereLUTSystem.setCloudDensity(cloudDensity);
    }
    float getCloudDensity() const { return cloudDensity; }

    // Cloud shadow control
    void setCloudShadowEnabled(bool enabled) { cloudShadowSystem.setEnabled(enabled); }
    bool isCloudShadowEnabled() const { return cloudShadowSystem.isEnabled(); }

    // HDR/Post-processing control
    void setHDREnabled(bool enabled) { hdrEnabled = enabled; }
    bool isHDREnabled() const { return hdrEnabled; }
    void setCloudShadowIntensity(float intensity) { cloudShadowSystem.setShadowIntensity(intensity); }
    float getCloudShadowIntensity() const { return cloudShadowSystem.getShadowIntensity(); }

    // God ray quality control
    void setGodRaysEnabled(bool enabled) { postProcessSystem.setGodRaysEnabled(enabled); }
    bool isGodRaysEnabled() const { return postProcessSystem.isGodRaysEnabled(); }
    void setGodRayQuality(PostProcessSystem::GodRayQuality quality) { postProcessSystem.setGodRayQuality(quality); }
    PostProcessSystem::GodRayQuality getGodRayQuality() const { return postProcessSystem.getGodRayQuality(); }

    // Froxel volumetric fog quality control
    void setFroxelFilterQuality(bool highQuality) { postProcessSystem.setFroxelFilterQuality(highQuality); }
    bool isFroxelFilterHighQuality() const { return postProcessSystem.isFroxelFilterHighQuality(); }

    // Terrain control
    void setTerrainEnabled(bool enabled) { terrainEnabled = enabled; }
    bool isTerrainEnabled() const { return terrainEnabled; }
    void toggleTerrainWireframe() { terrainSystem.setWireframeMode(!terrainSystem.isWireframeMode()); }
    bool isTerrainWireframeMode() const { return terrainSystem.isWireframeMode(); }
    float getTerrainHeightAt(float x, float z) const { return terrainSystem.getHeightAt(x, z); }
    uint32_t getTerrainNodeCount() const { return terrainSystem.getNodeCount(); }

    // Terrain data access for physics integration
    const TerrainSystem& getTerrainSystem() const { return terrainSystem; }
    TerrainSystem& getTerrainSystem() { return terrainSystem; }

    // Catmull-Clark subdivision control
    void toggleCatmullClarkWireframe() { catmullClarkSystem.setWireframeMode(!catmullClarkSystem.isWireframeMode()); }
    bool isCatmullClarkWireframeMode() const { return catmullClarkSystem.isWireframeMode(); }
    CatmullClarkSystem& getCatmullClarkSystem() { return catmullClarkSystem; }

    // Weather control
    void setWeatherIntensity(float intensity);
    void setWeatherType(uint32_t type);
    uint32_t getWeatherType() const { return weatherSystem.getWeatherType(); }
    float getIntensity() const { return weatherSystem.getIntensity(); }

    // Fog control - Froxel volumetric fog
    void setFogDensity(float density) { froxelSystem.setFogDensity(density); }
    float getFogDensity() const { return froxelSystem.getFogDensity(); }
    void setFogEnabled(bool enabled) { froxelSystem.setEnabled(enabled); postProcessSystem.setFroxelEnabled(enabled); }
    bool isFogEnabled() const { return froxelSystem.isEnabled(); }

    // Froxel fog extended parameters
    void setFogBaseHeight(float h) { froxelSystem.setFogBaseHeight(h); }
    float getFogBaseHeight() const { return froxelSystem.getFogBaseHeight(); }
    void setFogScaleHeight(float h) { froxelSystem.setFogScaleHeight(h); }
    float getFogScaleHeight() const { return froxelSystem.getFogScaleHeight(); }
    void setFogAbsorption(float a) { froxelSystem.setFogAbsorption(a); }
    float getFogAbsorption() const { return froxelSystem.getFogAbsorption(); }
    void setVolumetricFarPlane(float f) { froxelSystem.setVolumetricFarPlane(f); postProcessSystem.setFroxelParams(f, FroxelSystem::DEPTH_DISTRIBUTION); }
    float getVolumetricFarPlane() const { return froxelSystem.getVolumetricFarPlane(); }
    void setTemporalBlend(float b) { froxelSystem.setTemporalBlend(b); }
    float getTemporalBlend() const { return froxelSystem.getTemporalBlend(); }

    // Height fog layer parameters
    void setLayerHeight(float h) { froxelSystem.setLayerHeight(h); }
    float getLayerHeight() const { return froxelSystem.getLayerHeight(); }
    void setLayerThickness(float t) { froxelSystem.setLayerThickness(t); }
    float getLayerThickness() const { return froxelSystem.getLayerThickness(); }
    void setLayerDensity(float d) { froxelSystem.setLayerDensity(d); }
    float getLayerDensity() const { return froxelSystem.getLayerDensity(); }

    // Atmospheric scattering parameters
    void setAtmosphereParams(const AtmosphereParams& params) { atmosphereLUTSystem.setAtmosphereParams(params); }
    const AtmosphereParams& getAtmosphereParams() const { return atmosphereLUTSystem.getAtmosphereParams(); }
    AtmosphereLUTSystem& getAtmosphereLUTSystem() { return atmosphereLUTSystem; }

    // Leaf control
    void setLeafIntensity(float intensity) { leafSystem.setIntensity(intensity); }
    float getLeafIntensity() const { return leafSystem.getIntensity(); }
    void spawnConfetti(const glm::vec3& position, float velocity = 8.0f, float count = 100.0f, float coneAngle = 0.5f) {
        leafSystem.spawnConfetti(position, velocity, count, coneAngle);
    }

    // Snow control
    void setSnowAmount(float amount) { environmentSettings.snowAmount = glm::clamp(amount, 0.0f, 1.0f); }
    float getSnowAmount() const { return environmentSettings.snowAmount; }
    void setSnowColor(const glm::vec3& color) { environmentSettings.snowColor = color; }
    const glm::vec3& getSnowColor() const { return environmentSettings.snowColor; }
    void addSnowInteraction(const glm::vec3& position, float radius, float strength) {
        snowMaskSystem.addInteraction(position, radius, strength);
    }
    EnvironmentSettings& getEnvironmentSettings() { return environmentSettings; }

    // Scene access
    SceneManager& getSceneManager() { return sceneManager; }
    const SceneManager& getSceneManager() const { return sceneManager; }

    // Rock system access for physics integration
    const RockSystem& getRockSystem() const { return rockSystem; }

    // Player position for grass interaction (xyz = position, w = capsule radius)
    void setPlayerPosition(const glm::vec3& position, float radius);
    void setPlayerState(const glm::vec3& position, const glm::vec3& velocity, float radius);

    // Access to systems for simulation
    WindSystem& getWindSystem() { return windSystem; }
    const WindSystem& getWindSystem() const { return windSystem; }
    WaterSystem& getWaterSystem() { return waterSystem; }
    const WaterSystem& getWaterSystem() const { return waterSystem; }
    WaterTileCull& getWaterTileCull() { return waterTileCull; }
    const WaterTileCull& getWaterTileCull() const { return waterTileCull; }
    const WaterPlacementData& getWaterPlacementData() const { return erosionDataLoader.getWaterData(); }
    SceneBuilder& getSceneBuilder() { return sceneManager.getSceneBuilder(); }
    Mesh& getFlagClothMesh() { return sceneManager.getSceneBuilder().getFlagClothMesh(); }
    Mesh& getFlagPoleMesh() { return sceneManager.getSceneBuilder().getFlagPoleMesh(); }
    void uploadFlagClothMesh() { sceneManager.getSceneBuilder().uploadFlagClothMesh(vulkanContext.getAllocator(), vulkanContext.getDevice(), commandPool, vulkanContext.getGraphicsQueue()); }

    // Animated character update
    // movementSpeed: horizontal speed for animation state selection
    // isGrounded: whether on the ground
    // isJumping: whether just started jumping
    void updateAnimatedCharacter(float deltaTime, float movementSpeed = 0.0f, bool isGrounded = true, bool isJumping = false) {
        sceneManager.getSceneBuilder().updateAnimatedCharacter(deltaTime, vulkanContext.getAllocator(), vulkanContext.getDevice(), commandPool, vulkanContext.getGraphicsQueue(), movementSpeed, isGrounded, isJumping);
    }

    // Start a jump with trajectory prediction for animation sync
    void startCharacterJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const class PhysicsWorld* physics) {
        sceneManager.getSceneBuilder().startCharacterJump(startPos, velocity, gravity, physics);
    }

    // Celestial/astronomical settings
    void setLocation(const GeographicLocation& location) { celestialCalculator.setLocation(location); }
    const GeographicLocation& getLocation() const { return celestialCalculator.getLocation(); }
    void setDate(int year, int month, int day) { timeSystem.setDate(year, month, day); }
    int getCurrentYear() const { return timeSystem.getCurrentYear(); }
    int getCurrentMonth() const { return timeSystem.getCurrentMonth(); }
    int getCurrentDay() const { return timeSystem.getCurrentDay(); }
    const CelestialCalculator& getCelestialCalculator() const { return celestialCalculator; }

    // Moon phase override controls
    void setMoonPhaseOverride(bool enabled) { timeSystem.setMoonPhaseOverride(enabled); }
    bool isMoonPhaseOverrideEnabled() const { return timeSystem.isMoonPhaseOverrideEnabled(); }
    void setMoonPhase(float phase) { timeSystem.setMoonPhase(phase); }
    float getMoonPhase() const { return timeSystem.getMoonPhase(); }
    float getCurrentMoonPhase() const { return timeSystem.getCurrentMoonPhase(); }  // Actual phase (auto or manual)

    // Moon brightness controls
    void setMoonBrightness(float brightness) { timeSystem.setMoonBrightness(brightness); }
    float getMoonBrightness() const { return timeSystem.getMoonBrightness(); }
    void setMoonDiscIntensity(float intensity) { timeSystem.setMoonDiscIntensity(intensity); }
    float getMoonDiscIntensity() const { return timeSystem.getMoonDiscIntensity(); }
    void setMoonEarthshine(float earthshine) { timeSystem.setMoonEarthshine(earthshine); }
    float getMoonEarthshine() const { return timeSystem.getMoonEarthshine(); }

    // Eclipse controls
    void setEclipseEnabled(bool enabled) { timeSystem.setEclipseEnabled(enabled); }
    bool isEclipseEnabled() const { return timeSystem.isEclipseEnabled(); }
    void setEclipseAmount(float amount) { timeSystem.setEclipseAmount(amount); }
    float getEclipseAmount() const { return timeSystem.getEclipseAmount(); }

    // Hi-Z occlusion culling control
    void setHiZCullingEnabled(bool enabled) { hiZSystem.setHiZEnabled(enabled); }
    bool isHiZCullingEnabled() const { return hiZSystem.isHiZEnabled(); }
    HiZSystem::CullingStats getHiZCullingStats() const { return hiZSystem.getStats(); }
    uint32_t getVisibleObjectCount() const { return hiZSystem.getVisibleCount(currentFrame); }

    // Profiling access
    Profiler& getProfiler() { return profiler; }
    const Profiler& getProfiler() const { return profiler; }
    void setProfilingEnabled(bool enabled) { profiler.setEnabled(enabled); }
    bool isProfilingEnabled() const { return profiler.isEnabled(); }

    // Tree edit system access
    TreeEditSystem& getTreeEditSystem() { return treeEditSystem; }
    const TreeEditSystem& getTreeEditSystem() const { return treeEditSystem; }
    bool isTreeEditMode() const { return treeEditSystem.isEnabled(); }
    void setTreeEditMode(bool enabled) { treeEditSystem.setEnabled(enabled); }
    void toggleTreeEditMode() { treeEditSystem.toggle(); }

    // Resource access for billboard capture
    VkCommandPool getCommandPool() const { return commandPool; }
    DescriptorManager::Pool* getDescriptorPool() { return &*descriptorManagerPool; }
    std::string getShaderPath() const { return resourcePath + "/shaders"; }

    // Physics debug visualization
    DebugLineSystem& getDebugLineSystem() { return debugLineSystem; }
    const DebugLineSystem& getDebugLineSystem() const { return debugLineSystem; }
    void setPhysicsDebugEnabled(bool enabled) { physicsDebugEnabled = enabled; }
    bool isPhysicsDebugEnabled() const { return physicsDebugEnabled; }
#ifdef JPH_DEBUG_RENDERER
    PhysicsDebugRenderer* getPhysicsDebugRenderer() { return physicsDebugRenderer.get(); }
    const PhysicsDebugRenderer* getPhysicsDebugRenderer() const { return physicsDebugRenderer.get(); }

    // Update physics debug visualization (call before render)
    void updatePhysicsDebug(PhysicsWorld& physics, const glm::vec3& cameraPos);
#endif

private:
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

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    // Skinned mesh rendering (GPU skinning)
    SkinnedMeshRenderer skinnedMeshRenderer;

    SkySystem skySystem;
    GrassSystem grassSystem;
    WindSystem windSystem;
    WeatherSystem weatherSystem;
    LeafSystem leafSystem;
    PostProcessSystem postProcessSystem;
    BloomSystem bloomSystem;
    FroxelSystem froxelSystem;
    AtmosphereLUTSystem atmosphereLUTSystem;
    TerrainSystem terrainSystem;
    CatmullClarkSystem catmullClarkSystem;
    SnowMaskSystem snowMaskSystem;
    VolumetricSnowSystem volumetricSnowSystem;
    RockSystem rockSystem;
    CloudShadowSystem cloudShadowSystem;
    HiZSystem hiZSystem;
    WaterSystem waterSystem;
    WaterDisplacement waterDisplacement;
    FlowMapGenerator flowMapGenerator;
    FoamBuffer foamBuffer;
    SSRSystem ssrSystem;
    WaterTileCull waterTileCull;
    WaterGBuffer waterGBuffer;
    ErosionDataLoader erosionDataLoader;
    TreeEditSystem treeEditSystem;
    EnvironmentSettings environmentSettings;
    UBOBuilder uboBuilder;
    Profiler profiler;
    DebugLineSystem debugLineSystem;
    ResizeCoordinator resizeCoordinator;
#ifdef JPH_DEBUG_RENDERER
    std::unique_ptr<PhysicsDebugRenderer> physicsDebugRenderer;
#endif
    bool physicsDebugEnabled = false;
    glm::mat4 lastViewProj{1.0f};  // Cached view-projection for debug rendering
    bool useVolumetricSnow = true;  // Use new volumetric system by default

    // Render pipeline (stages abstraction - for future refactoring)
    RenderPipeline renderPipeline;

    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    VkImage depthImage = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkSampler depthSampler = VK_NULL_HANDLE;  // For Hi-Z pyramid generation
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    // Shadow system (CSM + dynamic shadows)
    ShadowSystem shadowSystem;

    // Global buffer manager for per-frame shared GPU buffers
    GlobalBufferManager globalBufferManager;

    std::optional<DescriptorManager::Pool> descriptorManagerPool;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    // Scene management (meshes, textures, objects, lights, physics)
    SceneManager sceneManager;
    // Rock descriptor sets (RockSystem has its own textures, not in MaterialRegistry)
    std::vector<VkDescriptorSet> rockDescriptorSets;

    uint32_t currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // Time management (day/night cycle, frame timing, moon phase, eclipse)
    TimeSystem timeSystem;
    float lastSunIntensity = 1.0f;

    // Celestial calculations
    CelestialCalculator celestialCalculator;

    bool showCascadeDebug = false;         // true = show cascade colors overlay
    bool showSnowDepthDebug = false;       // true = show snow depth heat map overlay
    bool useParaboloidClouds = true;       // true = paraboloid LUT hybrid, false = procedural
    bool hdrEnabled = true;                // true = HDR tonemapping/bloom, false = bypass
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
