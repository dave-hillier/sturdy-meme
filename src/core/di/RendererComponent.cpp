#include "di/RendererComponent.h"

#include <SDL3/SDL_log.h>

#include "DebugLineSystem.h"
#include "DescriptorInfrastructure.h"
#include "GlobalBufferManager.h"
#include "HiZSystem.h"
#include "Profiler.h"
#include "SceneManager.h"
#include "SkinnedMeshRenderer.h"
#include "TerrainFactory.h"
#include "VulkanContext.h"
#include "asset/AssetRegistry.h"
#include "lighting/ShadowSystem.h"

namespace core::di {
namespace {
std::optional<PostProcessSystem::Bundle> providePostProcessBundle(
    const InitContext& initContext,
    VulkanContext& vulkanContext) {
    VkFormat swapchainImageFormat = static_cast<VkFormat>(vulkanContext.getVkSwapchainImageFormat());
    auto bundle = PostProcessSystem::createWithDependencies(initContext,
                                                            vulkanContext.getRenderPass(),
                                                            swapchainImageFormat);
    if (!bundle) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize PostProcessSystem bundle");
    }
    return bundle;
}

std::unique_ptr<SkinnedMeshRenderer> provideSkinnedMeshRenderer(
    const std::optional<PostProcessSystem::Bundle>& postProcessBundle,
    VulkanContext& vulkanContext,
    DescriptorManager::Pool* descriptorPool,
    uint32_t framesInFlight,
    const std::string& resourcePath) {
    if (!postProcessBundle || !postProcessBundle->postProcess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMeshRenderer requires a valid PostProcessSystem");
        return nullptr;
    }

    SkinnedMeshRenderer::InitInfo info{};
    info.device = vulkanContext.getVkDevice();
    info.raiiDevice = &vulkanContext.getRaiiDevice();
    info.allocator = vulkanContext.getAllocator();
    info.descriptorPool = descriptorPool;
    info.renderPass = postProcessBundle->postProcess->getHDRRenderPass();
    info.extent = vulkanContext.getVkSwapchainExtent();
    info.shaderPath = resourcePath + "/shaders";
    info.framesInFlight = framesInFlight;
    info.addCommonBindings = [](DescriptorManager::LayoutBuilder& builder) {
        DescriptorInfrastructure::addCommonDescriptorBindings(builder);
    };

    auto skinnedMeshRenderer = SkinnedMeshRenderer::create(info);
    if (!skinnedMeshRenderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SkinnedMeshRenderer");
    }
    return skinnedMeshRenderer;
}

std::unique_ptr<GlobalBufferManager> provideGlobalBufferManager(
    VulkanContext& vulkanContext,
    uint32_t framesInFlight) {
    auto buffers = GlobalBufferManager::create(vulkanContext.getAllocator(),
                                               vulkanContext.getVkPhysicalDevice(),
                                               framesInFlight);
    if (!buffers) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GlobalBufferManager");
    }
    return buffers;
}

std::unique_ptr<ShadowSystem> provideShadowSystem(
    const InitContext& initContext,
    const std::unique_ptr<SkinnedMeshRenderer>& skinnedMesh,
    VkDescriptorSetLayout mainDescriptorSetLayout) {
    if (!skinnedMesh) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowSystem requires a valid SkinnedMeshRenderer");
        return nullptr;
    }
    auto shadowSystem = ShadowSystem::create(initContext,
                                             mainDescriptorSetLayout,
                                             skinnedMesh->getDescriptorSetLayout());
    if (!shadowSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize ShadowSystem");
    }
    return shadowSystem;
}

std::unique_ptr<TerrainSystem> provideTerrainSystem(
    const InitContext& initContext,
    const std::optional<PostProcessSystem::Bundle>& postProcessBundle,
    const std::unique_ptr<ShadowSystem>& shadowSystem,
    const std::string& resourcePath) {
    if (!postProcessBundle || !postProcessBundle->postProcess || !shadowSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainSystem requires PostProcessSystem and ShadowSystem");
        return nullptr;
    }

    TerrainFactory::Config terrainFactoryConfig{};
    terrainFactoryConfig.hdrRenderPass = postProcessBundle->postProcess->getHDRRenderPass();
    terrainFactoryConfig.shadowRenderPass = shadowSystem->getShadowRenderPass();
    terrainFactoryConfig.shadowMapSize = shadowSystem->getShadowMapSize();
    terrainFactoryConfig.resourcePath = resourcePath;

    auto terrainSystem = TerrainFactory::create(initContext, terrainFactoryConfig);
    if (!terrainSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize TerrainSystem");
    }
    return terrainSystem;
}

