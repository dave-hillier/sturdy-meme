#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <cstdint>

#include "DescriptorManager.h"

/**
 * InitContext - Common resources needed for subsystem initialization
 *
 * Bundles together the Vulkan handles, paths, and settings that nearly
 * every subsystem needs during init(). Subsystem-specific InitInfo structs
 * can reference this instead of duplicating these fields.
 *
 * This is for init-time setup. For per-frame rendering, use RenderContext.
 */
struct InitContext {
    // Core Vulkan handles (from VulkanContext)
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    // Queue for one-time command submission (uploads, etc.)
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Shared descriptor pool (auto-growing)
    DescriptorManager::Pool* descriptorPool = nullptr;

    // Paths
    std::string shaderPath;
    std::string resourcePath;

    // Frame/swapchain info
    uint32_t framesInFlight = 3;
    VkExtent2D extent = {0, 0};
};
