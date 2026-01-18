#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <string>
#include <cstdint>

#include "DescriptorManager.h"

class VulkanContext;

/**
 * VulkanServices - Consolidated Vulkan resources for dependency injection
 *
 * This class holds the 7 common fields that appear in 60+ InitInfo structs:
 *   - device, allocator, descriptorPool, shaderPath, framesInFlight, extent, raiiDevice
 *
 * Instead of every system taking these 7 fields separately:
 *
 *   struct OldInitInfo {
 *       VkDevice device;
 *       VmaAllocator allocator;
 *       DescriptorManager::Pool* descriptorPool;
 *       std::string shaderPath;
 *       uint32_t framesInFlight;
 *       VkExtent2D extent;
 *       const vk::raii::Device* raiiDevice;
 *       // ... system-specific fields
 *   };
 *
 * Systems can now inject VulkanServices:
 *
 *   struct NewInitInfo {
 *       const VulkanServices& services;  // All 7 common fields
 *       VkRenderPass renderPass;         // System-specific only
 *   };
 *
 * This eliminates ~200 lines of duplicate field declarations and
 * reduces the chance of initialization errors.
 */
class VulkanServices {
public:
    // Construct from VulkanContext (the common case)
    VulkanServices(
        const VulkanContext& context,
        DescriptorManager::Pool* descriptorPool,
        const std::string& resourcePath
    );

    // Construct with explicit values (for testing/mocking)
    VulkanServices(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VmaAllocator allocator,
        VkQueue graphicsQueue,
        VkCommandPool commandPool,
        DescriptorManager::Pool* descriptorPool,
        const vk::raii::Device* raiiDevice,
        const std::string& shaderPath,
        const std::string& resourcePath,
        uint32_t framesInFlight,
        VkExtent2D extent
    );

    // Non-copyable but movable
    VulkanServices(const VulkanServices&) = delete;
    VulkanServices& operator=(const VulkanServices&) = delete;
    VulkanServices(VulkanServices&&) = default;
    VulkanServices& operator=(VulkanServices&&) = default;

    // ========================================================================
    // Core Vulkan handles (the 7 common fields)
    // ========================================================================

    VkDevice device() const { return device_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VmaAllocator allocator() const { return allocator_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkCommandPool commandPool() const { return commandPool_; }
    DescriptorManager::Pool* descriptorPool() const { return descriptorPool_; }
    const vk::raii::Device* raiiDevice() const { return raiiDevice_; }

    // ========================================================================
    // Paths
    // ========================================================================

    const std::string& shaderPath() const { return shaderPath_; }
    const std::string& resourcePath() const { return resourcePath_; }

    // ========================================================================
    // Frame/swapchain info
    // ========================================================================

    uint32_t framesInFlight() const { return framesInFlight_; }
    VkExtent2D extent() const { return extent_; }

    // ========================================================================
    // Vulkan-hpp accessors (for systems that prefer hpp types)
    // ========================================================================

    vk::Device vkDevice() const { return vk::Device(device_); }
    vk::PhysicalDevice vkPhysicalDevice() const { return vk::PhysicalDevice(physicalDevice_); }
    vk::Queue vkGraphicsQueue() const { return vk::Queue(graphicsQueue_); }
    vk::CommandPool vkCommandPool() const { return vk::CommandPool(commandPool_); }
    vk::Extent2D vkExtent() const { return vk::Extent2D{extent_.width, extent_.height}; }

    // ========================================================================
    // Mutators (for resize, etc.)
    // ========================================================================

    void setExtent(VkExtent2D newExtent) { extent_ = newExtent; }
    void setExtent(uint32_t width, uint32_t height) {
        extent_ = VkExtent2D{width, height};
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    const vk::raii::Device* raiiDevice_ = nullptr;

    std::string shaderPath_;
    std::string resourcePath_;

    uint32_t framesInFlight_ = 3;
    VkExtent2D extent_ = {0, 0};
};
