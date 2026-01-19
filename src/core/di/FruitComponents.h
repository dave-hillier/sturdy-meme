#pragma once

#include <fruit/fruit.h>
#include <memory>
#include <vulkan/vulkan.h>
#include <string>

// Forward declarations
class PostProcessSystem;
class ShadowSystem;
class TerrainSystem;
class GlobalBufferManager;

/**
 * Fruit DI with system lifecycle ownership.
 *
 * Systems are non-movable (GPU resources), so Fruit manages unique_ptrs.
 * The injector owns system lifetimes.
 *
 * Usage:
 *   fruit::Injector<...> injector(getRenderingComponent(params));
 *   ShadowSystem& shadow = *injector.get<std::unique_ptr<ShadowSystem>&>();
 */

// Runtime parameters needed for system creation
struct VulkanParams {
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VmaAllocator allocator;
    VkQueue graphicsQueue;
    VkCommandPool commandPool;
    VkDescriptorSetLayout mainDescriptorSetLayout;
    uint32_t framesInFlight;
    VkExtent2D extent;
    std::string shaderPath;
    std::string resourcePath;
};

// Unique_ptr aliases for Fruit injection
using ShadowSystemPtr = std::unique_ptr<ShadowSystem>;
using PostProcessSystemPtr = std::unique_ptr<PostProcessSystem>;
using TerrainSystemPtr = std::unique_ptr<TerrainSystem>;
using GlobalBufferManagerPtr = std::unique_ptr<GlobalBufferManager>;

/**
 * Combined component installing core systems.
 * Fruit manages construction order based on dependencies.
 */
fruit::Component<ShadowSystemPtr, PostProcessSystemPtr, TerrainSystemPtr, GlobalBufferManagerPtr>
getRenderingComponent(VulkanParams params);
