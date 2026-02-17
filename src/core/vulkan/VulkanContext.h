#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <vector>
#include <memory>
#include "PipelineCache.h"
#include "VmaImage.h"

/**
 * VulkanContext encapsulates core Vulkan setup:
 * - Instance creation
 * - Surface creation
 * - Physical device selection
 * - Logical device creation
 * - Queue retrieval
 * - VMA allocator setup
 * - Swapchain management
 */
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext() = default;

    // Non-copyable
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    /**
     * Two-phase initialization for early Vulkan startup.
     *
     * initInstance() can be called before window creation to start
     * Vulkan instance, validation layers, and dispatcher earlier.
     *
     * initDevice() completes initialization once a window is available.
     */
    bool initInstance();
    bool initDevice(SDL_Window* window);

    /**
     * Combined init for backwards compatibility.
     * Equivalent to initInstance() + initDevice(window).
     */
    bool init(SDL_Window* window);
    void shutdown();

    bool createSwapchain();
    void destroySwapchain();
    bool recreateSwapchain();

    // Clear all swapchain images to black (call after recreateSwapchain to prevent ghost frames)
    void clearSwapchainImages();

    // Swapchain-dependent resource creation (render pass, depth buffer, framebuffers)
    bool createSwapchainResources();
    void destroySwapchainResources();
    bool recreateSwapchainResources();

    // Command pool and buffers
    bool createCommandPoolAndBuffers(uint32_t frameCount);
    void destroyCommandPoolAndBuffers();

    void waitIdle();

    // RAII access for vulkan-hpp raii types (preferred for new code)
    const vk::raii::Instance& getRaiiInstance() const { return *raiiInstance_; }
    const vk::raii::PhysicalDevice& getRaiiPhysicalDevice() const { return *raiiPhysicalDevice_; }
    const vk::raii::Device& getRaiiDevice() const { return *raiiDevice_; }

    // vulkan-hpp handle getters (implicit conversion to VkXxx when needed)
    vk::Instance getVkInstance() const { return vk::Instance(instance); }
    vk::PhysicalDevice getVkPhysicalDevice() const { return vk::PhysicalDevice(physicalDevice); }
    vk::Device getVkDevice() const { return vk::Device(device); }
    vk::Queue getVkGraphicsQueue() const { return vk::Queue(graphicsQueue); }
    vk::Queue getVkPresentQueue() const { return vk::Queue(presentQueue); }
    vk::Queue getVkTransferQueue() const { return vk::Queue(transferQueue_); }
    vk::SwapchainKHR getVkSwapchain() const { return vk::SwapchainKHR(swapchain); }
    vk::Format getVkSwapchainImageFormat() const { return static_cast<vk::Format>(swapchainImageFormat); }
    vk::Extent2D getVkSwapchainExtent() const { return vk::Extent2D{swapchainExtent.width, swapchainExtent.height}; }

    uint32_t getGraphicsQueueFamily() const;
    uint32_t getPresentQueueFamily() const;
    uint32_t getTransferQueueFamily() const;
    bool hasDedicatedTransferQueue() const { return hasDedicatedTransfer_; }
    VmaAllocator getAllocator() const { return allocator; }
    VkPipelineCache getPipelineCache() const { return pipelineCache.getCache(); }
    SDL_Window* getWindow() const { return window; }

    const std::vector<VkImageView>& getSwapchainImageViews() const { return swapchainImageViews; }
    uint32_t getSwapchainImageCount() const { return static_cast<uint32_t>(swapchainImages.size()); }
    uint32_t getWidth() const { return swapchainExtent.width; }
    uint32_t getHeight() const { return swapchainExtent.height; }

    // Swapchain-dependent resource getters
    VkRenderPass getRenderPass() const { return renderPass_ ? **renderPass_ : VK_NULL_HANDLE; }
    const vk::raii::RenderPass& getRaiiRenderPass() const { return *renderPass_; }
    VkImageView getDepthImageView() const { return depthImageView_ ? **depthImageView_ : VK_NULL_HANDLE; }
    VkSampler getDepthSampler() const { return depthSampler_ ? **depthSampler_ : VK_NULL_HANDLE; }
    VkFormat getDepthFormat() const { return depthFormat_; }
    const std::vector<vk::raii::Framebuffer>& getFramebuffers() const { return framebuffers_; }
    std::vector<vk::raii::Framebuffer>& getFramebuffers() { return framebuffers_; }
    uint32_t getFramebufferCount() const { return static_cast<uint32_t>(framebuffers_.size()); }

    // Command pool/buffer getters
    VkCommandPool getCommandPool() const { return commandPool_ ? **commandPool_ : VK_NULL_HANDLE; }
    const vk::raii::CommandPool& getRaiiCommandPool() const { return *commandPool_; }
    const std::vector<VkCommandBuffer>& getCommandBuffers() const { return commandBuffers_; }
    VkCommandBuffer getCommandBuffer(uint32_t frameIndex) const {
        return frameIndex < commandBuffers_.size() ? commandBuffers_[frameIndex] : VK_NULL_HANDLE;
    }

    const vkb::Device& getVkbDevice() const { return vkbDevice; }

    // Check if validation layers are enabled (useful for diagnostics)
    bool hasValidationLayers() const { return vkbInstance.debug_messenger != VK_NULL_HANDLE; }

    // Check if instance phase is complete (for two-phase init)
    bool isInstanceReady() const { return instanceReady; }

    // Check if device phase is complete (device, surface, swapchain created)
    bool isDeviceReady() const { return device != VK_NULL_HANDLE; }

    // Check if timeline semaphores are supported (always true for Vulkan 1.2+)
    bool hasTimelineSemaphores() const { return hasTimelineSemaphores_; }

    // Check if drawIndirectCount is supported (vkCmdDrawIndexedIndirectCount)
    bool hasDrawIndirectCount() const { return hasDrawIndirectCount_; }

    // Check if shaderDrawParameters is supported (gl_DrawID)
    bool hasShaderDrawParameters() const { return hasShaderDrawParameters_; }

