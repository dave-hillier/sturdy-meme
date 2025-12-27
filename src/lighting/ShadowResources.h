#pragma once

#include <vulkan/vulkan.hpp>
#include <cstdint>

// Forward declaration
class ShadowSystem;

/**
 * ShadowResources - Resources provided by ShadowSystem
 *
 * Captures shadow maps, render pass, and samplers needed by
 * systems that sample shadows or render to shadow maps.
 */
struct ShadowResources {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkImageView cascadeView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t mapSize = 0;

    // Per-frame shadow array views (point lights, spot lights)
    // Index by frame index
    VkImageView pointShadowViews[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSampler pointShadowSampler = VK_NULL_HANDLE;
    VkImageView spotShadowViews[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSampler spotShadowSampler = VK_NULL_HANDLE;

    bool isValid() const { return renderPass != VK_NULL_HANDLE && cascadeView != VK_NULL_HANDLE; }

    // Collect from ShadowSystem
    static ShadowResources collect(const ShadowSystem& shadow, uint32_t framesInFlight);
};
