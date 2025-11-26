#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "Camera.h"
#include "GrassSystem.h"
#include "CelestialCalculator.h"
#include "WindSystem.h"
#include "WeatherSystem.h"
#include "PostProcessSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "SceneBuilder.h"
#include "Light.h"

static constexpr uint32_t NUM_SHADOW_CASCADES = 4;

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 cascadeViewProj[NUM_SHADOW_CASCADES];  // Per-cascade light matrices
    glm::vec4 cascadeSplits;                          // View-space split depths
    glm::vec4 sunDirection;
    glm::vec4 moonDirection;
    glm::vec4 sunColor;
    glm::vec4 moonColor;                              // rgb = moon color, a = unused
    glm::vec4 ambientColor;
    glm::vec4 cameraPosition;
    glm::vec4 pointLightPosition;  // xyz = position, w = intensity
    glm::vec4 pointLightColor;     // rgb = color, a = radius
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;           // 1.0 = show cascade colors
    float padding;
};

struct ShadowPushConstants {
    glm::mat4 model;
    int cascadeIndex;  // Which cascade we're rendering to
    int padding[3];    // Padding to align
};

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

    uint32_t getWidth() const { return swapchainExtent.width; }
    uint32_t getHeight() const { return swapchainExtent.height; }

    void setTimeScale(float scale) { timeScale = scale; }
    float getTimeScale() const { return timeScale; }
    void setTimeOfDay(float time) { manualTime = time; useManualTime = true; }
    void resumeAutoTime() { useManualTime = false; }
    float getTimeOfDay() const { return currentTimeOfDay; }

    void toggleCascadeDebug() { showCascadeDebug = !showCascadeDebug; }
    bool isShowingCascadeDebug() const { return showCascadeDebug; }

    // Weather control
    void setWeatherIntensity(float intensity);
    void setWeatherType(uint32_t type);
    uint32_t getWeatherType() const { return weatherSystem.getWeatherType(); }
    float getIntensity() const { return weatherSystem.getIntensity(); }

    // Player rendering
    void updatePlayerTransform(const glm::mat4& transform);

    // Scene object access for physics integration
    std::vector<SceneObject>& getSceneObjects() { return sceneBuilder.getSceneObjects(); }
    const std::vector<SceneObject>& getSceneObjects() const { return sceneBuilder.getSceneObjects(); }
    size_t getPlayerObjectIndex() const { return sceneBuilder.getPlayerObjectIndex(); }

    // Celestial/astronomical settings
    void setLocation(const GeographicLocation& location) { celestialCalculator.setLocation(location); }
    const GeographicLocation& getLocation() const { return celestialCalculator.getLocation(); }
    void setDate(int year, int month, int day) { currentYear = year; currentMonth = month; currentDay = day; }
    int getCurrentYear() const { return currentYear; }
    int getCurrentMonth() const { return currentMonth; }
    int getCurrentDay() const { return currentDay; }
    const CelestialCalculator& getCelestialCalculator() const { return celestialCalculator; }

private:
    bool createSwapchain();
    void destroySwapchain();
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createDescriptorSetLayout();
    bool createGraphicsPipeline();
    bool createSkyPipeline();
    bool createUniformBuffers();
    bool createDescriptorPool();
    bool createDescriptorSets();
    bool createDepthResources();

    // Shadow mapping (CSM)
    bool createShadowResources();
    bool createShadowRenderPass();
    bool createShadowPipeline();
    void calculateCascadeSplits(float nearClip, float farClip, float lambda, std::vector<float>& splits);
    glm::mat4 calculateCascadeMatrix(const glm::vec3& lightDir, const Camera& camera, float nearSplit, float farSplit);
    void updateCascadeMatrices(const glm::vec3& lightDir, const Camera& camera);

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
    };
    LightingParams calculateLightingParams(float timeOfDay) const;
    UniformBufferObject buildUniformBufferData(const Camera& camera, const LightingParams& lighting, float timeOfDay) const;
    glm::vec2 calculateSunScreenPos(const Camera& camera, const glm::vec3& sunDir) const;

    SDL_Window* window = nullptr;
    std::string resourcePath;

    vkb::Instance vkbInstance;
    vkb::Device vkbDevice;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    VmaAllocator allocator = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent = {0, 0};

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipeline skyPipeline = VK_NULL_HANDLE;

    GrassSystem grassSystem;
    WindSystem windSystem;
    WeatherSystem weatherSystem;
    PostProcessSystem postProcessSystem;
    FroxelSystem froxelSystem;
    AtmosphereLUTSystem atmosphereLUTSystem;

    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    VkImage depthImage = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    // Shadow map resources (CSM - texture array with NUM_SHADOW_CASCADES layers)
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
    VkImage shadowImage = VK_NULL_HANDLE;
    VmaAllocation shadowImageAllocation = VK_NULL_HANDLE;
    VkImageView shadowImageView = VK_NULL_HANDLE;  // Array view for sampling
    std::array<VkImageView, NUM_SHADOW_CASCADES> cascadeImageViews{};  // Per-layer views for rendering
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, NUM_SHADOW_CASCADES> cascadeFramebuffers{};  // Per-cascade framebuffers
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;

    // CSM cascade data
    std::vector<float> cascadeSplitDepths;
    std::array<glm::mat4, NUM_SHADOW_CASCADES> cascadeMatrices;

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformBuffersAllocations;
    std::vector<void*> uniformBuffersMapped;

    // Light buffer SSBO (per frame)
    std::vector<VkBuffer> lightBuffers;
    std::vector<VmaAllocation> lightBufferAllocations;
    std::vector<void*> lightBuffersMapped;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<VkDescriptorSet> groundDescriptorSets;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    // Scene resources (meshes, textures, objects)
    SceneBuilder sceneBuilder;
    std::vector<VkDescriptorSet> metalDescriptorSets;

    uint32_t currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    float timeScale = 1.0f;
    float manualTime = 0.0f;
    bool useManualTime = false;
    float currentTimeOfDay = 0.0f;
    float lastSunIntensity = 1.0f;

    // Celestial calculations
    CelestialCalculator celestialCalculator;
    int currentYear = 2024;
    int currentMonth = 6;
    int currentDay = 21;  // Summer solstice by default

    bool showCascadeDebug = false;         // true = show cascade colors overlay

    // Dynamic lights
    LightManager lightManager;
    float lightCullRadius = 100.0f;        // Radius from camera for light culling

    bool createLightBuffers();
    void updateLightBuffer(uint32_t currentImage, const glm::vec3& cameraPos);
    void setupSceneLights();               // Initialize default scene lights
};