private:
    bool createInstance();
    bool createSurface();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createPipelineCache();

    // Internal helpers for swapchain resource creation
    bool createRenderPass();
    bool createDepthResources();
    bool createFramebuffers();
    bool recreateDepthResources();

    SDL_Window* window = nullptr;
    bool instanceReady = false;

    vkb::Instance vkbInstance;
    vkb::PhysicalDevice vkbPhysicalDevice;
    vkb::Device vkbDevice;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkQueue transferQueue_ = VK_NULL_HANDLE;
    uint32_t transferQueueFamily_ = 0;
    bool hasDedicatedTransfer_ = false;
    bool hasTimelineSemaphores_ = false;
    bool hasDrawIndirectCount_ = false;
    bool hasShaderDrawParameters_ = false;

    VmaAllocator allocator = VK_NULL_HANDLE;

    PipelineCache pipelineCache;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent = {0, 0};

    // vulkan-hpp RAII wrappers (non-owning, wrapping existing handles)
    vk::raii::Context raiiContext_;
    std::unique_ptr<vk::raii::Instance> raiiInstance_;
    std::unique_ptr<vk::raii::PhysicalDevice> raiiPhysicalDevice_;
    std::unique_ptr<vk::raii::Device> raiiDevice_;

    // Swapchain-dependent resources (render pass, depth buffer, framebuffers)
    std::optional<vk::raii::RenderPass> renderPass_;
    VmaImage depthImage_;
    std::optional<vk::raii::ImageView> depthImageView_;
    std::optional<vk::raii::Sampler> depthSampler_;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    std::vector<vk::raii::Framebuffer> framebuffers_;

    // Command pool and buffers
    std::optional<vk::raii::CommandPool> commandPool_;
    std::vector<VkCommandBuffer> commandBuffers_;
};
