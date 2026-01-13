#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <string>
#include <cstdint>
#include <entt/fwd.hpp>

#include "DescriptorManager.h"

// Forward declaration to avoid circular dependency
class VulkanContext;

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
    const vk::raii::Device* raiiDevice = nullptr;
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

    // Optional pool sizes hint for systems that create their own pools
    std::optional<DescriptorPoolSizes> poolSizesHint;

    // Optional: ECS registry for systems that create entities (rocks, trees, etc.)
    entt::registry* registry = nullptr;

    // ========================================================================
    // Factory and modifier methods
    // ========================================================================

    /**
     * Build an InitContext from VulkanContext and common resources.
     * This is the preferred way to create an InitContext.
     */
    static InitContext build(
        const VulkanContext& vulkanContext,
        VkCommandPool commandPool,
        DescriptorManager::Pool* descriptorPool,
        const std::string& resourcePath,
        uint32_t framesInFlight,
        std::optional<DescriptorPoolSizes> poolSizes = std::nullopt,
        entt::registry* registry = nullptr
    );

    /**
     * Create a modified InitContext with different extent.
     * Useful for systems that need different resolution.
     */
    [[nodiscard]] InitContext withExtent(VkExtent2D newExtent) const {
        InitContext modified = *this;
        modified.extent = newExtent;
        return modified;
    }

    /**
     * Create a modified InitContext with different shader path.
     * Rare, mainly for testing.
     */
    [[nodiscard]] InitContext withShaderPath(const std::string& newShaderPath) const {
        InitContext modified = *this;
        modified.shaderPath = newShaderPath;
        return modified;
    }

    /**
     * Update extent in place (e.g., after resize).
     */
    void setExtent(VkExtent2D newExtent) {
        extent = newExtent;
    }

    /**
     * Create a modified InitContext with ECS registry.
     * Use when a subsystem needs to create ECS entities.
     */
    [[nodiscard]] InitContext withRegistry(entt::registry* reg) const {
        InitContext modified = *this;
        modified.registry = reg;
        return modified;
    }
};
