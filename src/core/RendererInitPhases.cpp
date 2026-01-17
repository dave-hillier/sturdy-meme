// RendererInitPhases.cpp - High-level initialization phases for Renderer
// Split from Renderer.cpp to keep file sizes manageable

#include <array>
#include <filesystem>
#include "Renderer.h"
#include "RendererInit.h"
#include "MaterialDescriptorFactory.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "SkySystem.h"
#include "GlobalBufferManager.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "GrassSystem.h"
#include "WeatherSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "LeafSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "CloudShadowSystem.h"
#include "AtmosphereSystemGroup.h"
#include "SnowSystemGroup.h"
#include "HiZSystem.h"
#include "CatmullClarkSystem.h"
#include "RockSystem.h"
#include "TreeSystem.h"
#include "ThreadedTreeGenerator.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "DetritusSystem.h"
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
#include "threading/TaskScheduler.h"
#include <SDL3/SDL.h>

bool Renderer::initCoreVulkanResources() {
    // Create swapchain-dependent resources (render pass, depth buffer, framebuffers)
    if (!vulkanContext_->createSwapchainResources()) return false;

    // Create command pool and buffers
    if (!vulkanContext_->createCommandPoolAndBuffers(MAX_FRAMES_IN_FLIGHT)) return false;

    // Initialize multi-threading infrastructure (from video architecture)
    {
        INIT_PROFILE_PHASE("ThreadingInfra");

        // Initialize async transfer manager for non-blocking GPU uploads
        if (!asyncTransferManager_.initialize(*vulkanContext_)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "AsyncTransferManager initialization failed - using synchronous transfers");
        }

        // Initialize threaded command pool for parallel command recording
        // Use TaskScheduler thread count + 1 for main thread
        uint32_t threadCount = TaskScheduler::instance().getThreadCount();
        if (threadCount > 0) {
            if (!threadedCommandPool_.initialize(*vulkanContext_, threadCount + 1)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "ThreadedCommandPool initialization failed - using single-threaded recording");
            }
        }

        // Initialize async texture uploader for non-blocking texture uploads
        if (!asyncTextureUploader_.initialize(
                vulkanContext_->getVkDevice(),
                vulkanContext_->getAllocator(),
                &asyncTransferManager_)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "AsyncTextureUploader initialization failed - using synchronous uploads");
        }
    }

    return true;
}

