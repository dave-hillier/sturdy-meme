#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "Mesh.h"
#include "Texture.h"
#include "Camera.h"
#include "GrassSystem.h"

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightSpaceMatrix;
    glm::vec4 sunDirection;
    glm::vec4 moonDirection;
    glm::vec4 sunColor;
    glm::vec4 ambientColor;
    glm::vec4 cameraPosition;
    float timeOfDay;
    float shadowMapSize;
    float padding[2];
};

struct ShadowPushConstants {
    glm::mat4 model;
};

struct PushConstants {
    glm::mat4 model;
    float roughness;
    float metallic;
    float padding[2];  // Align to 16 bytes
};

struct SceneObject {
    glm::mat4 transform;
    Mesh* mesh;
    Texture* texture;
    float roughness = 0.5f;
    float metallic = 0.0f;
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

    // Shadow mapping
    bool createShadowResources();
    bool createShadowRenderPass();
    bool createShadowPipeline();
    glm::mat4 calculateLightSpaceMatrix(const glm::vec3& lightDir, const Camera& camera);

    void updateUniformBuffer(uint32_t currentImage, const Camera& camera);

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

    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    VkImage depthImage = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    // Shadow map resources
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
    VkImage shadowImage = VK_NULL_HANDLE;
    VmaAllocation shadowImageAllocation = VK_NULL_HANDLE;
    VkImageView shadowImageView = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkFramebuffer shadowFramebuffer = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformBuffersAllocations;
    std::vector<void*> uniformBuffersMapped;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<VkDescriptorSet> groundDescriptorSets;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    Mesh groundMesh;
    Mesh cubeMesh;
    Mesh sphereMesh;
    Texture crateTexture;
    Texture crateNormalMap;
    Texture groundTexture;
    Texture groundNormalMap;
    Texture metalTexture;
    Texture metalNormalMap;

    std::vector<SceneObject> sceneObjects;
    std::vector<VkDescriptorSet> metalDescriptorSets;

    uint32_t currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    float timeScale = 1.0f;
    float manualTime = 0.0f;
    bool useManualTime = false;
};
