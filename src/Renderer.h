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

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 sunDirection;
    glm::vec4 moonDirection;
    glm::vec4 sunColor;
    glm::vec4 ambientColor;
    glm::vec4 cameraPosition;
    float timeOfDay;
    float padding[3];
};

struct PushConstants {
    glm::mat4 model;
};

struct GrassPushConstants {
    float time;
};

struct GrassInstance {
    glm::vec4 positionAndFacing;  // xyz = position, w = facing angle
    glm::vec4 heightHashTilt;     // x = height, y = hash, z = tilt, w = unused
};

struct SceneObject {
    glm::mat4 transform;
    Mesh* mesh;
    Texture* texture;
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

    // Grass system
    bool createGrassBuffers();
    bool createGrassComputeDescriptorSetLayout();
    bool createGrassComputePipeline();
    bool createGrassGraphicsDescriptorSetLayout();
    bool createGrassGraphicsPipeline();
    bool createGrassDescriptorSets();

    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> readFile(const std::string& filename);
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

    // Grass compute pipeline
    VkDescriptorSetLayout grassComputeDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout grassComputePipelineLayout = VK_NULL_HANDLE;
    VkPipeline grassComputePipeline = VK_NULL_HANDLE;

    // Grass graphics pipeline
    VkDescriptorSetLayout grassGraphicsDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout grassGraphicsPipelineLayout = VK_NULL_HANDLE;
    VkPipeline grassGraphicsPipeline = VK_NULL_HANDLE;

    // Grass storage buffers (per frame)
    std::vector<VkBuffer> grassInstanceBuffers;
    std::vector<VmaAllocation> grassInstanceAllocations;
    std::vector<VkBuffer> grassIndirectBuffers;
    std::vector<VmaAllocation> grassIndirectAllocations;

    // Grass descriptor sets
    std::vector<VkDescriptorSet> grassComputeDescriptorSets;
    std::vector<VkDescriptorSet> grassGraphicsDescriptorSets;

    static constexpr uint32_t MAX_GRASS_INSTANCES = 10000;

    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    VkImage depthImage = VK_NULL_HANDLE;
    VmaAllocation depthImageAllocation = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

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
    Texture crateTexture;
    Texture groundTexture;

    std::vector<SceneObject> sceneObjects;

    uint32_t currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    float timeScale = 1.0f;
    float manualTime = 0.0f;
    bool useManualTime = false;
};
