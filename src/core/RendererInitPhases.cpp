// RendererInitPhases.cpp - High-level initialization phases for Renderer
// Split from Renderer.cpp to keep file sizes manageable

#include <array>
#include <filesystem>
#include "Renderer.h"
#include "MaterialDescriptorFactory.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "SkySystem.h"
#include "GlobalBufferManager.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "GrassSystem.h"
#include "DisplacementSystem.h"
#include "WeatherSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "LeafSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "CloudShadowSystem.h"
#include "AtmosphereSystemGroup.h"
#include "SnowSystemGroup.h"
#include "GeometrySystemGroup.h"
#include "HiZSystem.h"
#include "ScatterSystem.h"
#include "ScatterSystemFactory.h"
#include "TreeSystem.h"
#include "ThreadedTreeGenerator.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "VegetationContentGenerator.h"
#include "VegetationSystemGroup.h"
#include "DeferredTerrainObjects.h"
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "WaterSystemGroup.h"
#include "SSRSystem.h"
#include "DebugLineSystem.h"
#include "Profiler.h"
#include "InitProfiler.h"
#include "SkinnedMeshRenderer.h"
#include "SceneManager.h"
#include "ErosionDataLoader.h"
#include "RoadNetworkLoader.h"
#include "RoadRiverVisualization.h"
#include "ResizeCoordinator.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"
#include "UBOBuilder.h"
#include "WindSystem.h"
#include "SystemWiring.h"
#include "TerrainFactory.h"
#include "di/RendererComponent.h"
#include "threading/TaskScheduler.h"
#include <SDL3/SDL.h>

bool Renderer::initCoreVulkanResources() {
    // Create swapchain-dependent resources (render pass, depth buffer, framebuffers)
    if (!vulkanContext_->createSwapchainResources()) return false;

    // Create command pool and buffers
    if (!vulkanContext_->createCommandPoolAndBuffers(MAX_FRAMES_IN_FLIGHT)) return false;

    // Initialize multi-threading infrastructure via RenderingInfrastructure
    {
        INIT_PROFILE_PHASE("ThreadingInfra");

        // Use TaskScheduler thread count for parallel command recording
        uint32_t threadCount = TaskScheduler::instance().getThreadCount();
        renderingInfra_.init(*vulkanContext_, threadCount);
    }

    return true;
}

bool Renderer::initDescriptorInfrastructure() {
    // Initialize descriptor infrastructure (layout and pool)
    DescriptorInfrastructure::Config descConfig;
    descConfig.setsPerPool = config_.setsPerPool;
    descConfig.poolSizes = config_.descriptorPoolSizes;

    if (!descriptorInfra_.initDescriptors(*vulkanContext_, descConfig)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize descriptor infrastructure");
        return false;
    }
    return true;
}

