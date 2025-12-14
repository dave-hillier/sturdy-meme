#pragma once

#include <vulkan/vulkan.h>

// Forward declaration
class PostProcessSystem;

/**
 * HDRResources - Resources provided by PostProcessSystem
 *
 * Captures the render pass, framebuffer, and image views needed by
 * systems that render to the HDR target.
 */
struct HDRResources {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};

    bool isValid() const { return renderPass != VK_NULL_HANDLE; }

    // Collect from PostProcessSystem
    static HDRResources collect(const PostProcessSystem& postProcess);
};
