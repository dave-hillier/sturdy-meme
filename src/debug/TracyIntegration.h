#pragma once

/**
 * Tracy Profiler Integration
 *
 * This header provides macros for integrating Tracy profiler into the codebase.
 * When TRACY_ENABLE is defined (via CMake), Tracy profiling is active.
 * When disabled, all macros compile to no-ops with zero overhead.
 *
 * Usage:
 *   - TRACY_FRAME_MARK: Call once per frame in the main loop
 *   - TRACY_ZONE_SCOPED: Profile the current scope
 *   - TRACY_ZONE_SCOPED_N("name"): Profile with a custom name
 *   - TRACY_ZONE_TEXT(text, len): Add text to current zone
 *   - TRACY_PLOT("name", value): Plot a value over time
 *
 * GPU Profiling (requires TracyVulkan initialization):
 *   - TracyVkContext: Create once per device/queue pair
 *   - TracyVkZone: Profile GPU work in command buffers
 *   - TracyVkCollect: Collect GPU timing data
 */

#ifdef TRACY_ENABLE

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

// Frame marking - call once per frame in main loop
#define TRACY_FRAME_MARK FrameMark

// CPU zone profiling
#define TRACY_ZONE_SCOPED ZoneScoped
#define TRACY_ZONE_SCOPED_N(name) ZoneScopedN(name)
#define TRACY_ZONE_SCOPED_C(color) ZoneScopedC(color)
#define TRACY_ZONE_SCOPED_NC(name, color) ZoneScopedNC(name, color)

// Zone text/value annotation
#define TRACY_ZONE_TEXT(text, len) ZoneText(text, len)
#define TRACY_ZONE_NAME(name, len) ZoneName(name, len)
#define TRACY_ZONE_VALUE(value) ZoneValue(value)

// Plotting values over time
#define TRACY_PLOT(name, value) TracyPlot(name, value)
#define TRACY_PLOT_CONFIG(name, type, step, fill, color) TracyPlotConfig(name, type, step, fill, color)

// Message logging
#define TRACY_MESSAGE(text, len) TracyMessage(text, len)
#define TRACY_MESSAGE_L(text) TracyMessageL(text)

// Memory tracking
#define TRACY_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define TRACY_FREE(ptr) TracyFree(ptr)

// Lockable wrapper (for mutex profiling)
#define TRACY_LOCKABLE(type, varname) TracyLockable(type, varname)
#define TRACY_LOCKABLE_NAME(varname, name, len) LockableName(varname, name, len)

// GPU context creation helper
// Call once after Vulkan device creation
inline tracy::VkCtx* createTracyVulkanContext(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkQueue queue,
    VkCommandBuffer cmdBuffer)
{
    return TracyVkContext(physicalDevice, device, queue, cmdBuffer);
}

// GPU context creation with calibrated timestamps (more accurate)
inline tracy::VkCtx* createTracyVulkanContextCalibrated(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkQueue queue,
    VkCommandBuffer cmdBuffer,
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT getTimeDomains,
    PFN_vkGetCalibratedTimestampsEXT getTimestamps)
{
    return TracyVkContextCalibrated(physicalDevice, device, queue, cmdBuffer,
                                     getTimeDomains, getTimestamps);
}

// GPU context cleanup
inline void destroyTracyVulkanContext(tracy::VkCtx* ctx) {
    TracyVkDestroy(ctx);
}

// Macro for GPU zone (use in command buffer recording)
// Uses Tracy's built-in TracyVkZone macro for correct API usage
#define TRACY_VK_ZONE(ctx, cmdBuffer, name) TracyVkZone(ctx, cmdBuffer, name)

// Collect GPU data - call once per frame after queue submit
#define TRACY_VK_COLLECT(ctx, cmdBuffer) TracyVkCollect(ctx, cmdBuffer)

// Named zones with colors for subsystem identification
// Colors follow Tracy's standard color format (0xRRGGBB)
#define TRACY_COLOR_TERRAIN    0x8B4513  // Saddle brown
#define TRACY_COLOR_WATER      0x1E90FF  // Dodger blue
#define TRACY_COLOR_VEGETATION 0x228B22  // Forest green
#define TRACY_COLOR_ATMOSPHERE 0x87CEEB  // Sky blue
#define TRACY_COLOR_SHADOW     0x2F4F4F  // Dark slate gray
#define TRACY_COLOR_POSTFX     0xFFD700  // Gold
#define TRACY_COLOR_PHYSICS    0xFF4500  // Orange red
#define TRACY_COLOR_ANIMATION  0xDA70D6  // Orchid
#define TRACY_COLOR_UI         0x9370DB  // Medium purple

#else // TRACY_ENABLE not defined

// No-op implementations when Tracy is disabled
#define TRACY_FRAME_MARK
#define TRACY_ZONE_SCOPED
#define TRACY_ZONE_SCOPED_N(name)
#define TRACY_ZONE_SCOPED_C(color)
#define TRACY_ZONE_SCOPED_NC(name, color)
#define TRACY_ZONE_TEXT(text, len)
#define TRACY_ZONE_NAME(name, len)
#define TRACY_ZONE_VALUE(value)
#define TRACY_PLOT(name, value)
#define TRACY_PLOT_CONFIG(name, type, step, fill, color)
#define TRACY_MESSAGE(text, len)
#define TRACY_MESSAGE_L(text)
#define TRACY_ALLOC(ptr, size)
#define TRACY_FREE(ptr)
#define TRACY_LOCKABLE(type, varname) type varname
#define TRACY_LOCKABLE_NAME(varname, name, len)
#define TRACY_VK_ZONE(ctx, cmdBuffer, name)
#define TRACY_VK_COLLECT(ctx, cmdBuffer)

// Stub colors
#define TRACY_COLOR_TERRAIN    0
#define TRACY_COLOR_WATER      0
#define TRACY_COLOR_VEGETATION 0
#define TRACY_COLOR_ATMOSPHERE 0
#define TRACY_COLOR_SHADOW     0
#define TRACY_COLOR_POSTFX     0
#define TRACY_COLOR_PHYSICS    0
#define TRACY_COLOR_ANIMATION  0
#define TRACY_COLOR_UI         0

#endif // TRACY_ENABLE
