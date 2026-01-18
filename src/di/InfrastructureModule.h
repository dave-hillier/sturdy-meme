#pragma once

#include <fruit/fruit.h>
#include <memory>

// Forward declarations
class VulkanContext;
class RenderingInfrastructure;
class DescriptorInfrastructure;
class TaskScheduler;
class DescriptorManager;
struct InitContext;

namespace di {

/**
 * InfrastructureModule - Provides threading, descriptor, and asset infrastructure
 *
 * Bindings provided:
 * - TaskScheduler (singleton reference to global instance)
 * - RenderingInfrastructure (threading, async transfers, frame graph)
 * - DescriptorInfrastructure (layouts, pools, pipelines)
 * - DescriptorManager::Pool* (from DescriptorInfrastructure)
 */
class InfrastructureModule {
public:
    /**
     * Configuration for infrastructure
     */
    struct Config {
        uint32_t threadCount = 0;  // 0 = auto-detect
        uint32_t setsPerPool = 64;
    };

    /**
     * Get the Fruit component for infrastructure bindings.
     */
    static fruit::Component<
        fruit::Required<VulkanContext, InitContext>,
        RenderingInfrastructure,
        DescriptorInfrastructure
    > getComponent();

private:
    static RenderingInfrastructure* createRenderingInfrastructure(
        VulkanContext& vulkanContext
    );

    static DescriptorInfrastructure* createDescriptorInfrastructure(
        VulkanContext& vulkanContext,
        const InitContext& initCtx
    );
};

} // namespace di
