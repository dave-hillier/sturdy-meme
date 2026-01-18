#include "SystemsModule.h"
#include "InitContext.h"
#include "VulkanContext.h"
#include "DescriptorInfrastructure.h"

// Include system headers
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "TerrainFactory.h"
#include "HiZSystem.h"
#include "SceneManager.h"
#include "GlobalBufferManager.h"
#include "SkinnedMeshRenderer.h"
#include "GpuProfiler.h"
#include "DebugLineSystem.h"
#include "UBOBuilder.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"

#include <SDL3/SDL.h>

namespace di {

PostProcessBundle SystemsModule::createPostProcessBundle(
    const InitContext& initCtx,
    VkRenderPass swapchainRenderPass,
    VkFormat swapchainImageFormat
) {
    PostProcessBundle bundle;

    // Use the existing createWithDependencies pattern
    auto result = PostProcessSystem::createWithDependencies(
        initCtx, swapchainRenderPass, swapchainImageFormat
    );

    if (!result) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SystemsModule: Failed to create PostProcessBundle");
        return bundle;
    }

    bundle.postProcess = std::move(result->postProcess);
    bundle.bloom = std::move(result->bloom);
    bundle.bilateralGrid = std::move(result->bilateralGrid);

    return bundle;
}

CoreSystemsBundle SystemsModule::createCoreSystems(
    const InitContext& initCtx,
    VulkanContext& vulkanContext,
    DescriptorInfrastructure& descriptorInfra,
    const SystemsConfig& config
) {
    CoreSystemsBundle bundle;

    // Create PostProcess bundle first (needed by other systems)
    bundle.postProcess = createPostProcessBundle(
        initCtx,
        vulkanContext.getRenderPass(),
        static_cast<VkFormat>(vulkanContext.getVkSwapchainImageFormat())
    );

    if (!bundle.postProcess.postProcess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SystemsModule: PostProcess creation failed");
        return bundle;
    }

    // Create Shadow System
    ShadowSystem::InitInfo shadowInfo;
    shadowInfo.device = initCtx.device;
    shadowInfo.allocator = initCtx.allocator;
    shadowInfo.descriptorPool = descriptorInfra.getDescriptorPool();
    shadowInfo.shaderPath = initCtx.shaderPath;
    shadowInfo.raiiDevice = initCtx.raiiDevice;

    bundle.shadow = ShadowSystem::create(shadowInfo);
    if (!bundle.shadow) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SystemsModule: ShadowSystem creation failed");
        return bundle;
    }

    // Create Terrain System if enabled
    if (config.enableTerrain) {
        TerrainFactory::Config terrainConfig;
        terrainConfig.maxDepth = config.terrainMaxDepth;
        terrainConfig.terrainSize = config.terrainSize;

        bundle.terrain = TerrainFactory::create(initCtx, terrainConfig);
        if (!bundle.terrain) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "SystemsModule: TerrainSystem creation failed");
            // Continue without terrain - not fatal
        }
    }

    // Create HiZ System
    bundle.hiZ = HiZSystem::create(initCtx);
    if (!bundle.hiZ) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SystemsModule: HiZSystem creation failed");
        return bundle;
    }

    return bundle;
}

InfrastructureBundle SystemsModule::createInfrastructure(
    const InitContext& initCtx,
    VulkanContext& vulkanContext,
    DescriptorInfrastructure& descriptorInfra,
    const SystemsConfig& config
) {
    InfrastructureBundle bundle;

    // Create SceneManager
    SceneManager::InitInfo sceneInfo;
    sceneInfo.device = initCtx.device;
    sceneInfo.physicalDevice = initCtx.physicalDevice;
    sceneInfo.allocator = initCtx.allocator;
    sceneInfo.descriptorPool = descriptorInfra.getDescriptorPool();
    sceneInfo.descriptorSetLayout = descriptorInfra.getVkDescriptorSetLayout();
    sceneInfo.resourcePath = config.resourcePath;

    bundle.sceneManager = SceneManager::create(sceneInfo);
    if (!bundle.sceneManager) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SystemsModule: SceneManager creation failed");
        return bundle;
    }

    // Create GlobalBufferManager
    bundle.globalBuffers = GlobalBufferManager::create(
        initCtx.allocator,
        initCtx.physicalDevice,
        initCtx.framesInFlight
    );
    if (!bundle.globalBuffers) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SystemsModule: GlobalBufferManager creation failed");
        return bundle;
    }

    // Create SkinnedMeshRenderer
    // Note: This requires the HDR render pass from PostProcessSystem
    // which may not be available yet - leave for later wiring

    // Create Profiler
    bundle.profiler = Profiler::create(
        initCtx.device,
        initCtx.physicalDevice,
        initCtx.framesInFlight
    );
    if (!bundle.profiler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SystemsModule: Profiler creation failed");
        // Continue without profiler - not fatal
    }

    // Create DebugLineSystem
    // Note: This requires HDR render pass - leave for later wiring

    // Create UBOBuilder
    bundle.uboBuilder = std::make_unique<UBOBuilder>();

    // Create TimeSystem
    bundle.time = std::make_unique<TimeSystem>();

    // Create CelestialCalculator
    bundle.celestial = std::make_unique<CelestialCalculator>();

    // Create EnvironmentSettings
    bundle.environmentSettings = std::make_unique<EnvironmentSettings>();

    return bundle;
}

fruit::Component<
    fruit::Required<VulkanContext, InitContext, DescriptorInfrastructure, SystemsConfig>,
    PostProcessBundle,
    CoreSystemsBundle,
    InfrastructureBundle
> SystemsModule::getComponent() {
    return fruit::createComponent()
        .registerProvider<PostProcessBundle(
            const InitContext&, VulkanContext&)>(
            [](const InitContext& ctx, VulkanContext& vulkanCtx) {
                return createPostProcessBundle(
                    ctx,
                    vulkanCtx.getRenderPass(),
                    static_cast<VkFormat>(vulkanCtx.getVkSwapchainImageFormat())
                );
            })
        .registerProvider<CoreSystemsBundle(
            const InitContext&, VulkanContext&,
            DescriptorInfrastructure&, const SystemsConfig&)>(
            [](const InitContext& ctx, VulkanContext& vulkanCtx,
               DescriptorInfrastructure& descInfra, const SystemsConfig& config) {
                return createCoreSystems(ctx, vulkanCtx, descInfra, config);
            })
        .registerProvider<InfrastructureBundle(
            const InitContext&, VulkanContext&,
            DescriptorInfrastructure&, const SystemsConfig&)>(
            [](const InitContext& ctx, VulkanContext& vulkanCtx,
               DescriptorInfrastructure& descInfra, const SystemsConfig& config) {
                return createInfrastructure(ctx, vulkanCtx, descInfra, config);
            });
}

fruit::Component<SystemsConfig> getSystemsConfigComponent(SystemsConfig config) {
    return fruit::createComponent()
        .registerProvider<SystemsConfig>([config]() { return config; });
}

} // namespace di