std::unique_ptr<SceneManager> provideSceneManager(
    const std::unique_ptr<TerrainSystem>& terrainSystem,
    VulkanContext& vulkanContext,
    AssetRegistry* assetRegistry,
    const std::string& resourcePath,
    const glm::vec2& sceneOrigin) {
    if (!terrainSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SceneManager requires a valid TerrainSystem");
        return nullptr;
    }

    SceneBuilder::InitInfo sceneInfo{};
    sceneInfo.allocator = vulkanContext.getAllocator();
    sceneInfo.device = vulkanContext.getVkDevice();
    sceneInfo.commandPool = vulkanContext.getCommandPool();
    sceneInfo.graphicsQueue = vulkanContext.getVkGraphicsQueue();
    sceneInfo.physicalDevice = vulkanContext.getVkPhysicalDevice();
    sceneInfo.resourcePath = resourcePath;
    sceneInfo.assetRegistry = assetRegistry;
    sceneInfo.getTerrainHeight = [terrain = terrainSystem.get()](float x, float z) {
        return terrain ? terrain->getHeightAt(x, z) : 0.0f;
    };
    sceneInfo.sceneOrigin = sceneOrigin;
    sceneInfo.deferRenderables = true;

    auto sceneManager = SceneManager::create(sceneInfo);
    if (!sceneManager) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SceneManager");
    }
    return sceneManager;
}

CoreResources provideCoreResources(
    const std::optional<PostProcessSystem::Bundle>& postProcessBundle,
    const std::unique_ptr<ShadowSystem>& shadowSystem,
    const std::unique_ptr<TerrainSystem>& terrainSystem,
    uint32_t framesInFlight) {
    if (!postProcessBundle || !postProcessBundle->postProcess || !shadowSystem || !terrainSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CoreResources requires PostProcess, Shadow, and Terrain systems");
        return {};
    }

    return CoreResources::collect(*postProcessBundle->postProcess, *shadowSystem, *terrainSystem, framesInFlight);
}

std::optional<SnowSystemGroup::Bundle> provideSnowBundle(
    const InitContext& initContext,
    const CoreResources& coreResources) {
    if (!coreResources.isValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Snow systems require valid core resources");
        return std::nullopt;
    }

    SnowSystemGroup::CreateDeps snowDeps{initContext, coreResources.hdr.renderPass};
    auto snowBundle = SnowSystemGroup::createAll(snowDeps);
    if (!snowBundle) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SnowSystemGroup");
    }
    return snowBundle;
}

std::optional<VegetationSystemGroup::Bundle> provideVegetationBundle(
    const InitContext& initContext,
    const CoreResources& coreResources,
    const ScatterSystemFactory::RockConfig& rockConfig) {
    if (!coreResources.isValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vegetation systems require valid core resources");
        return std::nullopt;
    }

    VegetationSystemGroup::CreateDeps vegDeps{
        initContext,
        coreResources.hdr.renderPass,
        coreResources.shadow.renderPass,
        coreResources.shadow.mapSize,
        coreResources.terrain.size,
        coreResources.terrain.getHeightAt,
        rockConfig
    };

    auto vegBundle = VegetationSystemGroup::createAll(vegDeps);
    if (!vegBundle) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize VegetationSystemGroup");
    }
    return vegBundle;
}

std::optional<AtmosphereSystemGroup::Bundle> provideAtmosphereBundle(
    const InitContext& initContext,
    const CoreResources& coreResources,
    const std::unique_ptr<GlobalBufferManager>& globalBuffers) {
    if (!coreResources.isValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Atmosphere systems require valid core resources");
        return std::nullopt;
    }
    if (!globalBuffers) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Atmosphere systems require GlobalBufferManager");
        return std::nullopt;
    }

    AtmosphereSystemGroup::CreateDeps atmosDeps{
        initContext,
        coreResources.hdr.renderPass,
        coreResources.shadow.cascadeView,
        coreResources.shadow.sampler,
        globalBuffers->lightBuffers.buffers
    };

    auto atmosBundle = AtmosphereSystemGroup::createAll(atmosDeps);
    if (!atmosBundle) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize AtmosphereSystemGroup");
    }
    return atmosBundle;
}

std::optional<GeometrySystemGroup::Bundle> provideGeometryBundle(
    const InitContext& initContext,
    const CoreResources& coreResources,
    const std::unique_ptr<GlobalBufferManager>& globalBuffers,
    const std::string& resourcePath) {
    if (!coreResources.isValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Geometry systems require valid core resources");
        return std::nullopt;
    }
    if (!globalBuffers) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Geometry systems require GlobalBufferManager");
        return std::nullopt;
    }

    GeometrySystemGroup::CreateDeps geomDeps{
        initContext,
        coreResources.hdr.renderPass,
        globalBuffers->uniformBuffers.buffers,
        resourcePath,
        coreResources.terrain.getHeightAt
    };

    auto geomBundle = GeometrySystemGroup::createAll(geomDeps);
    if (!geomBundle) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GeometrySystemGroup");
    }
    return geomBundle;
}

std::unique_ptr<HiZSystem> provideHiZSystem(
    const InitContext& initContext,
    VulkanContext& vulkanContext) {
    auto hiZSystem = HiZSystem::create(initContext, vulkanContext.getDepthFormat());
    if (!hiZSystem) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Warning: Hi-Z system initialization failed, occlusion culling disabled");
    }
    return hiZSystem;
}