bool Renderer::initSubsystems(const InitContext& initCtx) {
    VkDevice device = vulkanContext_->getVkDevice();
    VmaAllocator allocator = vulkanContext_->getAllocator();
    VkPhysicalDevice physicalDevice = vulkanContext_->getVkPhysicalDevice();
    VkQueue graphicsQueue = vulkanContext_->getVkGraphicsQueue();

    // Place scene at Town 1 settlement (market town with coastal/agricultural features)
    // Settlement coords (9200, 3000) in 0-16384 space -> world coords by subtracting 8192
    const float halfTerrain = 8192.0f;
    const glm::vec2 sceneOrigin(9200.0f - halfTerrain, 3000.0f - halfTerrain);

    // Rock placement configuration
    ScatterSystemFactory::RockConfig rockConfig{};
    rockConfig.rockVariations = 6;
    rockConfig.rocksPerVariation = 10;
    rockConfig.minRadius = 0.4f;
    rockConfig.maxRadius = 2.0f;
    rockConfig.placementRadius = 100.0f;
    rockConfig.placementCenter = sceneOrigin;
    rockConfig.minDistanceBetween = 4.0f;
    rockConfig.roughness = 0.35f;
    rockConfig.asymmetry = 0.3f;
    rockConfig.subdivisions = 3;
    rockConfig.materialRoughness = 0.75f;
    rockConfig.materialMetallic = 0.0f;

    CoreResources core{};
    std::optional<VegetationSystemGroup::Bundle> vegetationBundle;
    std::optional<AtmosphereSystemGroup::Bundle> atmosphereBundle;
    std::optional<GeometrySystemGroup::Bundle> geometryBundle;
    std::optional<WaterSystemGroup::Bundle> waterBundle;
    std::unique_ptr<HiZSystem> hiZSystem;
    std::unique_ptr<Profiler> profiler;
    std::unique_ptr<DebugLineSystem> debugLineSystem;

    {
        INIT_PROFILE_PHASE("SubsystemDI");
        auto component = core::di::getRendererComponent(
            *vulkanContext_,
            initCtx,
            config_,
            resourcePath,
            MAX_FRAMES_IN_FLIGHT,
            descriptorInfra_.getDescriptorPool(),
            descriptorInfra_.getVkDescriptorSetLayout(),
            rockConfig,
            &renderingInfra_.assetRegistry(),
            sceneOrigin);
        fruit::Injector<core::di::RendererSubsystemBundle> injector(component);
        auto bundle = injector.get<core::di::RendererSubsystemBundle>();

        if (!bundle.postProcess) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PostProcess bundle creation failed");
            return false;
        }
        systems_->setPostProcess(std::move(bundle.postProcess->postProcess));
        systems_->setBloom(std::move(bundle.postProcess->bloom));
        systems_->setBilateralGrid(std::move(bundle.postProcess->bilateralGrid));

        if (!bundle.skinnedMesh) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkinnedMeshRenderer creation failed");
            return false;
        }
        systems_->setSkinnedMesh(std::move(bundle.skinnedMesh));

        if (!bundle.globalBuffers) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GlobalBufferManager creation failed");
            return false;
        }
        systems_->setGlobalBuffers(std::move(bundle.globalBuffers));

        if (!bundle.shadow) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShadowSystem creation failed");
            return false;
        }
        systems_->setShadow(std::move(bundle.shadow));

        if (!bundle.terrain) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainSystem creation failed");
            return false;
        }
        systems_->setTerrain(std::move(bundle.terrain));

        if (!bundle.scene) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SceneManager creation failed");
            return false;
        }
        systems_->setScene(std::move(bundle.scene));

        core = bundle.core;
        if (!core.isValid()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Core resources are invalid after DI setup");
            return false;
        }

        if (!bundle.snow) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SnowSystemGroup creation failed");
            return false;
        }
        systems_->setSnowMask(std::move(bundle.snow->snowMask));
        systems_->setVolumetricSnow(std::move(bundle.snow->volumetricSnow));
        systems_->setWeather(std::move(bundle.snow->weather));
        systems_->setLeaf(std::move(bundle.snow->leaf));

        vegetationBundle = std::move(bundle.vegetation);
        atmosphereBundle = std::move(bundle.atmosphere);
        geometryBundle = std::move(bundle.geometry);
        waterBundle = std::move(bundle.water);
        hiZSystem = std::move(bundle.hiZ);
        profiler = std::move(bundle.profiler);
        debugLineSystem = std::move(bundle.debugLine);
    }

    {
        INIT_PROFILE_PHASE("GraphicsPipeline");
        if (!descriptorInfra_.createGraphicsPipeline(*vulkanContext_,
                systems_->postProcess().getHDRRenderPass(), resourcePath)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create graphics pipeline");
            return false;
        }
    }

    // Initialize light buffers with empty data
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        LightBuffer emptyBuffer{};
        emptyBuffer.lightCount = glm::uvec4(0, 0, 0, 0);
        systems_->globalBuffers().updateLightBuffer(i, emptyBuffer);
    }

    // Initialize terrain system BEFORE scene so scene objects can query terrain height
    std::string terrainDataPath = resourcePath + "/terrain_data";

    TerrainFactory::Config terrainFactoryConfig{};
    terrainFactoryConfig.hdrRenderPass = systems_->postProcess().getHDRRenderPass();
    terrainFactoryConfig.shadowRenderPass = systems_->shadow().getShadowRenderPass();
    terrainFactoryConfig.shadowMapSize = systems_->shadow().getShadowMapSize();
    terrainFactoryConfig.resourcePath = resourcePath;

    TerrainConfig terrainConfig = TerrainFactory::buildTerrainConfig(terrainFactoryConfig);

    if (!createDescriptorSets()) return false;
    if (!createSkinnedMeshRendererDescriptorSets()) return false;

    if (!vegetationBundle) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VegetationSystemGroup creation failed");
        return false;
    }

    systems_->setWind(std::move(vegetationBundle->wind));
    systems_->setDisplacement(std::move(vegetationBundle->displacement));
    systems_->setGrass(std::move(vegetationBundle->grass));
    systems_->setRocks(std::move(vegetationBundle->rocks));
    systems_->setTree(std::move(vegetationBundle->tree));
    systems_->setTreeRenderer(std::move(vegetationBundle->treeRenderer));
    if (vegetationBundle->treeLOD) systems_->setTreeLOD(std::move(vegetationBundle->treeLOD));
    if (vegetationBundle->impostorCull) systems_->setImpostorCull(std::move(vegetationBundle->impostorCull));

    // Create system wiring helper for cross-system descriptor set updates
    SystemWiring wiring(device, MAX_FRAMES_IN_FLIGHT);

    // Wire terrain descriptors (UBOs, shadow maps, snow/cloud buffers)
    wiring.wireTerrainDescriptors(*systems_);

    // Defer vegetation content generation (trees, detritus) until terrain is fully loaded
    // This improves startup time by allowing initial render before vegetation is populated
    {
        DeferredTerrainObjects::Config deferredConfig;
        deferredConfig.resourcePath = resourcePath;
        deferredConfig.terrainSize = core.terrain.size;
        deferredConfig.getTerrainHeight = core.terrain.getHeightAt;
        deferredConfig.sceneOrigin = sceneOrigin;
        deferredConfig.forestCenter = glm::vec2(sceneOrigin.x + 200.0f, sceneOrigin.y + 100.0f);
        deferredConfig.forestRadius = 80.0f;
        deferredConfig.maxTrees = 500;
        deferredConfig.uniformBuffers = systems_->globalBuffers().uniformBuffers.buffers;
        deferredConfig.shadowView = systems_->shadow().getShadowImageView();
        deferredConfig.shadowSampler = systems_->shadow().getShadowSampler();
        deferredConfig.device = device;
        deferredConfig.allocator = allocator;
        deferredConfig.commandPool = vulkanContext_->getCommandPool();
        deferredConfig.graphicsQueue = graphicsQueue;
        deferredConfig.physicalDevice = physicalDevice;
        deferredConfig.descriptorPool = descriptorInfra_.getDescriptorPool();
        deferredConfig.descriptorSetLayout = descriptorInfra_.getVkDescriptorSetLayout();
        deferredConfig.framesInFlight = MAX_FRAMES_IN_FLIGHT;

        auto deferredObjects = DeferredTerrainObjects::create(deferredConfig);
        if (deferredObjects) {
            systems_->setDeferredTerrainObjects(std::move(deferredObjects));
            SDL_Log("Deferred terrain objects configured - will generate on first frame");
        }
    }

    // Helper lambda to get common bindings for a frame
    auto getCommonBindings = [this](uint32_t frameIndex) -> MaterialDescriptorFactory::CommonBindings {
        MaterialDescriptorFactory::CommonBindings common{};
        common.uniformBuffer = systems_->globalBuffers().uniformBuffers.buffers[frameIndex];
        common.uniformBufferSize = sizeof(UniformBufferObject);
        common.shadowMapView = systems_->shadow().getShadowImageView();
        common.shadowMapSampler = systems_->shadow().getShadowSampler();
        common.lightBuffer = systems_->globalBuffers().lightBuffers.buffers[frameIndex];
        common.lightBufferSize = sizeof(LightBuffer);
        common.emissiveMapView = systems_->scene().getSceneBuilder().getDefaultEmissiveMap()->getImageView();
        common.emissiveMapSampler = systems_->scene().getSceneBuilder().getDefaultEmissiveMap()->getSampler();
        common.pointShadowView = systems_->shadow().getPointShadowArrayView(frameIndex);
        common.pointShadowSampler = systems_->shadow().getPointShadowSampler();
        common.spotShadowView = systems_->shadow().getSpotShadowArrayView(frameIndex);
        common.spotShadowSampler = systems_->shadow().getSpotShadowSampler();
        common.snowMaskView = systems_->snowMask().getSnowMaskView();
        common.snowMaskSampler = systems_->snowMask().getSnowMaskSampler();
        common.placeholderTextureView = systems_->scene().getSceneBuilder().getWhiteTexture()->getImageView();
        common.placeholderTextureSampler = systems_->scene().getSceneBuilder().getWhiteTexture()->getSampler();
        return common;
    };

    // Create rocks descriptor sets (ScatterSystem owns them)
    if (!systems_->rocks().createDescriptorSets(
            device,
            *descriptorInfra_.getDescriptorPool(),
            descriptorInfra_.getVkDescriptorSetLayout(),
            MAX_FRAMES_IN_FLIGHT,
            getCommonBindings)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create rocks ScatterSystem descriptor sets");
        return false;
    }

    // Set common bindings function for deferred terrain objects (used when creating detritus)
    if (systems_->deferredTerrainObjects()) {
        systems_->deferredTerrainObjects()->setCommonBindingsFunc(getCommonBindings);
    }

    // Note: Tree descriptor sets are managed internally by TreeRenderer

    // Wire snow systems, leaf, and weather descriptors
    wiring.wireSnowSystems(*systems_);
    wiring.wireLeafDescriptors(*systems_);
    wiring.wireWeatherDescriptors(*systems_);

    if (!atmosphereBundle) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup creation failed");
        return false;
    }

    systems_->setSky(std::move(atmosphereBundle->sky));
    systems_->setFroxel(std::move(atmosphereBundle->froxel));
    systems_->setAtmosphereLUT(std::move(atmosphereBundle->atmosphereLUT));
    systems_->setCloudShadow(std::move(atmosphereBundle->cloudShadow));

    // Wire froxel to post-process for compositing
    AtmosphereSystemGroup::wireToPostProcess(systems_->froxel(), systems_->postProcess());

    // Wire grass descriptors (now that CloudShadowSystem exists)
    wiring.wireGrassDescriptors(*systems_);

    // Wire atmosphere connections (froxel to weather, cloud shadow to terrain)
    wiring.wireFroxelToWeather(*systems_);
    wiring.wireCloudShadowToTerrain(*systems_);
    wiring.wireCloudShadowBindings(*systems_);

    if (!geometryBundle) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GeometrySystemGroup creation failed");
        return false;
    }

    systems_->setCatmullClark(std::move(geometryBundle->catmullClark));

    // Create sky descriptor sets now that uniform buffers and LUTs are ready
    if (!systems_->sky().createDescriptorSets(systems_->globalBuffers().uniformBuffers.buffers, sizeof(UniformBufferObject), systems_->atmosphereLUT())) return false;

    if (hiZSystem) {
        systems_->setHiZ(std::move(hiZSystem));
        // Connect depth buffer to Hi-Z system - use HDR depth where scene is rendered
        systems_->hiZ().setDepthBuffer(core.hdr.depthView, vulkanContext_->getDepthSampler());

        // Initialize object data for culling
        systems_->hiZ().gatherObjects(systems_->scene().getRenderables(),
                                       systems_->rocks().getSceneObjects());
    }

    if (!profiler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Profiler creation failed");
        return false;
    }
    systems_->setProfiler(std::move(profiler));

    if (!waterBundle) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystemGroup creation failed");
        return false;
    }

    systems_->setWater(std::move(waterBundle->system));
    systems_->setFlowMap(std::move(waterBundle->flowMap));
    systems_->setWaterDisplacement(std::move(waterBundle->displacement));
    systems_->setFoam(std::move(waterBundle->foam));
    systems_->setSSR(std::move(waterBundle->ssr));
    if (waterBundle->tileCull) systems_->setWaterTileCull(std::move(waterBundle->tileCull));
    if (waterBundle->gBuffer) systems_->setWaterGBuffer(std::move(waterBundle->gBuffer));

    // Configure water subsystems (water level, wave properties, flow map)
    if (!WaterSystemGroup::configureSubsystems(*systems_, terrainConfig)) return false;

    // Create water descriptor sets
    if (!WaterSystemGroup::createDescriptorSets(*systems_, systems_->globalBuffers().uniformBuffers.buffers,
                                                 sizeof(UniformBufferObject), systems_->shadow(), systems_->terrain(),
                                                 systems_->postProcess(), vulkanContext_->getDepthSampler())) return false;

    // Wire underwater caustics (must happen after water system is fully initialized)
    wiring.wireCausticsToTerrain(*systems_);

    if (!createSyncObjects()) return false;

    // Initialize RendererCore (core frame loop execution)
    {
        RendererCore::InitParams coreParams;
        coreParams.vulkanContext = vulkanContext_.get();
        coreParams.frameGraph = &renderingInfra_.frameGraph();
        coreParams.frameSync = &frameSync_;
        if (!rendererCore_.init(coreParams)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize RendererCore");
            return false;
        }
    }

    if (!debugLineSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem creation failed");
        return false;
    }
    systems_->setDebugLineSystem(std::move(debugLineSystem));
    SDL_Log("Debug line system initialized");

    // Load road network data and configure visualization
    {
        // Try roads subdirectory first (standard layout), then root terrain_data
        std::string roadsSubdir = terrainDataPath + "/roads";
        std::string roadsPath = roadsSubdir + "/roads.geojson";
        std::string roadsPathAlt = terrainDataPath + "/roads.geojson";

        if (systems_->roadData().loadFromGeoJson(roadsPath)) {
            SDL_Log("Loaded road network from %s", roadsPath.c_str());
        } else if (systems_->roadData().loadFromGeoJson(roadsPathAlt)) {
            SDL_Log("Loaded road network from %s", roadsPathAlt.c_str());
        } else {
            SDL_Log("No road network data found (checked %s and %s)", roadsPath.c_str(), roadsPathAlt.c_str());
        }

        // Load water/river data from watershed subdirectory
        std::string watershedPath = terrainDataPath + "/watershed";
        ErosionLoadConfig erosionConfig{};
        erosionConfig.cacheDirectory = watershedPath;
        erosionConfig.seaLevel = 0.0f;
        if (systems_->erosionData().loadFromCache(erosionConfig)) {
            SDL_Log("Loaded water placement data from %s", watershedPath.c_str());
        } else {
            SDL_Log("No water placement data found at %s (visualization disabled)", watershedPath.c_str());
        }

        // Configure road/river visualization
        auto& vis = systems_->roadRiverVis();
        vis.setWaterData(&systems_->erosionData().getWaterData());
        vis.setRoadNetwork(&systems_->roadData().getRoadNetwork());
        vis.setTerrainTileCache(systems_->terrain().getTileCache());

        // Default config - can be modified via GUI later
        RoadRiverVisConfig visConfig{};
        visConfig.showRivers = true;
        visConfig.showRoads = true;
        visConfig.coneRadius = 0.5f;
        visConfig.coneLength = 2.0f;
        visConfig.heightAboveGround = 1.0f;
        visConfig.riverConeSpacing = 50.0f;
        visConfig.roadConeSpacing = 50.0f;
        vis.setConfig(visConfig);
        SDL_Log("Road/river visualization configured");
    }

    // Initialize UBO builder with system references
    UBOBuilder::Systems uboSystems{};
    uboSystems.timeSystem = &systems_->time();
    uboSystems.celestialCalculator = &systems_->celestial();
    uboSystems.shadowSystem = &systems_->shadow();
    uboSystems.windSystem = &systems_->wind();
    uboSystems.atmosphereLUTSystem = &systems_->atmosphereLUT();
    uboSystems.froxelSystem = &systems_->froxel();
    uboSystems.sceneManager = &systems_->scene();
    uboSystems.snowMaskSystem = &systems_->snowMask();
    uboSystems.volumetricSnowSystem = &systems_->volumetricSnow();
    uboSystems.cloudShadowSystem = &systems_->cloudShadow();
    uboSystems.environmentSettings = &systems_->environmentSettings();
    systems_->uboBuilder().setSystems(uboSystems);

    return true;
}

