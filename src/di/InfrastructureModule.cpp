#include "InfrastructureModule.h"
#include "VulkanContext.h"
#include "InitContext.h"
#include "RenderingInfrastructure.h"
#include "DescriptorInfrastructure.h"
#include "threading/TaskScheduler.h"
#include <SDL3/SDL.h>

namespace di {

RenderingInfrastructure* InfrastructureModule::createRenderingInfrastructure(
    VulkanContext& vulkanContext
) {
    auto infra = std::make_unique<RenderingInfrastructure>();

    // Get thread count from TaskScheduler (already initialized)
    uint32_t threadCount = TaskScheduler::instance().threadCount();

    if (!infra->init(vulkanContext, threadCount)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "InfrastructureModule: Failed to init RenderingInfrastructure");
        return nullptr;
    }

    // Initialize asset registry with Vulkan resources
    infra->initAssetRegistry(
        vulkanContext.getDevice(),
        vulkanContext.getPhysicalDevice(),
        vulkanContext.getAllocator(),
        vulkanContext.getCommandPool(),
        vulkanContext.getGraphicsQueue()
    );

    return infra.release();
}

DescriptorInfrastructure* InfrastructureModule::createDescriptorInfrastructure(
    VulkanContext& vulkanContext,
    const InitContext& /* initCtx */
) {
    auto infra = std::make_unique<DescriptorInfrastructure>();

    DescriptorInfrastructure::Config config;
    config.setsPerPool = 64;
    config.poolSizes = DescriptorPoolSizes::standard();

    if (!infra->initDescriptors(vulkanContext, config)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "InfrastructureModule: Failed to init DescriptorInfrastructure");
        return nullptr;
    }

    return infra.release();
}

fruit::Component<
    fruit::Required<VulkanContext, InitContext>,
    RenderingInfrastructure,
    DescriptorInfrastructure
> InfrastructureModule::getComponent() {
    return fruit::createComponent()
        .registerProvider<RenderingInfrastructure*(VulkanContext&)>(
            [](VulkanContext& ctx) {
                return createRenderingInfrastructure(ctx);
            })
        .registerProvider<DescriptorInfrastructure*(VulkanContext&, const InitContext&)>(
            [](VulkanContext& ctx, const InitContext& initCtx) {
                return createDescriptorInfrastructure(ctx, initCtx);
            });
}

} // namespace di
