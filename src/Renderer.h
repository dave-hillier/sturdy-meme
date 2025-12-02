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

struct PushConstants {
    glm::mat4 model;
    float roughness;
    float metallic;
    float emissiveIntensity;
    float padding;
    glm::vec4 emissiveColor;  // rgb = color, a unused
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    bool init(SDL_Window* window, const std::string& resourcePath);
    void shutdown();

    void render(const Camera& camera);
    void waitIdle();

    uint32_t getWidth() const { return vulkanContext.getWidth(); }
    uint32_t getHeight() const { return vulkanContext.getHeight(); }

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

    // Cloud shadow control
    void setCloudShadowEnabled(bool enabled) { cloudShadowSystem.setEnabled(enabled); }
    bool isCloudShadowEnabled() const { return cloudShadowSystem.isEnabled(); }
    void setCloudShadowIntensity(float intensity) { cloudShadowSystem.setShadowIntensity(intensity); }
    float getCloudShadowIntensity() const { return cloudShadowSystem.getShadowIntensity(); }

    // Terrain control
    void toggleTerrainWireframe() { terrainSystem.setWireframeMode(!terrainSystem.isWireframeMode()); }
    bool isTerrainWireframeMode() const { return terrainSystem.isWireframeMode(); }
    float getTerrainHeightAt(float x, float z) const { return terrainSystem.getHeightAt(x, z); }
    uint32_t getTerrainNodeCount() const { return terrainSystem.getNodeCount(); }

    // Terrain data access for physics integration
    const TerrainSystem& getTerrainSystem() const { return terrainSystem; }

    // Catmull-Clark subdivision control
    void toggleCatmullClarkWireframe() { catmullClarkSystem.setWireframeMode(!catmullClarkSystem.isWireframeMode()); }
    bool isCatmullClarkWireframeMode() const { return catmullClarkSystem.isWireframeMode(); }
    CatmullClarkSystem& getCatmullClarkSystem() { return catmullClarkSystem; }

    // Weather control
    void setWeatherIntensity(float intensity);
    void setWeatherType(uint32_t type);
    uint32_t getWeatherType() const { return weatherSystem.getWeatherType(); }
    float getIntensity() const { return weatherSystem.getIntensity(); }

    // Fog control
    void setFogDensity(float density) { froxelSystem.setFogDensity(density); }
    float getFogDensity() const { return froxelSystem.getFogDensity(); }
    void setFogEnabled(bool enabled) { froxelSystem.setEnabled(enabled); postProcessSystem.setFroxelEnabled(enabled); }
    bool isFogEnabled() const { return froxelSystem.isEnabled(); }

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

    // Celestial/astronomical settings
    void setLocation(const GeographicLocation& location) { celestialCalculator.setLocation(location); }
    const GeographicLocation& getLocation() const { return celestialCalculator.getLocation(); }
    void setDate(int year, int month, int day) { currentYear = year; currentMonth = month; currentDay = day; }
    int getCurrentYear() const { return currentYear; }
    int getCurrentMonth() const { return currentMonth; }
    int getCurrentDay() const { return currentDay; }
    const CelestialCalculator& getCelestialCalculator() const { return celestialCalculator; }

private:
    bool createRenderPass();
    void destroyRenderResources();
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createDescriptorSetLayout();
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
        double julianDay;
    };
    LightingParams calculateLightingParams(float timeOfDay) const;
    UniformBufferObject buildUniformBufferData(const Camera& camera, const LightingParams& lighting, float timeOfDay) const;
    glm::vec2 calculateSunScreenPos(const Camera& camera, const glm::vec3& sunDir) const;

    SDL_Window* window = nullptr;
    std::string resourcePath;

    VulkanContext vulkanContext;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

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
    EnvironmentSettings environmentSettings;
    bool useVolumetricSnow = true;  // Use new volumetric system by default

    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    VkImage depthImage = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    // Shadow system (CSM + dynamic shadows)
    ShadowSystem shadowSystem;

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformBuffersAllocations;
    std::vector<void*> uniformBuffersMapped;

    // Light buffer SSBO (per frame)
    std::vector<VkBuffer> lightBuffers;
    std::vector<VmaAllocation> lightBufferAllocations;
    std::vector<void*> lightBuffersMapped;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;  // Legacy pool (for systems not yet migrated)
    std::optional<DescriptorManager::Pool> descriptorManagerPool;
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<VkDescriptorSet> groundDescriptorSets;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    // Scene management (meshes, textures, objects, lights, physics)
    SceneManager sceneManager;
    std::vector<VkDescriptorSet> metalDescriptorSets;
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

    bool showCascadeDebug = false;         // true = show cascade colors overlay
    bool showSnowDepthDebug = false;       // true = show snow depth heat map overlay
    bool useParaboloidClouds = true;       // true = paraboloid LUT hybrid, false = procedural

    // Player position for grass displacement
    glm::vec3 playerPosition = glm::vec3(0.0f);
    float playerCapsuleRadius = 0.3f;      // Default capsule radius

    // Dynamic lights
    float lightCullRadius = 100.0f;        // Radius from camera for light culling

    // GUI rendering callback
    GuiRenderCallback guiRenderCallback;

    bool createLightBuffers();
    void updateLightBuffer(uint32_t currentImage, const Camera& camera);
};
