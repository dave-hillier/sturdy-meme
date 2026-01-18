#pragma once

#include "VulkanServices.h"

// Include system headers for their InitInfo types
#include "WindSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "SkySystem.h"
#include "FroxelSystem.h"
#include "ShadowSystem.h"
#include "HiZSystem.h"
#include "GrassSystem.h"

/**
 * InitHelpers - Convert VulkanServices to system-specific InitInfo structs
 *
 * This allows gradual migration: systems keep their existing InitInfo pattern,
 * but callers can use VulkanServices to reduce boilerplate.
 *
 * Before (60+ lines of repetition):
 *   WindSystem::InitInfo windInfo;
 *   windInfo.device = ctx.device;
 *   windInfo.allocator = ctx.allocator;
 *   windInfo.framesInFlight = ctx.framesInFlight;
 *   auto wind = WindSystem::create(windInfo);
 *
 *   BloomSystem::InitInfo bloomInfo;
 *   bloomInfo.device = ctx.device;
 *   bloomInfo.allocator = ctx.allocator;
 *   bloomInfo.descriptorPool = ctx.descriptorPool;
 *   bloomInfo.extent = ctx.extent;
 *   bloomInfo.shaderPath = ctx.shaderPath;
 *   bloomInfo.raiiDevice = ctx.raiiDevice;
 *   auto bloom = BloomSystem::create(bloomInfo);
 *
 * After (2 lines each):
 *   auto wind = WindSystem::create(InitHelpers::toWindInfo(services));
 *   auto bloom = BloomSystem::create(InitHelpers::toBloomInfo(services));
 */
namespace InitHelpers {

// ============================================================================
// Simple systems (device + allocator + framesInFlight)
// ============================================================================

inline WindSystem::InitInfo toWindInfo(const VulkanServices& s) {
    return WindSystem::InitInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .framesInFlight = s.framesInFlight()
    };
}

// ============================================================================
// Post-processing systems
// ============================================================================

inline BloomSystem::InitInfo toBloomInfo(const VulkanServices& s) {
    return BloomSystem::InitInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .descriptorPool = s.descriptorPool(),
        .extent = s.extent(),
        .shaderPath = s.shaderPath(),
        .raiiDevice = s.raiiDevice()
    };
}

inline BilateralGridSystem::InitInfo toBilateralGridInfo(const VulkanServices& s) {
    return BilateralGridSystem::InitInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .descriptorPool = s.descriptorPool(),
        .extent = s.extent(),
        .shaderPath = s.shaderPath(),
        .framesInFlight = s.framesInFlight(),
        .raiiDevice = s.raiiDevice()
    };
}

inline HiZSystem::InitInfo toHiZInfo(const VulkanServices& s, VkFormat depthFormat) {
    return HiZSystem::InitInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .descriptorPool = s.descriptorPool(),
        .extent = s.extent(),
        .shaderPath = s.shaderPath(),
        .framesInFlight = s.framesInFlight(),
        .depthFormat = depthFormat,
        .raiiDevice = s.raiiDevice()
    };
}

// ============================================================================
// Atmosphere systems
// ============================================================================

inline SkySystem::InitInfo toSkyInfo(const VulkanServices& s, VkRenderPass hdrRenderPass) {
    return SkySystem::InitInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .descriptorPool = s.descriptorPool(),
        .shaderPath = s.shaderPath(),
        .framesInFlight = s.framesInFlight(),
        .extent = s.extent(),
        .hdrRenderPass = hdrRenderPass,
        .raiiDevice = s.raiiDevice()
    };
}

inline FroxelSystem::InitInfo toFroxelInfo(const VulkanServices& s) {
    return FroxelSystem::InitInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .descriptorPool = s.descriptorPool(),
        .extent = s.extent(),
        .shaderPath = s.shaderPath(),
        .framesInFlight = s.framesInFlight(),
        .raiiDevice = s.raiiDevice()
    };
}

// ============================================================================
// Lighting systems
// ============================================================================

inline ShadowSystem::InitInfo toShadowInfo(const VulkanServices& s) {
    return ShadowSystem::InitInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .descriptorPool = s.descriptorPool(),
        .shaderPath = s.shaderPath(),
        .raiiDevice = s.raiiDevice()
    };
}

// ============================================================================
// Vegetation systems (need extra params)
// ============================================================================

inline GrassSystem::InitInfo toGrassInfo(
    const VulkanServices& s,
    vk::RenderPass renderPass,
    vk::RenderPass shadowRenderPass,
    uint32_t shadowMapSize
) {
    return GrassSystem::InitInfo{
        .device = s.vkDevice(),
        .allocator = s.allocator(),
        .renderPass = renderPass,
        .descriptorPool = s.descriptorPool(),
        .extent = s.vkExtent(),
        .shaderPath = s.shaderPath(),
        .framesInFlight = s.framesInFlight(),
        .raiiDevice = s.raiiDevice(),
        .shadowRenderPass = shadowRenderPass,
        .shadowMapSize = shadowMapSize
    };
}

} // namespace InitHelpers