bool Renderer::initDescriptorInfrastructure() {
    if (!createDescriptorSetLayout()) return false;
    if (!createDescriptorPool()) return false;
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
        if (!createGraphicsPipeline()) return false;
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
        auto shadowSystem = ShadowSystem::create(initCtx, **descriptorSetLayout_, systems_->skinnedMesh().getDescriptorSetLayout());
        if (!shadowSystem) return false;
        systems_->setShadow(std::move(shadowSystem));
    }

    // Initialize terrain system BEFORE scene so scene objects can query terrain height
    std::string terrainDataPath = resourcePath + "/terrain_data";

    // Initialize terrain system with CBT (loads heightmap directly)
    TerrainSystem::TerrainInitParams terrainParams{};
    terrainParams.renderPass = systems_->postProcess().getHDRRenderPass();
    terrainParams.shadowRenderPass = systems_->shadow().getShadowRenderPass();
    terrainParams.shadowMapSize = systems_->shadow().getShadowMapSize();
    terrainParams.texturePath = resourcePath + "/textures";

    TerrainConfig terrainConfig{};
    terrainConfig.size = 16384.0f;
    terrainConfig.maxDepth = 20;
    terrainConfig.minDepth = 5;
    terrainConfig.targetEdgePixels = 16.0f;
    terrainConfig.splitThreshold = 100.0f;
    terrainConfig.mergeThreshold = 50.0f;
    // Isle of Wight altitude range (-15m to 220m, includes beaches below sea level)
    terrainConfig.minAltitude = -15.0f;
    terrainConfig.maxAltitude = 220.0f;
    // heightScale is computed from minAltitude/maxAltitude during init

    // Enable LOD-based tile streaming from preprocessed tile cache
    terrainConfig.tileCacheDir = resourcePath + "/terrain_data";
    terrainConfig.tileLoadRadius = 2000.0f;   // Load high-res tiles within 2km
    terrainConfig.tileUnloadRadius = 3000.0f; // Unload tiles beyond 3km

    // Enable virtual texturing (if tile directory exists)
    terrainConfig.virtualTextureTileDir = resourcePath + "/vt_tiles";
    terrainConfig.useVirtualTexture = true;

    std::unique_ptr<TerrainSystem> terrainSystem;
    {
        INIT_PROFILE_PHASE("TerrainSystem");
        terrainSystem = TerrainSystem::create(initCtx, terrainParams, terrainConfig);
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
    sceneInfo.assetRegistry = &assetRegistry_;  // Pass asset registry for centralized texture management
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
        RockConfig rockConfig{};
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
        systems_->setGrass(std::move(vegBundle->grass));
        systems_->setRock(std::move(vegBundle->rock));
        systems_->setTree(std::move(vegBundle->tree));
        systems_->setTreeRenderer(std::move(vegBundle->treeRenderer));
        if (vegBundle->treeLOD) systems_->setTreeLOD(std::move(vegBundle->treeLOD));
        if (vegBundle->impostorCull) systems_->setImpostorCull(std::move(vegBundle->impostorCull));
    }

    const EnvironmentSettings* envSettings = &systems_->wind().getEnvironmentSettings();

    // Get wind buffers for grass and other descriptor sets
    std::vector<VkBuffer> windBuffers(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        windBuffers[i] = systems_->wind().getBufferInfo(i).buffer;
    }

    // Update terrain descriptor sets with shared resources
    auto convertBuffers = [](const std::vector<VkBuffer>& raw) {
        std::vector<vk::Buffer> result;
        result.reserve(raw.size());
        for (auto b : raw) result.push_back(vk::Buffer(b));
        return result;
    };
    systems_->terrain().updateDescriptorSets(
        vk::Device(device),
        convertBuffers(systems_->globalBuffers().uniformBuffers.buffers),
        vk::ImageView(systems_->shadow().getShadowImageView()),
        vk::Sampler(systems_->shadow().getShadowSampler()),
        convertBuffers(systems_->globalBuffers().snowBuffers.buffers),
        convertBuffers(systems_->globalBuffers().cloudShadowBuffers.buffers));

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

    // Create rock descriptor sets (RockSystem owns them)
    if (!systems_->rock().createDescriptorSets(
            device,
            *descriptorManagerPool,
            **descriptorSetLayout_,
            MAX_FRAMES_IN_FLIGHT,
            getCommonBindings)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create RockSystem descriptor sets");
        return false;
    }

    // Create detritus descriptor sets (DetritusSystem owns them)
    if (systems_->detritus()) {
        if (!systems_->detritus()->createDescriptorSets(
                device,
                *descriptorManagerPool,
                **descriptorSetLayout_,
                MAX_FRAMES_IN_FLIGHT,
                getCommonBindings)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create DetritusSystem descriptor sets");
            return false;
        }
    }

    // Note: Tree descriptor sets are managed internally by TreeRenderer

    // Connect leaf system to environment settings
    systems_->leaf().setEnvironmentSettings(envSettings);

    // Update weather system descriptor sets
    systems_->weather().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, windBuffers,
                                        systems_->postProcess().getHDRDepthView(), systems_->shadow().getShadowSampler(),
                                        &systems_->globalBuffers().dynamicRendererUBO);

    // Connect snow to environment settings and systems
    systems_->snowMask().setEnvironmentSettings(envSettings);
    systems_->volumetricSnow().setEnvironmentSettings(envSettings);
    systems_->terrain().setSnowMask(device, systems_->snowMask().getSnowMaskView(), systems_->snowMask().getSnowMaskSampler());
    systems_->terrain().setVolumetricSnowCascades(device,
        systems_->volumetricSnow().getCascadeView(0), systems_->volumetricSnow().getCascadeView(1),
        systems_->volumetricSnow().getCascadeView(2), systems_->volumetricSnow().getCascadeSampler());
    systems_->grass().setSnowMask(device, systems_->snowMask().getSnowMaskView(), systems_->snowMask().getSnowMaskSampler());

    // Update leaf system descriptor sets
    // Pass triple-buffered tile info buffers to avoid CPU-GPU sync issues
    std::array<VkBuffer, 3> leafTileInfoBuffers = {
        systems_->terrain().getTileInfoBuffer(0),
        systems_->terrain().getTileInfoBuffer(1),
        systems_->terrain().getTileInfoBuffer(2)
    };
    systems_->leaf().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, windBuffers,
                                     systems_->terrain().getHeightMapView(), systems_->terrain().getHeightMapSampler(),
                                     systems_->grass().getDisplacementImageView(), systems_->grass().getDisplacementSampler(),
                                     systems_->terrain().getTileArrayView(), systems_->terrain().getTileSampler(),
                                     leafTileInfoBuffers, &systems_->globalBuffers().dynamicRendererUBO);

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

    // Update grass descriptor sets (now that CloudShadowSystem exists)
    // Pass triple-buffered tile info buffers to avoid CPU-GPU sync issues
    std::array<VkBuffer, 3> tileInfoBuffers = {
        systems_->terrain().getTileInfoBuffer(0),
        systems_->terrain().getTileInfoBuffer(1),
        systems_->terrain().getTileInfoBuffer(2)
    };
    systems_->grass().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, systems_->shadow().getShadowImageView(), systems_->shadow().getShadowSampler(), windBuffers, systems_->globalBuffers().lightBuffers.buffers,
                                      systems_->terrain().getHeightMapView(), systems_->terrain().getHeightMapSampler(),
                                      systems_->globalBuffers().snowBuffers.buffers, systems_->globalBuffers().cloudShadowBuffers.buffers,
                                      systems_->cloudShadow().getShadowMapView(), systems_->cloudShadow().getShadowMapSampler(),
                                      systems_->terrain().getTileArrayView(), systems_->terrain().getTileSampler(),
                                      tileInfoBuffers, &systems_->globalBuffers().dynamicRendererUBO);

    // Connect froxel volume to weather system
    systems_->weather().setFroxelVolume(systems_->froxel().getScatteringVolumeView(), systems_->froxel().getVolumeSampler(),
                                   systems_->froxel().getVolumetricFarPlane(), FroxelSystem::DEPTH_DISTRIBUTION);

    // Connect cloud shadow map to terrain system
    systems_->terrain().setCloudShadowMap(device, systems_->cloudShadow().getShadowMapView(), systems_->cloudShadow().getShadowMapSampler());

    // Note: Caustics setup moved to initPhase4Complete after water system is created

    // Update cloud shadow bindings across all descriptor sets
    RendererInit::updateCloudShadowBindings(device, systems_->scene().getSceneBuilder().getMaterialRegistry(),
                                            systems_->rock(), systems_->detritus(), systems_->skinnedMesh(),
                                            systems_->cloudShadow().getShadowMapView(), systems_->cloudShadow().getShadowMapSampler(),
                                            MAX_FRAMES_IN_FLIGHT);

    // Initialize Catmull-Clark subdivision system via factory
    float suzanneX = 5.0f, suzanneZ = -5.0f;
    glm::vec3 suzannePos(suzanneX, core.terrain.getHeightAt(suzanneX, suzanneZ) + 2.0f, suzanneZ);

    CatmullClarkSystem::InitInfo catmullClarkInfo{};
    catmullClarkInfo.device = device;
    catmullClarkInfo.physicalDevice = physicalDevice;
    catmullClarkInfo.allocator = allocator;
    catmullClarkInfo.renderPass = core.hdr.renderPass;
    catmullClarkInfo.descriptorPool = initCtx.descriptorPool;
    catmullClarkInfo.extent = initCtx.extent;
    catmullClarkInfo.shaderPath = initCtx.shaderPath;
    catmullClarkInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    catmullClarkInfo.graphicsQueue = graphicsQueue;
    catmullClarkInfo.commandPool = vulkanContext_->getCommandPool();
    catmullClarkInfo.raiiDevice = &vulkanContext_->getRaiiDevice();

    CatmullClarkConfig catmullClarkConfig{};
    catmullClarkConfig.position = suzannePos;
    catmullClarkConfig.scale = glm::vec3(2.0f);
    catmullClarkConfig.targetEdgePixels = 12.0f;
    catmullClarkConfig.maxDepth = 16;
    catmullClarkConfig.splitThreshold = 18.0f;
    catmullClarkConfig.mergeThreshold = 6.0f;
    catmullClarkConfig.objPath = resourcePath + "/assets/suzanne.obj";

    auto catmullClarkSystem = CatmullClarkSystem::create(catmullClarkInfo, catmullClarkConfig);
    if (!catmullClarkSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create CatmullClarkSystem");
        return false;
    }
    systems_->setCatmullClark(std::move(catmullClarkSystem));
    systems_->catmullClark().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers);

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
        updateHiZObjectData();
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

    // Initialize water subsystems (configure WaterSystem, generate flow map)
    WaterSubsystems waterSubs{systems_->water(), systems_->waterDisplacement(), systems_->flowMap(), systems_->foam(), *systems_, systems_->waterTileCull(), systems_->waterGBuffer()};
    if (!RendererInit::initWaterSubsystems(waterSubs, initCtx, core.hdr.renderPass,
                                            systems_->shadow(), systems_->terrain(), terrainConfig, systems_->postProcess(), vulkanContext_->getDepthSampler())) return false;

    // Create water descriptor sets
    if (!RendererInit::createWaterDescriptorSets(waterSubs, systems_->globalBuffers().uniformBuffers.buffers,
                                                  sizeof(UniformBufferObject), systems_->shadow(), systems_->terrain(),
                                                  systems_->postProcess(), vulkanContext_->getDepthSampler())) return false;

    // Connect underwater caustics to terrain system (use foam texture as caustics pattern)
    // Must happen after water system is fully initialized
    if (systems_->water().getFoamTextureView() != VK_NULL_HANDLE) {
        systems_->terrain().setCaustics(device,
                                         systems_->water().getFoamTextureView(),
                                         systems_->water().getFoamTextureSampler(),
                                         systems_->water().getWaterLevel(),
                                         true);  // Enable caustics
    }

    if (!createSyncObjects()) return false;

    // Initialize RendererCore (core frame loop execution)
    {
        RendererCore::InitParams coreParams;
        coreParams.vulkanContext = vulkanContext_.get();
        coreParams.frameGraph = &frameGraph_;
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
