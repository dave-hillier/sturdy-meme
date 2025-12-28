#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <vector>
#include <memory>
#include "PipelineCache.h"

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

    void waitIdle();

    // Getters for Vulkan handles
    VkInstance getInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkQueue getPresentQueue() const { return presentQueue; }
    uint32_t getGraphicsQueueFamily() const;
    uint32_t getPresentQueueFamily() const;
    VmaAllocator getAllocator() const { return allocator; }
    VkPipelineCache getPipelineCache() const { return pipelineCache.getCache(); }

    // RAII device access for vulkan-hpp raii types
    const vk::raii::Device& getRaiiDevice() const { return *raiiDevice_; }

    VkSwapchainKHR getSwapchain() const { return swapchain; }
    const std::vector<VkImageView>& getSwapchainImageViews() const { return swapchainImageViews; }
    VkFormat getSwapchainImageFormat() const { return swapchainImageFormat; }
    VkExtent2D getSwapchainExtent() const { return swapchainExtent; }
    uint32_t getSwapchainImageCount() const { return static_cast<uint32_t>(swapchainImages.size()); }
    uint32_t getWidth() const { return swapchainExtent.width; }
    uint32_t getHeight() const { return swapchainExtent.height; }

    const vkb::Device& getVkbDevice() const { return vkbDevice; }

    // Check if instance phase is complete (for two-phase init)
    bool isInstanceReady() const { return instanceReady; }

private:
    bool createInstance();
    bool createSurface();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createPipelineCache();

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
};
