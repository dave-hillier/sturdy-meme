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
#include "ErosionSimulator.h"
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
#include "HiZSystem.h"
#include "FrameData.h"
#include "RenderContext.h"
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

// PBR texture flags - indicates which optional PBR textures are bound
// Must match definitions in push_constants_common.glsl
constexpr uint32_t PBR_HAS_ROUGHNESS_MAP = (1u << 0);
constexpr uint32_t PBR_HAS_METALLIC_MAP  = (1u << 1);
constexpr uint32_t PBR_HAS_AO_MAP        = (1u << 2);
constexpr uint32_t PBR_HAS_HEIGHT_MAP    = (1u << 3);

struct PushConstants {
    glm::mat4 model;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float opacity;  // For camera occlusion fading (1.0 = fully visible)
    glm::vec4 emissiveColor;  // rgb = color, a unused
    uint32_t pbrFlags;  // Bitmask indicating which PBR textures are bound
    float _padding1;
    float _padding2;
    float _padding3;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    bool init(SDL_Window* window, const std::string& resourcePath);
    void shutdown();

    void render(const Camera& camera);
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

    void setTimeScale(float scale) { timeScale = scale; }
    float getTimeScale() const { return timeScale; }
    void setTimeOfDay(float time) { manualTime = time; useManualTime = true; }
    void resumeAutoTime() { useManualTime = false; }
    float getTimeOfDay() const { return currentTimeOfDay; }

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

    // Terrain control
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

    // Access to systems for simulation
    WindSystem& getWindSystem() { return windSystem; }
    const WindSystem& getWindSystem() const { return windSystem; }
    WaterSystem& getWaterSystem() { return waterSystem; }
    const WaterSystem& getWaterSystem() const { return waterSystem; }
    WaterTileCull& getWaterTileCull() { return waterTileCull; }
    const WaterTileCull& getWaterTileCull() const { return waterTileCull; }
    const WaterPlacementData& getWaterPlacementData() const { return erosionSimulator.getWaterData(); }
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
    void setDate(int year, int month, int day) { currentYear = year; currentMonth = month; currentDay = day; }
    int getCurrentYear() const { return currentYear; }
    int getCurrentMonth() const { return currentMonth; }
    int getCurrentDay() const { return currentDay; }
    const CelestialCalculator& getCelestialCalculator() const { return celestialCalculator; }

    // Moon phase override controls
    void setMoonPhaseOverride(bool enabled) { useMoonPhaseOverride = enabled; }
    bool isMoonPhaseOverrideEnabled() const { return useMoonPhaseOverride; }
    void setMoonPhase(float phase) { manualMoonPhase = glm::clamp(phase, 0.0f, 1.0f); }
    float getMoonPhase() const { return manualMoonPhase; }
    float getCurrentMoonPhase() const { return currentMoonPhase; }  // Actual phase (auto or manual)

    // Moon brightness controls
    void setMoonBrightness(float brightness) { moonBrightness = glm::clamp(brightness, 0.0f, 5.0f); }
    float getMoonBrightness() const { return moonBrightness; }
    void setMoonDiscIntensity(float intensity) { moonDiscIntensity = glm::clamp(intensity, 0.0f, 50.0f); }
    float getMoonDiscIntensity() const { return moonDiscIntensity; }
    void setMoonEarthshine(float earthshine) { moonEarthshine = glm::clamp(earthshine, 0.0f, 0.2f); }
    float getMoonEarthshine() const { return moonEarthshine; }

    // Eclipse controls
    void setEclipseEnabled(bool enabled) { eclipseEnabled = enabled; }
    bool isEclipseEnabled() const { return eclipseEnabled; }
    void setEclipseAmount(float amount) { eclipseAmount = glm::clamp(amount, 0.0f, 1.0f); }
    float getEclipseAmount() const { return eclipseAmount; }

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

private:
    bool createRenderPass();
    void destroyRenderResources();
    void destroyDepthImageAndView();  // Helper for resize (keeps sampler)
    void destroyFramebuffers();       // Helper for resize
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createDescriptorSetLayout();
    void addCommonDescriptorBindings(DescriptorManager::LayoutBuilder& builder);
    bool createGraphicsPipeline();
    bool createUniformBuffers();
    bool createDescriptorPool();
    bool createDescriptorSets();
    bool createDepthResources();


    void updateUniformBuffer(uint32_t currentImage, const Camera& camera);

