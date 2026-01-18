#pragma once

#include <fruit/fruit.h>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <memory>

// Forward declarations
class VulkanContext;
class DescriptorManager;
struct InitContext;

namespace di {

/**
 * Configuration for the core DI module
 * This is passed to the component to configure external dependencies
 */
struct CoreConfig {
    SDL_Window* window = nullptr;
    std::string resourcePath;
    uint32_t framesInFlight = 3;
};

/**
 * CoreModule - Provides core Vulkan infrastructure
 *
 * Bindings provided:
 * - VulkanContext (singleton)
 * - InitContext (factory-built from VulkanContext)
 * - VkDevice, VmaAllocator, VkQueue, VkCommandPool (extracted from VulkanContext)
 * - Paths (shader path, resource path)
 */
class CoreModule {
public:
    /**
     * Get the Fruit component for core bindings.
     * The component requires a CoreConfig to be installed first.
     */
    static fruit::Component<
        fruit::Required<CoreConfig>,
        VulkanContext,
        InitContext
    > getComponent();

private:
    // Factory functions for creating dependencies
    static VulkanContext* createVulkanContext(const CoreConfig& config);
    static InitContext createInitContext(
        VulkanContext& vulkanContext,
        const CoreConfig& config
    );
};

/**
 * Provides the CoreConfig as an installed component
 */
fruit::Component<CoreConfig> getCoreConfigComponent(CoreConfig config);

} // namespace di
