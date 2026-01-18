#include "CoreModule.h"
#include "VulkanContext.h"
#include "InitContext.h"
#include "DescriptorManager.h"
#include <SDL3/SDL.h>

namespace di {

VulkanContext* CoreModule::createVulkanContext(const CoreConfig& config) {
    if (!config.window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CoreModule: window is null");
        return nullptr;
    }

    auto ctx = std::make_unique<VulkanContext>();

    // Initialize the VulkanContext with the window (combined init)
    if (!ctx->init(config.window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CoreModule: Failed to init Vulkan");
        return nullptr;
    }

    // Create command pool and buffers
    if (!ctx->createCommandPoolAndBuffers(config.framesInFlight)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CoreModule: Failed to create command pool");
        return nullptr;
    }

    return ctx.release();
}

InitContext CoreModule::createInitContext(
    VulkanContext& vulkanContext,
    const CoreConfig& config
) {
    // Build InitContext using VulkanContext methods
    // Note: descriptor pool is not set here - it's created by DescriptorInfrastructure
    InitContext ctx{};
    ctx.raiiDevice = &vulkanContext.getRaiiDevice();
    ctx.device = vulkanContext.getVkDevice();
    ctx.physicalDevice = vulkanContext.getVkPhysicalDevice();
    ctx.allocator = vulkanContext.getAllocator();
    ctx.graphicsQueue = vulkanContext.getVkGraphicsQueue();
    ctx.commandPool = vulkanContext.getCommandPool();
    ctx.shaderPath = config.resourcePath + "/shaders";
    ctx.resourcePath = config.resourcePath;
    ctx.framesInFlight = config.framesInFlight;
    ctx.extent = vulkanContext.getVkSwapchainExtent();

    return ctx;
}

fruit::Component<
    fruit::Required<CoreConfig>,
    VulkanContext,
    InitContext
> CoreModule::getComponent() {
    return fruit::createComponent()
        .registerProvider<VulkanContext*(const CoreConfig&)>(
            [](const CoreConfig& config) {
                return createVulkanContext(config);
            })
        .registerProvider<InitContext(VulkanContext&, const CoreConfig&)>(
            [](VulkanContext& vulkanContext, const CoreConfig& config) {
                return createInitContext(vulkanContext, config);
            });
}

fruit::Component<CoreConfig> getCoreConfigComponent(CoreConfig config) {
    return fruit::createComponent()
        .registerProvider<CoreConfig>([config]() { return config; });
}

} // namespace di
