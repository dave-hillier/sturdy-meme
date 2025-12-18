// RendererInitPhases.cpp - High-level initialization phases for Renderer
// Split from Renderer.cpp to keep file sizes manageable

#include "Renderer.h"
#include "RendererInit.h"
#include "MaterialDescriptorFactory.h"
#include "VulkanResourceFactory.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
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
#include "HiZSystem.h"
#include "CatmullClarkSystem.h"
#include "RockSystem.h"
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "SSRSystem.h"
#include "TreeEditSystem.h"
#include "DebugLineSystem.h"
#include "Profiler.h"
#include "SkinnedMeshRenderer.h"
#include "SceneManager.h"
#include "ErosionDataLoader.h"
#include "ResizeCoordinator.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"
#include "UBOBuilder.h"
#include "WindSystem.h"
#include <SDL3/SDL.h>

bool Renderer::initCoreVulkanResources() {
    if (!createRenderPass()) return false;
    if (!createDepthResources()) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPool()) return false;
    return true;
}

bool Renderer::initDescriptorInfrastructure() {
    if (!createDescriptorSetLayout()) return false;
    if (!createDescriptorPool()) return false;
    return true;
}

bool Renderer::initSubsystems(const InitContext& initCtx) {
    VkDevice device = vulkanContext.getDevice();
    VmaAllocator allocator = vulkanContext.getAllocator();
    VkPhysicalDevice physicalDevice = vulkanContext.getPhysicalDevice();
    VkQueue graphicsQueue = vulkanContext.getGraphicsQueue();
    VkFormat swapchainImageFormat = vulkanContext.getSwapchainImageFormat();

    // Initialize post-processing systems (PostProcessSystem, BloomSystem)
    if (!RendererInit::initPostProcessing(*systems_, initCtx, renderPass.get(), swapchainImageFormat)) {
        return false;
    }

    if (!createGraphicsPipeline()) return false;

    // Initialize skinned mesh rendering (GPU skinning for animated characters)
    if (!initSkinnedMeshRenderer()) return false;

    // Initialize sky system via factory (needs HDR render pass from postProcessSystem)
    auto skySystem = SkySystem::create(initCtx, systems_->postProcess().getHDRRenderPass());
    if (!skySystem) return false;
    systems_->setSky(std::move(skySystem));

    if (!createCommandBuffers()) return false;

    // Initialize global buffer manager for all per-frame shared buffers
    auto globalBuffers = GlobalBufferManager::create(allocator, MAX_FRAMES_IN_FLIGHT);
    if (!globalBuffers) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GlobalBufferManager");
        return false;
    }
    systems_->setGlobalBuffers(std::move(globalBuffers));

    // Initialize light buffers with empty data
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        LightBuffer emptyBuffer{};
        emptyBuffer.lightCount = glm::uvec4(0, 0, 0, 0);
        systems_->globalBuffers().updateLightBuffer(i, emptyBuffer);
    }

    // Initialize shadow system (needs descriptor set layouts for pipeline compatibility)
    auto shadowSystem = ShadowSystem::create(initCtx, descriptorSetLayout.get(), systems_->skinnedMesh().getDescriptorSetLayout());
    if (!shadowSystem) return false;
    systems_->setShadow(std::move(shadowSystem));

    // Initialize terrain system BEFORE scene so scene objects can query terrain height
    std::string heightmapPath = resourcePath + "/assets/terrain/isleofwight-0m-200m.png";
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
    // Load Isle of Wight heightmap (-15m to 200m altitude range, includes beaches below sea level)
    terrainConfig.heightmapPath = resourcePath + "/assets/terrain/isleofwight-0m-200m.png";
    terrainConfig.minAltitude = -15.0f;
    terrainConfig.maxAltitude = 220.0f;
    // heightScale is computed from minAltitude/maxAltitude during init

    // Enable LOD-based tile streaming from preprocessed tile cache
    terrainConfig.tileCacheDir = resourcePath + "/terrain_data";
    terrainConfig.tileLoadRadius = 2000.0f;   // Load high-res tiles within 2km
    terrainConfig.tileUnloadRadius = 3000.0f; // Unload tiles beyond 3km

    auto terrainSystem = TerrainSystem::create(initCtx, terrainParams, terrainConfig);
    if (!terrainSystem) return false;
    systems_->setTerrain(std::move(terrainSystem));

    // Collect resources from tier-1 systems for tier-2+ initialization
    // This decouples tier-2 systems from tier-1 systems - they depend on resources, not systems
    CoreResources core = CoreResources::collect(systems_->postProcess(), systems_->shadow(), systems_->terrain(), MAX_FRAMES_IN_FLIGHT);

    // Initialize scene (meshes, textures, objects, lights) via factory
    // Pass terrain height function so objects can be placed on terrain
    SceneBuilder::InitInfo sceneInfo{};
    sceneInfo.allocator = allocator;
    sceneInfo.device = device;
    sceneInfo.commandPool = commandPool.get();
    sceneInfo.graphicsQueue = graphicsQueue;
    sceneInfo.physicalDevice = physicalDevice;
    sceneInfo.resourcePath = resourcePath;
    sceneInfo.getTerrainHeight = [this](float x, float z) {
        return systems_->terrain().getHeightAt(x, z);
    };

    auto sceneManager = SceneManager::create(sceneInfo);
    if (!sceneManager) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SceneManager");
        return false;
    }
    systems_->setScene(std::move(sceneManager));

    // Initialize snow subsystems (SnowMaskSystem, VolumetricSnowSystem)
    if (!RendererInit::initSnowSubsystems(*systems_, initCtx, core.hdr)) return false;

    if (!createDescriptorSets()) return false;
    if (!createSkinnedMeshRendererDescriptorSets()) return false;

    // Initialize grass and wind subsystems (WindSystem created via factory)
    if (!RendererInit::initGrassSubsystem(*systems_, initCtx, core.hdr, core.shadow)) return false;

    const EnvironmentSettings* envSettings = &systems_->wind().getEnvironmentSettings();

    // Get wind buffers for grass and other descriptor sets
    std::vector<VkBuffer> windBuffers(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        windBuffers[i] = systems_->wind().getBufferInfo(i).buffer;
    }
    // Note: grass.updateDescriptorSets is called later after CloudShadowSystem is created

    // Update terrain descriptor sets with shared resources
    systems_->terrain().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, systems_->shadow().getShadowImageView(), systems_->shadow().getShadowSampler(),
                                        systems_->globalBuffers().snowBuffers.buffers, systems_->globalBuffers().cloudShadowBuffers.buffers);

    // Initialize rock system via factory
    RockSystem::InitInfo rockInfo{};
    rockInfo.device = device;
    rockInfo.allocator = allocator;
    rockInfo.commandPool = commandPool.get();
    rockInfo.graphicsQueue = graphicsQueue;
    rockInfo.physicalDevice = physicalDevice;
    rockInfo.resourcePath = resourcePath;
    rockInfo.terrainSize = core.terrain.size;
    rockInfo.getTerrainHeight = core.terrain.getHeightAt;

    RockConfig rockConfig{};
    rockConfig.rockVariations = 6;
    rockConfig.rocksPerVariation = 10;
    rockConfig.minRadius = 0.4f;
    rockConfig.maxRadius = 2.0f;
    rockConfig.placementRadius = 100.0f;
    rockConfig.minDistanceBetween = 4.0f;
    rockConfig.roughness = 0.35f;
    rockConfig.asymmetry = 0.3f;
    rockConfig.subdivisions = 3;
    rockConfig.materialRoughness = 0.75f;
    rockConfig.materialMetallic = 0.0f;

    auto rockSystem = RockSystem::create(rockInfo, rockConfig);
    if (!rockSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create RockSystem");
        return false;
    }
    systems_->setRock(std::move(rockSystem));

    // Update rock descriptor sets now that rock textures are loaded
    {
        MaterialDescriptorFactory factory(device);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            MaterialDescriptorFactory::CommonBindings common{};
            common.uniformBuffer = systems_->globalBuffers().uniformBuffers.buffers[i];
            common.uniformBufferSize = sizeof(UniformBufferObject);
            common.shadowMapView = systems_->shadow().getShadowImageView();
            common.shadowMapSampler = systems_->shadow().getShadowSampler();
            common.lightBuffer = systems_->globalBuffers().lightBuffers.buffers[i];
            common.lightBufferSize = sizeof(LightBuffer);
            common.emissiveMapView = systems_->scene().getSceneBuilder().getDefaultEmissiveMap().getImageView();
            common.emissiveMapSampler = systems_->scene().getSceneBuilder().getDefaultEmissiveMap().getSampler();
            common.pointShadowView = systems_->shadow().getPointShadowArrayView(i);
            common.pointShadowSampler = systems_->shadow().getPointShadowSampler();
            common.spotShadowView = systems_->shadow().getSpotShadowArrayView(i);
            common.spotShadowSampler = systems_->shadow().getSpotShadowSampler();
            common.snowMaskView = systems_->snowMask().getSnowMaskView();
            common.snowMaskSampler = systems_->snowMask().getSnowMaskSampler();
            // Placeholder texture for unused PBR bindings (13-16)
            common.placeholderTextureView = systems_->scene().getSceneBuilder().getWhiteTexture().getImageView();
            common.placeholderTextureSampler = systems_->scene().getSceneBuilder().getWhiteTexture().getSampler();

            MaterialDescriptorFactory::MaterialTextures mat{};
            mat.diffuseView = systems_->rock().getRockTexture().getImageView();
            mat.diffuseSampler = systems_->rock().getRockTexture().getSampler();
            mat.normalView = systems_->rock().getRockNormalMap().getImageView();
            mat.normalSampler = systems_->rock().getRockNormalMap().getSampler();
            factory.writeDescriptorSet(rockDescriptorSets[i], common, mat);
        }
    }

    // Initialize weather and leaf subsystems
    if (!RendererInit::initWeatherSubsystems(*systems_, initCtx, core.hdr)) return false;

    // Connect leaf system to environment settings (must be done after initWeatherSubsystems creates LeafSystem)
    systems_->leaf().setEnvironmentSettings(envSettings);

    // Update weather system descriptor sets
    systems_->weather().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, windBuffers,
                                        systems_->postProcess().getHDRDepthView(), systems_->shadow().getShadowSampler());

    // Connect snow to environment settings and systems
    systems_->snowMask().setEnvironmentSettings(envSettings);
    systems_->volumetricSnow().setEnvironmentSettings(envSettings);
    systems_->terrain().setSnowMask(device, systems_->snowMask().getSnowMaskView(), systems_->snowMask().getSnowMaskSampler());
    systems_->terrain().setVolumetricSnowCascades(device,
        systems_->volumetricSnow().getCascadeView(0), systems_->volumetricSnow().getCascadeView(1),
        systems_->volumetricSnow().getCascadeView(2), systems_->volumetricSnow().getCascadeSampler());
    systems_->grass().setSnowMask(device, systems_->snowMask().getSnowMaskView(), systems_->snowMask().getSnowMaskSampler());

    // Update leaf system descriptor sets
    systems_->leaf().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, windBuffers,
                                     systems_->terrain().getHeightMapView(), systems_->terrain().getHeightMapSampler(),
                                     systems_->grass().getDisplacementImageView(), systems_->grass().getDisplacementSampler(),
                                     systems_->terrain().getTileArrayView(), systems_->terrain().getTileSampler(),
                                     systems_->terrain().getTileInfoBuffer());

    // Initialize atmosphere subsystems (Froxel, AtmosphereLUT, CloudShadow)
    if (!RendererInit::initAtmosphereSubsystems(*systems_, initCtx, core.shadow,
                                                 systems_->globalBuffers().lightBuffers.buffers)) return false;

    // Update grass descriptor sets (now that CloudShadowSystem exists)
    systems_->grass().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, systems_->shadow().getShadowImageView(), systems_->shadow().getShadowSampler(), windBuffers, systems_->globalBuffers().lightBuffers.buffers,
                                      systems_->terrain().getHeightMapView(), systems_->terrain().getHeightMapSampler(),
                                      systems_->globalBuffers().snowBuffers.buffers, systems_->globalBuffers().cloudShadowBuffers.buffers,
                                      systems_->cloudShadow().getShadowMapView(), systems_->cloudShadow().getShadowMapSampler(),
                                      systems_->terrain().getTileArrayView(), systems_->terrain().getTileSampler(),
                                      systems_->terrain().getTileInfoBuffer());

    // Connect froxel volume to weather system
    systems_->weather().setFroxelVolume(systems_->froxel().getScatteringVolumeView(), systems_->froxel().getVolumeSampler(),
                                   systems_->froxel().getVolumetricFarPlane(), FroxelSystem::DEPTH_DISTRIBUTION);

    // Connect cloud shadow map to terrain system
    systems_->terrain().setCloudShadowMap(device, systems_->cloudShadow().getShadowMapView(), systems_->cloudShadow().getShadowMapSampler());

    // Update cloud shadow bindings across all descriptor sets
    RendererInit::updateCloudShadowBindings(device, systems_->scene().getSceneBuilder().getMaterialRegistry(),
                                            rockDescriptorSets, systems_->skinnedMesh(),
                                            systems_->cloudShadow().getShadowMapView(), systems_->cloudShadow().getShadowMapSampler());

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
    catmullClarkInfo.commandPool = commandPool.get();

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
    auto hiZSystem = HiZSystem::create(initCtx, depthFormat);
    if (!hiZSystem) {
        SDL_Log("Warning: Hi-Z system initialization failed, occlusion culling disabled");
        // Continue without Hi-Z - it's an optional optimization
    } else {
        systems_->setHiZ(std::move(hiZSystem));
        // Connect depth buffer to Hi-Z system - use HDR depth where scene is rendered
        systems_->hiZ().setDepthBuffer(core.hdr.depthView, depthSampler.get());

        // Initialize object data for culling
        updateHiZObjectData();
    }

    // Initialize profiler for GPU and CPU timing
    // Factory always returns valid profiler - GPU may be disabled if init fails
    systems_->setProfiler(Profiler::create(device, physicalDevice, MAX_FRAMES_IN_FLIGHT));

    // Create WaterSystem via factory before initializing other water subsystems
    WaterSystem::InitInfo waterInfo{};
    waterInfo.device = device;
    waterInfo.physicalDevice = physicalDevice;
    waterInfo.allocator = allocator;
    waterInfo.descriptorPool = initCtx.descriptorPool;
    waterInfo.hdrRenderPass = core.hdr.renderPass;
    waterInfo.shaderPath = initCtx.shaderPath;
    waterInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    waterInfo.extent = initCtx.extent;
    waterInfo.commandPool = commandPool.get();
    waterInfo.graphicsQueue = graphicsQueue;
    waterInfo.waterSize = 65536.0f;  // Extend well beyond terrain for horizon
    waterInfo.assetPath = resourcePath;

    auto waterSystem = WaterSystem::create(waterInfo);
    if (!waterSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create WaterSystem");
        return false;
    }
    systems_->setWater(std::move(waterSystem));

    // Create water subsystems via factories before constructing WaterSubsystems struct
    // FlowMapGenerator
    FlowMapGenerator::InitInfo flowInfo{};
    flowInfo.device = device;
    flowInfo.allocator = allocator;
    flowInfo.commandPool = commandPool.get();
    flowInfo.queue = graphicsQueue;

    auto flowMapGenerator = FlowMapGenerator::create(flowInfo);
    if (!flowMapGenerator) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create FlowMapGenerator");
        return false;
    }
    systems_->setFlowMap(std::move(flowMapGenerator));

    // WaterDisplacement
    WaterDisplacement::InitInfo dispInfo{};
    dispInfo.device = device;
    dispInfo.physicalDevice = physicalDevice;
    dispInfo.allocator = allocator;
    dispInfo.commandPool = commandPool.get();
    dispInfo.computeQueue = graphicsQueue;
    dispInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    dispInfo.displacementResolution = 512;
    dispInfo.worldSize = 65536.0f;

    auto waterDisplacement = WaterDisplacement::create(dispInfo);
    if (!waterDisplacement) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create WaterDisplacement");
        return false;
    }
    systems_->setWaterDisplacement(std::move(waterDisplacement));

    // FoamBuffer
    FoamBuffer::InitInfo foamInfo{};
    foamInfo.device = device;
    foamInfo.physicalDevice = physicalDevice;
    foamInfo.allocator = allocator;
    foamInfo.commandPool = commandPool.get();
    foamInfo.computeQueue = graphicsQueue;
    foamInfo.shaderPath = initCtx.shaderPath;
    foamInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    foamInfo.resolution = 512;
    foamInfo.worldSize = 65536.0f;

    auto foamBuffer = FoamBuffer::create(foamInfo);
    if (!foamBuffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create FoamBuffer");
        return false;
    }
    systems_->setFoam(std::move(foamBuffer));

    // WaterTileCull
    WaterTileCull::InitInfo tileCullInfo{};
    tileCullInfo.device = device;
    tileCullInfo.physicalDevice = physicalDevice;
    tileCullInfo.allocator = allocator;
    tileCullInfo.commandPool = commandPool.get();
    tileCullInfo.computeQueue = graphicsQueue;
    tileCullInfo.shaderPath = initCtx.shaderPath;
    tileCullInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    tileCullInfo.extent = initCtx.extent;
    tileCullInfo.tileSize = 32;

    auto waterTileCull = WaterTileCull::create(tileCullInfo);
    if (waterTileCull) {
        systems_->setWaterTileCull(std::move(waterTileCull));
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create WaterTileCull - continuing without");
    }

    // WaterGBuffer
    WaterGBuffer::InitInfo gbufferInfo{};
    gbufferInfo.device = device;
    gbufferInfo.physicalDevice = physicalDevice;
    gbufferInfo.allocator = allocator;
    gbufferInfo.fullResExtent = initCtx.extent;
    gbufferInfo.resolutionScale = 0.5f;
    gbufferInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    gbufferInfo.shaderPath = initCtx.shaderPath;
    gbufferInfo.descriptorPool = initCtx.descriptorPool;

    auto waterGBuffer = WaterGBuffer::create(gbufferInfo);
    if (waterGBuffer) {
        systems_->setWaterGBuffer(std::move(waterGBuffer));
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create WaterGBuffer - continuing without");
    }

    // Initialize water subsystems (configure WaterSystem, generate flow map, create SSR)
    WaterSubsystems waterSubs{systems_->water(), systems_->waterDisplacement(), systems_->flowMap(), systems_->foam(), *systems_, systems_->waterTileCull(), systems_->waterGBuffer()};
    if (!RendererInit::initWaterSubsystems(waterSubs, initCtx, core.hdr.renderPass,
                                            systems_->shadow(), systems_->terrain(), terrainConfig, systems_->postProcess(), depthSampler.get())) return false;

    // Create water descriptor sets
    if (!RendererInit::createWaterDescriptorSets(waterSubs, systems_->globalBuffers().uniformBuffers.buffers,
                                                  sizeof(UniformBufferObject), systems_->shadow(), systems_->terrain(),
                                                  systems_->postProcess(), depthSampler.get())) return false;

    // Initialize tree edit system via factory
    TreeEditSystem::InitInfo treeEditInfo{};
    treeEditInfo.device = device;
    treeEditInfo.physicalDevice = physicalDevice;
    treeEditInfo.allocator = allocator;
    treeEditInfo.renderPass = core.hdr.renderPass;
    treeEditInfo.descriptorPool = initCtx.descriptorPool;
    treeEditInfo.extent = initCtx.extent;
    treeEditInfo.shaderPath = initCtx.shaderPath;
    treeEditInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    treeEditInfo.graphicsQueue = graphicsQueue;
    treeEditInfo.commandPool = commandPool.get();

    auto treeEditSystem = TreeEditSystem::create(treeEditInfo);
    if (!treeEditSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create TreeEditSystem");
        return false;
    }
    systems_->setTreeEdit(std::move(treeEditSystem));
    systems_->treeEdit().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers);

    if (!createSyncObjects()) return false;

    // Create debug line system via factory
    auto debugLineSystem = RendererInit::createDebugLineSystem(initCtx, core.hdr);
    if (!debugLineSystem) return false;
    systems_->setDebugLineSystem(std::move(debugLineSystem));
    SDL_Log("Debug line system initialized");

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

    // Tree edit system uses updateExtent
    systems_->resizeCoordinator().registerWithUpdateExtent(systems_->treeEdit(), "TreeEditSystem");

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
        if (!vulkanContext.recreateSwapchain()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate swapchain");
            return {0, 0};
        }

        VkExtent2D newExtent = vulkanContext.getSwapchainExtent();

        // Handle minimized window (extent = 0)
        if (newExtent.width == 0 || newExtent.height == 0) {
            return {0, 0};
        }

        SDL_Log("Window resized to %ux%u", newExtent.width, newExtent.height);

        // Recreate depth resources
        if (!recreateDepthResources(newExtent)) {
            return {0, 0};
        }

        // Recreate framebuffers
        destroyFramebuffers();
        if (!createFramebuffers()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate framebuffers during resize");
            return {0, 0};
        }

        return newExtent;
    });

    SDL_Log("Resize coordinator configured with %zu systems", 17UL);
}