    // Render pass recording helpers (pure - only record commands, no state mutation)
    void recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime);
    void recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime);
    void recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex);

    // Pure calculation helpers (no state mutation)
    struct LightingParams {
        glm::vec3 sunDir;
        glm::vec3 moonDir;
        float sunIntensity;
        float moonIntensity;
        glm::vec3 sunColor;
        glm::vec3 moonColor;
        glm::vec3 ambientColor;
        float moonPhase;       // Moon phase (0 = new moon, 0.5 = full moon, 1 = new moon)
        float eclipseAmount;   // Eclipse amount (0 = none, 1 = total solar eclipse)
        double julianDay;
    };
    LightingParams calculateLightingParams(float timeOfDay) const;
    UniformBufferObject buildUniformBufferData(const Camera& camera, const LightingParams& lighting, float timeOfDay) const;
    SnowUBO buildSnowUBOData() const;
    CloudShadowUBO buildCloudShadowUBOData() const;
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
    VkDescriptorSetLayout skinnedDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout skinnedPipelineLayout = VK_NULL_HANDLE;
    VkPipeline skinnedGraphicsPipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> skinnedDescriptorSets;
    std::vector<VkBuffer> boneMatricesBuffers;
    std::vector<VmaAllocation> boneMatricesAllocations;
    std::vector<void*> boneMatricesMapped;

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
    ErosionSimulator erosionSimulator;
    TreeEditSystem treeEditSystem;
    EnvironmentSettings environmentSettings;
    Profiler profiler;
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

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformBuffersAllocations;
    std::vector<void*> uniformBuffersMapped;

    // Snow UBO buffers (binding 14)
    std::vector<VkBuffer> snowBuffers;
    std::vector<VmaAllocation> snowBuffersAllocations;
    std::vector<void*> snowBuffersMapped;

    // Cloud shadow UBO buffers (binding 15)
    std::vector<VkBuffer> cloudShadowBuffers;
    std::vector<VmaAllocation> cloudShadowBuffersAllocations;
    std::vector<void*> cloudShadowBuffersMapped;

    // Light buffer SSBO (per frame)
    std::vector<VkBuffer> lightBuffers;
    std::vector<VmaAllocation> lightBufferAllocations;
    std::vector<void*> lightBuffersMapped;

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

    float timeScale = 1.0f;
    float manualTime = 0.5f;  // Noon
    bool useManualTime = true;  // Start with manual time
    float currentTimeOfDay = 0.5f;  // Noon
    float lastSunIntensity = 1.0f;

    // Celestial calculations
    CelestialCalculator celestialCalculator;
    int currentYear = 2024;
    int currentMonth = 6;
    int currentDay = 21;  // Summer solstice by default

    // Moon phase override
    bool useMoonPhaseOverride = false;
    float manualMoonPhase = 0.5f;  // Default to full moon (0=new, 0.5=full, 1=new)
    mutable float currentMoonPhase = 0.5f; // Tracks current effective phase (mutable for const functions)

    // Moon brightness controls
    float moonBrightness = 1.0f;      // Multiplier for moon light intensity (0-5)
    float moonDiscIntensity = 20.0f;  // Visual disc intensity in sky (0-50, default was 25, now 20)
    float moonEarthshine = 0.02f;     // Earthshine on dark side (0-0.2, default 2%)

    // Eclipse simulation
    bool eclipseEnabled = false;
    float eclipseAmount = 0.0f;    // 0 = no eclipse, 1 = total eclipse

    bool showCascadeDebug = false;         // true = show cascade colors overlay
    bool showSnowDepthDebug = false;       // true = show snow depth heat map overlay
    bool useParaboloidClouds = true;       // true = paraboloid LUT hybrid, false = procedural
    bool hdrEnabled = true;                // true = HDR tonemapping/bloom, false = bypass

    // Cloud parameters (synced to UBO, cloud shadows, and cloud map LUT)
    float cloudCoverage = 0.5f;            // 0-1 cloud coverage amount
    float cloudDensity = 0.3f;             // Base density multiplier
    bool framebufferResized = false;       // true = window resized, need to recreate swapchain

    // Player position for grass displacement
    glm::vec3 playerPosition = glm::vec3(0.0f);
    float playerCapsuleRadius = 0.3f;      // Default capsule radius

    // Dynamic lights
    float lightCullRadius = 100.0f;        // Radius from camera for light culling

    // GUI rendering callback
    GuiRenderCallback guiRenderCallback;

    bool createLightBuffers();
    void updateLightBuffer(uint32_t currentImage, const Camera& camera);

    // Skinned mesh rendering
    bool createSkinnedDescriptorSetLayout();
    bool createSkinnedGraphicsPipeline();
    bool createBoneMatricesBuffers();
    bool createSkinnedDescriptorSets();
    void updateBoneMatrices(uint32_t currentImage);
    void recordSkinnedCharacter(VkCommandBuffer cmd, uint32_t frameIndex);

    // Hi-Z occlusion culling
    void updateHiZObjectData();
};