std::unique_ptr<Profiler> provideProfiler(
    VulkanContext& vulkanContext,
    uint32_t framesInFlight) {
    return Profiler::create(vulkanContext.getVkDevice(), vulkanContext.getVkPhysicalDevice(), framesInFlight);
}

std::optional<WaterSystemGroup::Bundle> provideWaterBundle(
    const InitContext& initContext,
    const CoreResources& coreResources,
    const std::string& resourcePath) {
    if (!coreResources.isValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Water systems require valid core resources");
        return std::nullopt;
    }

    WaterSystemGroup::CreateDeps waterDeps{
        initContext,
        coreResources.hdr.renderPass,
        65536.0f,
        resourcePath
    };

    auto waterBundle = WaterSystemGroup::createAll(waterDeps);
    if (!waterBundle) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize WaterSystemGroup");
    }
    return waterBundle;
}

std::unique_ptr<DebugLineSystem> provideDebugLineSystem(
    const InitContext& initContext,
    const CoreResources& coreResources) {
    if (!coreResources.isValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem requires valid core resources");
        return nullptr;
    }

    auto debugLineSystem = DebugLineSystem::create(initContext, coreResources.hdr.renderPass);
    if (!debugLineSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create debug line system");
    }
    return debugLineSystem;
}

RendererSubsystemBundle provideRendererSubsystemBundle(
    std::optional<PostProcessSystem::Bundle> postProcess,
    std::unique_ptr<SkinnedMeshRenderer> skinnedMesh,
    std::unique_ptr<GlobalBufferManager> globalBuffers,
    std::unique_ptr<ShadowSystem> shadow,
    std::unique_ptr<TerrainSystem> terrain,
    std::unique_ptr<SceneManager> scene,
    CoreResources core,
    std::optional<SnowSystemGroup::Bundle> snow,
    std::optional<VegetationSystemGroup::Bundle> vegetation,
    std::optional<AtmosphereSystemGroup::Bundle> atmosphere,
    std::optional<GeometrySystemGroup::Bundle> geometry,
    std::unique_ptr<HiZSystem> hiZ,
    std::unique_ptr<Profiler> profiler,
    std::optional<WaterSystemGroup::Bundle> water,
    std::unique_ptr<DebugLineSystem> debugLine) {
    RendererSubsystemBundle bundle{};
    bundle.postProcess = std::move(postProcess);
    bundle.skinnedMesh = std::move(skinnedMesh);
    bundle.globalBuffers = std::move(globalBuffers);
    bundle.shadow = std::move(shadow);
    bundle.terrain = std::move(terrain);
    bundle.scene = std::move(scene);
    bundle.core = std::move(core);
    bundle.snow = std::move(snow);
    bundle.vegetation = std::move(vegetation);
    bundle.atmosphere = std::move(atmosphere);
    bundle.geometry = std::move(geometry);
    bundle.hiZ = std::move(hiZ);
    bundle.profiler = std::move(profiler);
    bundle.water = std::move(water);
    bundle.debugLine = std::move(debugLine);
    return bundle;
}
} // namespace

fruit::Component<RendererSubsystemBundle> getRendererComponent(
    VulkanContext& vulkanContext,
    const InitContext& initContext,
    const Renderer::Config& rendererConfig,
    const std::string& resourcePath,
    uint32_t framesInFlight,
    DescriptorManager::Pool* descriptorPool,
    VkDescriptorSetLayout mainDescriptorSetLayout,
    const ScatterSystemFactory::RockConfig& rockConfig,
    AssetRegistry* assetRegistry,
    const glm::vec2& sceneOrigin) {
    return fruit::createComponent()
        .bindInstance(vulkanContext)
        .bindInstance(initContext)
        .bindInstance(rendererConfig)
        .bindInstance(resourcePath)
        .bindInstance(framesInFlight)
        .bindInstance(descriptorPool)
        .bindInstance(mainDescriptorSetLayout)
        .bindInstance(rockConfig)
        .bindInstance(assetRegistry)
        .bindInstance(sceneOrigin)
        .registerProvider(providePostProcessBundle)
        .registerProvider(provideSkinnedMeshRenderer)
        .registerProvider(provideGlobalBufferManager)
        .registerProvider(provideShadowSystem)
        .registerProvider(provideTerrainSystem)
        .registerProvider(provideSceneManager)
        .registerProvider(provideCoreResources)
        .registerProvider(provideSnowBundle)
        .registerProvider(provideVegetationBundle)
        .registerProvider(provideAtmosphereBundle)
        .registerProvider(provideGeometryBundle)
        .registerProvider(provideHiZSystem)
        .registerProvider(provideProfiler)
        .registerProvider(provideWaterBundle)
        .registerProvider(provideDebugLineSystem)
        .registerProvider(provideRendererSubsystemBundle);
}

} // namespace core::di