void Renderer::initResizeCoordinator() {
    // Register systems with resize coordinator
    // Order matters: render targets first, then systems that depend on them, then viewport-only

    // Render targets that need full resize (device/allocator/extent)
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->postProcess(), "PostProcessSystem", ResizePriority::RenderTarget);
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->bloom(), "BloomSystem", ResizePriority::RenderTarget);
    systems_->resizeCoordinator().registerWithResize(systems_->froxel(), "FroxelSystem", ResizePriority::RenderTarget);

    // Culling systems with simple resize (extent only, but reallocates)
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->hiZ(), "HiZSystem", ResizePriority::Culling);
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->ssr(), "SSRSystem", ResizePriority::Culling);
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->waterTileCull(), "WaterTileCull", ResizePriority::Culling);

    // G-buffer systems
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->waterGBuffer(), "WaterGBuffer", ResizePriority::GBuffer);

    // Viewport-only systems (setExtent)
    systems_->resizeCoordinator().registerWithExtent(systems_->terrain(), "TerrainSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->sky(), "SkySystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->water(), "WaterSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->grass(), "GrassSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->weather(), "WeatherSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->leaf(), "LeafSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->catmullClark(), "CatmullClarkSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->skinnedMesh(), "SkinnedMeshRenderer");

    // Register callback for bloom texture rebinding (needed after bloom resize)
    systems_->resizeCoordinator().registerCallback("BloomRebind",
        [this](VkDevice, VmaAllocator, VkExtent2D) {
            systems_->postProcess().setBloomTexture(systems_->bloom().getBloomOutput(), systems_->bloom().getBloomSampler());
        },
        nullptr,
        ResizePriority::RenderTarget);

    // Register core resize handler for swapchain, depth buffer, and framebuffers
    systems_->resizeCoordinator().setCoreResizeHandler([this](VkDevice, VmaAllocator) -> VkExtent2D {
        // Recreate swapchain
        if (!vulkanContext_->recreateSwapchain()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate swapchain");
            return {0, 0};
        }

        VkExtent2D newExtent = vulkanContext_->getVkSwapchainExtent();

        // Handle minimized window (extent = 0)
        if (newExtent.width == 0 || newExtent.height == 0) {
            return {0, 0};
        }

        // Recreate swapchain-dependent resources (depth buffer and framebuffers)
        if (!vulkanContext_->recreateSwapchainResources()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate swapchain resources during resize");
            return {0, 0};
        }

        return newExtent;
    });

    SDL_Log("Resize coordinator configured with %zu systems", 17UL);
}

void Renderer::initControlSubsystems() {
    // Initialize control subsystems in RendererSystems
    // These subsystems implement GUI-facing interfaces directly
    systems_->initControlSubsystems(*vulkanContext_, perfToggles);
}
