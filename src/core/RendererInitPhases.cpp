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
    VkFormat swapchainImageFormat = static_cast<VkFormat>(vulkanContext_->getVkSwapchainImageFormat());

    // Initialize post-processing systems (PostProcessSystem, BloomSystem, BilateralGridSystem)
    {
        INIT_PROFILE_PHASE("PostProcessing");
        auto bundle = PostProcessSystem::createWithDependencies(initCtx, vulkanContext_->getRenderPass(), swapchainImageFormat);
        if (!bundle) {
            return false;
        }
        systems_->setPostProcess(std::move(bundle->postProcess));
        systems_->setBloom(std::move(bundle->bloom));
        systems_->setBilateralGrid(std::move(bundle->bilateralGrid));
    }

    {
        INIT_PROFILE_PHASE("GraphicsPipeline");
        if (!descriptorInfra_.createGraphicsPipeline(*vulkanContext_,
                systems_->postProcess().getHDRRenderPass(), resourcePath)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create graphics pipeline");
            return false;
        }
    }

    // Initialize skinned mesh rendering (GPU skinning for animated characters)
    {
        INIT_PROFILE_PHASE("SkinnedMeshRenderer");
        if (!initSkinnedMeshRenderer()) return false;
    }

    // Note: Command buffers are now created by VulkanContext in initCoreVulkanResources()

    // Initialize global buffer manager for all per-frame shared buffers
    {
        INIT_PROFILE_PHASE("GlobalBufferManager");
        auto globalBuffers = GlobalBufferManager::create(allocator, physicalDevice, MAX_FRAMES_IN_FLIGHT);
        if (!globalBuffers) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GlobalBufferManager");
            return false;
        }
        systems_->setGlobalBuffers(std::move(globalBuffers));
    }

    // Initialize light buffers with empty data
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        LightBuffer emptyBuffer{};
        emptyBuffer.lightCount = glm::uvec4(0, 0, 0, 0);
        systems_->globalBuffers().updateLightBuffer(i, emptyBuffer);
    }

    // Initialize shadow system (needs descriptor set layouts for pipeline compatibility)
    {
        INIT_PROFILE_PHASE("ShadowSystem");
        auto shadowSystem = ShadowSystem::create(initCtx, descriptorInfra_.getVkDescriptorSetLayout(), systems_->skinnedMesh().getDescriptorSetLayout());
        if (!shadowSystem) return false;
        systems_->setShadow(std::move(shadowSystem));
    }

    // Initialize terrain system BEFORE scene so scene objects can query terrain height
    std::string terrainDataPath = resourcePath + "/terrain_data";

    // Configure and create terrain via factory
    TerrainFactory::Config terrainFactoryConfig{};
    terrainFactoryConfig.hdrRenderPass = systems_->postProcess().getHDRRenderPass();
    terrainFactoryConfig.shadowRenderPass = systems_->shadow().getShadowRenderPass();
    terrainFactoryConfig.shadowMapSize = systems_->shadow().getShadowMapSize();
    terrainFactoryConfig.resourcePath = resourcePath;

    // Get terrain config for other systems that need it
    TerrainConfig terrainConfig = TerrainFactory::buildTerrainConfig(terrainFactoryConfig);

    {
        INIT_PROFILE_PHASE("TerrainSystem");
        auto terrainSystem = TerrainFactory::create(initCtx, terrainFactoryConfig);
        if (!terrainSystem) return false;
        systems_->setTerrain(std::move(terrainSystem));
    }

    // Collect resources from tier-1 systems for tier-2+ initialization
    // This decouples tier-2 systems from tier-1 systems - they depend on resources, not systems
    CoreResources core = CoreResources::collect(systems_->postProcess(), systems_->shadow(), systems_->terrain(), MAX_FRAMES_IN_FLIGHT);

    // Initialize scene (meshes, textures, objects, lights) via factory
    // Pass terrain height function so objects can be placed on terrain
    SceneBuilder::InitInfo sceneInfo{};
    sceneInfo.allocator = allocator;
    sceneInfo.device = device;
    sceneInfo.commandPool = vulkanContext_->getCommandPool();
    sceneInfo.graphicsQueue = graphicsQueue;
    sceneInfo.physicalDevice = physicalDevice;
    sceneInfo.resourcePath = resourcePath;
    sceneInfo.assetRegistry = &renderingInfra_.assetRegistry();  // Pass asset registry for centralized texture management
    sceneInfo.getTerrainHeight = [this](float x, float z) {
        return systems_->terrain().getHeightAt(x, z);
    };
    // Place scene at Town 1 settlement (market town with coastal/agricultural features)
    // Settlement coords (9200, 3000) in 0-16384 space -> world coords by subtracting 8192
    const float halfTerrain = 8192.0f;
    sceneInfo.sceneOrigin = glm::vec2(9200.0f - halfTerrain, 3000.0f - halfTerrain);

    {
        INIT_PROFILE_PHASE("SceneManager");
        auto sceneManager = SceneManager::create(sceneInfo);
        if (!sceneManager) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SceneManager");
            return false;
        }
        systems_->setScene(std::move(sceneManager));
    }

    // Create all snow and weather systems using SnowSystemGroup factory
    {
        SnowSystemGroup::CreateDeps snowDeps{initCtx, core.hdr.renderPass};
        auto snowBundle = SnowSystemGroup::createAll(snowDeps);
        if (!snowBundle) return false;

        systems_->setSnowMask(std::move(snowBundle->snowMask));
        systems_->setVolumetricSnow(std::move(snowBundle->volumetricSnow));
        systems_->setWeather(std::move(snowBundle->weather));
        systems_->setLeaf(std::move(snowBundle->leaf));
    }

    if (!createDescriptorSets()) return false;
    if (!createSkinnedMeshRendererDescriptorSets()) return false;

    // Create all vegetation systems using VegetationSystemGroup factory
    {
        INIT_PROFILE_PHASE("VegetationSystems");

        // Rock placement configuration
        ScatterSystemFactory::RockConfig rockConfig{};
        rockConfig.rockVariations = 6;
        rockConfig.rocksPerVariation = 10;
        rockConfig.minRadius = 0.4f;
        rockConfig.maxRadius = 2.0f;
        rockConfig.placementRadius = 100.0f;
        rockConfig.placementCenter = sceneInfo.sceneOrigin;
        rockConfig.minDistanceBetween = 4.0f;
        rockConfig.roughness = 0.35f;
        rockConfig.asymmetry = 0.3f;
        rockConfig.subdivisions = 3;
        rockConfig.materialRoughness = 0.75f;
        rockConfig.materialMetallic = 0.0f;

        VegetationSystemGroup::CreateDeps vegDeps{
            initCtx,
            core.hdr.renderPass,
            core.shadow.renderPass,
            core.shadow.mapSize,
            core.terrain.size,
            core.terrain.getHeightAt,
            rockConfig
        };

        auto vegBundle = VegetationSystemGroup::createAll(vegDeps);
        if (!vegBundle) return false;

        systems_->setWind(std::move(vegBundle->wind));
        systems_->setDisplacement(std::move(vegBundle->displacement));
        systems_->setGrass(std::move(vegBundle->grass));
        systems_->setRocks(std::move(vegBundle->rocks));
        systems_->setTree(std::move(vegBundle->tree));
        systems_->setTreeRenderer(std::move(vegBundle->treeRenderer));
        if (vegBundle->treeLOD) systems_->setTreeLOD(std::move(vegBundle->treeLOD));
        if (vegBundle->impostorCull) systems_->setImpostorCull(std::move(vegBundle->impostorCull));
    }

    // Create system wiring helper for cross-system descriptor set updates
    SystemWiring wiring(device, MAX_FRAMES_IN_FLIGHT);

    // Wire terrain descriptors (UBOs, shadow maps, snow/cloud buffers)
    wiring.wireTerrainDescriptors(*systems_);

    // Generate vegetation content using VegetationContentGenerator
    {
        VegetationContentGenerator::Config vegConfig;
        vegConfig.resourcePath = resourcePath;
        vegConfig.getTerrainHeight = core.terrain.getHeightAt;
        vegConfig.terrainSize = core.terrain.size;

        VegetationContentGenerator vegGen(vegConfig);

        // Generate demo trees and forest
        if (systems_->tree()) {
            vegGen.generateDemoTrees(*systems_->tree(), sceneInfo.sceneOrigin);

            const glm::vec2 forestCenter(sceneInfo.sceneOrigin.x + 200.0f, sceneInfo.sceneOrigin.y + 100.0f);
            vegGen.generateForest(*systems_->tree(), forestCenter, 80.0f, 500);

            // Generate impostor archetypes
            if (systems_->treeLOD()) {
                vegGen.generateImpostorArchetypes(*systems_->tree(), *systems_->treeLOD());
            }

            // Finalize tree systems
            vegGen.finalizeTreeSystems(
                *systems_->tree(),
                systems_->treeLOD(),
                systems_->impostorCull(),
                systems_->treeRenderer(),
                systems_->globalBuffers().uniformBuffers.buffers,
                systems_->shadow().getShadowImageView(),
                systems_->shadow().getShadowSampler());
        }
    }

    // Initialize detritus system (fallen branches scattered near trees)
    if (systems_->tree()) {
        VegetationContentGenerator::Config vegConfig;
        vegConfig.resourcePath = resourcePath;
        vegConfig.getTerrainHeight = core.terrain.getHeightAt;
        vegConfig.terrainSize = core.terrain.size;

        VegetationContentGenerator vegGen(vegConfig);
        VegetationContentGenerator::DetritusCreateInfo detritusInfo{
            device, allocator, vulkanContext_->getCommandPool(), graphicsQueue, physicalDevice
        };
        auto detritusSystem = vegGen.createDetritusSystem(detritusInfo, *systems_->tree());
        if (detritusSystem) {
            systems_->setDetritus(std::move(detritusSystem));
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

    // Create detritus descriptor sets (ScatterSystem owns them)
    if (systems_->detritus()) {
        if (!systems_->detritus()->createDescriptorSets(
                device,
                *descriptorInfra_.getDescriptorPool(),
                descriptorInfra_.getVkDescriptorSetLayout(),
                MAX_FRAMES_IN_FLIGHT,
                getCommonBindings)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create detritus ScatterSystem descriptor sets");
            return false;
        }
    }

    // Note: Tree descriptor sets are managed internally by TreeRenderer

    // Wire snow systems, leaf, and weather descriptors
    wiring.wireSnowSystems(*systems_);
    wiring.wireLeafDescriptors(*systems_);
    wiring.wireWeatherDescriptors(*systems_);

    // Initialize atmosphere subsystems (Sky, Froxel, AtmosphereLUT, CloudShadow)
    // Uses self-initializing AtmosphereSystemGroup to invert dependencies
    {
        INIT_PROFILE_PHASE("AtmosphereSubsystems");
        AtmosphereSystemGroup::CreateDeps atmosDeps{
            initCtx,
            core.hdr.renderPass,
            core.shadow.cascadeView,
            core.shadow.sampler,
            systems_->globalBuffers().lightBuffers.buffers
        };
        auto atmosBundle = AtmosphereSystemGroup::createAll(atmosDeps);
        if (!atmosBundle) return false;

        systems_->setSky(std::move(atmosBundle->sky));
        systems_->setFroxel(std::move(atmosBundle->froxel));
        systems_->setAtmosphereLUT(std::move(atmosBundle->atmosphereLUT));
        systems_->setCloudShadow(std::move(atmosBundle->cloudShadow));

        // Wire froxel to post-process for compositing
        AtmosphereSystemGroup::wireToPostProcess(systems_->froxel(), systems_->postProcess());
    }

    // Wire grass descriptors (now that CloudShadowSystem exists)
    wiring.wireGrassDescriptors(*systems_);

    // Wire atmosphere connections (froxel to weather, cloud shadow to terrain)
    wiring.wireFroxelToWeather(*systems_);
    wiring.wireCloudShadowToTerrain(*systems_);
    wiring.wireCloudShadowBindings(*systems_);

    // Initialize geometry systems via GeometrySystemGroup factory
    {
        INIT_PROFILE_PHASE("GeometrySubsystems");
        GeometrySystemGroup::CreateDeps geomDeps{
            initCtx,
            core.hdr.renderPass,
            systems_->globalBuffers().uniformBuffers.buffers,
            resourcePath,
            core.terrain.getHeightAt
        };
        auto geomBundle = GeometrySystemGroup::createAll(geomDeps);
        if (!geomBundle) return false;

        systems_->setCatmullClark(std::move(geomBundle->catmullClark));
    }

    // Create sky descriptor sets now that uniform buffers and LUTs are ready
    if (!systems_->sky().createDescriptorSets(systems_->globalBuffers().uniformBuffers.buffers, sizeof(UniformBufferObject), systems_->atmosphereLUT())) return false;

    // Initialize Hi-Z occlusion culling system via factory
    auto hiZSystem = HiZSystem::create(initCtx, vulkanContext_->getDepthFormat());
    if (!hiZSystem) {
        SDL_Log("Warning: Hi-Z system initialization failed, occlusion culling disabled");
        // Continue without Hi-Z - it's an optional optimization
    } else {
        systems_->setHiZ(std::move(hiZSystem));
        // Connect depth buffer to Hi-Z system - use HDR depth where scene is rendered
        systems_->hiZ().setDepthBuffer(core.hdr.depthView, vulkanContext_->getDepthSampler());

        // Initialize object data for culling
        systems_->hiZ().gatherObjects(systems_->scene().getRenderables(),
                                       systems_->rocks().getSceneObjects());
    }

    // Initialize profiler for GPU and CPU timing
    // Factory always returns valid profiler - GPU may be disabled if init fails
    systems_->setProfiler(Profiler::create(device, physicalDevice, MAX_FRAMES_IN_FLIGHT));

    // Create all water systems using WaterSystemGroup factory
    {
        INIT_PROFILE_PHASE("WaterSystems");
        WaterSystemGroup::CreateDeps waterDeps{
            initCtx,
            core.hdr.renderPass,
            65536.0f,  // waterSize - extend well beyond terrain for horizon
            resourcePath
        };

        auto waterBundle = WaterSystemGroup::createAll(waterDeps);
        if (!waterBundle) return false;

        systems_->setWater(std::move(waterBundle->system));
        systems_->setFlowMap(std::move(waterBundle->flowMap));
        systems_->setWaterDisplacement(std::move(waterBundle->displacement));
        systems_->setFoam(std::move(waterBundle->foam));
        systems_->setSSR(std::move(waterBundle->ssr));
        if (waterBundle->tileCull) systems_->setWaterTileCull(std::move(waterBundle->tileCull));
        if (waterBundle->gBuffer) systems_->setWaterGBuffer(std::move(waterBundle->gBuffer));
    }

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

    // Create debug line system via factory
    auto debugLineSystem = DebugLineSystem::create(initCtx, core.hdr.renderPass);
    if (!debugLineSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create debug line system");
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
