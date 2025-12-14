#pragma once

#include "InitContext.h"
#include "VulkanContext.h"
#include "DescriptorManager.h"

#include <string>

/**
 * RendererInit - Helper class for building InitContext and managing subsystem initialization
 *
 * This centralizes the creation of InitContext and provides utilities for
 * initializing subsystems with consistent resource wiring.
 */
class RendererInit {
public:
    /**
     * Build an InitContext from VulkanContext and common resources.
     * This is the single source of truth for creating the shared init context.
     */
    static InitContext buildContext(
        const VulkanContext& vulkanContext,
        VkCommandPool commandPool,
        DescriptorManager::Pool* descriptorPool,
        const std::string& resourcePath,
        uint32_t framesInFlight
    ) {
        InitContext ctx{};
        ctx.device = vulkanContext.getDevice();
        ctx.physicalDevice = vulkanContext.getPhysicalDevice();
        ctx.allocator = vulkanContext.getAllocator();
        ctx.graphicsQueue = vulkanContext.getGraphicsQueue();
        ctx.commandPool = commandPool;
        ctx.descriptorPool = descriptorPool;
        ctx.shaderPath = resourcePath + "/shaders";
        ctx.resourcePath = resourcePath;
        ctx.framesInFlight = framesInFlight;
        ctx.extent = vulkanContext.getSwapchainExtent();
        return ctx;
    }

    /**
     * Update extent in an existing InitContext (e.g., after resize)
     */
    static void updateExtent(InitContext& ctx, VkExtent2D newExtent) {
        ctx.extent = newExtent;
    }

    /**
     * Create a modified InitContext with different extent (for systems that need different resolution)
     */
    static InitContext withExtent(const InitContext& ctx, VkExtent2D newExtent) {
        InitContext modified = ctx;
        modified.extent = newExtent;
        return modified;
    }

    /**
     * Create a modified InitContext with different shader path (rare, for testing)
     */
    static InitContext withShaderPath(const InitContext& ctx, const std::string& shaderPath) {
        InitContext modified = ctx;
        modified.shaderPath = shaderPath;
        return modified;
    }
};
