// RendererInitPhases.cpp - High-level initialization phases for Renderer
// Split from Renderer.cpp to keep file sizes manageable

#include "Renderer.h"
#include "RendererInit.h"
#include "MaterialDescriptorFactory.h"
#include "VulkanResourceFactory.h"
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
    if (!RendererInit::initPostProcessing(postProcessSystem, bloomSystem, initCtx, renderPass, swapchainImageFormat)) {
        return false;
    }

    if (!createGraphicsPipeline()) return false;

    // Initialize skinned mesh rendering (GPU skinning for animated characters)
    if (!initSkinnedMeshRenderer()) return false;

    // Initialize sky system (needs HDR render pass from postProcessSystem)
    if (!skySystem.init(initCtx, postProcessSystem.getHDRRenderPass())) return false;

    if (!createCommandBuffers()) return false;

    // Initialize global buffer manager for all per-frame shared buffers
    if (!globalBufferManager.init(allocator, MAX_FRAMES_IN_FLIGHT)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GlobalBufferManager");
        return false;
    }

    // Initialize light buffers with empty data
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        LightBuffer emptyBuffer{};
        emptyBuffer.lightCount = glm::uvec4(0, 0, 0, 0);
        globalBufferManager.updateLightBuffer(i, emptyBuffer);
    }

    // Initialize shadow system (needs descriptor set layouts for pipeline compatibility)
    if (!shadowSystem.init(initCtx, descriptorSetLayout, skinnedMeshRenderer.getDescriptorSetLayout())) return false;

    // Initialize terrain system BEFORE scene so scene objects can query terrain height
    std::string heightmapPath = resourcePath + "/assets/terrain/isleofwight-0m-200m.png";
    std::string terrainDataPath = resourcePath + "/terrain_data";

    // Initialize terrain system with CBT (loads heightmap directly)
    TerrainSystem::TerrainInitParams terrainParams{};
    terrainParams.renderPass = postProcessSystem.getHDRRenderPass();
    terrainParams.shadowRenderPass = shadowSystem.getShadowRenderPass();
    terrainParams.shadowMapSize = shadowSystem.getShadowMapSize();
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

    if (!terrainSystem.init(initCtx, terrainParams, terrainConfig)) return false;

    // Collect resources from tier-1 systems for tier-2+ initialization
    // This decouples tier-2 systems from tier-1 systems - they depend on resources, not systems
    CoreResources core = CoreResources::collect(postProcessSystem, shadowSystem, terrainSystem, MAX_FRAMES_IN_FLIGHT);

    // Initialize scene (meshes, textures, objects, lights)
    // Pass terrain height function so objects can be placed on terrain
    SceneBuilder::InitInfo sceneInfo{};
    sceneInfo.allocator = allocator;
    sceneInfo.device = device;
    sceneInfo.commandPool = commandPool;
    sceneInfo.graphicsQueue = graphicsQueue;
    sceneInfo.physicalDevice = physicalDevice;
    sceneInfo.resourcePath = resourcePath;
    sceneInfo.getTerrainHeight = [this](float x, float z) {
        return terrainSystem.getHeightAt(x, z);
    };

    if (!sceneManager.init(sceneInfo)) return false;

    // Initialize snow subsystems (SnowMaskSystem, VolumetricSnowSystem)
    if (!RendererInit::initSnowSubsystems(snowMaskSystem, volumetricSnowSystem, initCtx, core.hdr)) return false;

    if (!createDescriptorSets()) return false;
    if (!createSkinnedMeshRendererDescriptorSets()) return false;

    // Initialize grass and wind subsystems
    if (!RendererInit::initGrassSubsystem(grassSystem, windSystem, leafSystem, initCtx,
                                          core.hdr, core.shadow)) return false;

    const EnvironmentSettings* envSettings = &windSystem.getEnvironmentSettings();

    // Get wind buffers for grass descriptor sets
    std::vector<VkBuffer> windBuffers(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        windBuffers[i] = windSystem.getBufferInfo(i).buffer;
    }
    grassSystem.updateDescriptorSets(device, globalBufferManager.uniformBuffers.buffers, shadowSystem.getShadowImageView(), shadowSystem.getShadowSampler(), windBuffers, globalBufferManager.lightBuffers.buffers,
                                      terrainSystem.getHeightMapView(), terrainSystem.getHeightMapSampler(),
                                      globalBufferManager.snowBuffers.buffers, globalBufferManager.cloudShadowBuffers.buffers,
                                      cloudShadowSystem.getShadowMapView(), cloudShadowSystem.getShadowMapSampler());

    // Update terrain descriptor sets with shared resources
    terrainSystem.updateDescriptorSets(device, globalBufferManager.uniformBuffers.buffers, shadowSystem.getShadowImageView(), shadowSystem.getShadowSampler(),
                                        globalBufferManager.snowBuffers.buffers, globalBufferManager.cloudShadowBuffers.buffers);

    // Initialize rock system
    if (!RendererInit::initRockSystem(rockSystem, initCtx, core.terrain)) return false;

    // Update rock descriptor sets now that rock textures are loaded
    {
        MaterialDescriptorFactory factory(device);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            MaterialDescriptorFactory::CommonBindings common{};
            common.uniformBuffer = globalBufferManager.uniformBuffers.buffers[i];
            common.uniformBufferSize = sizeof(UniformBufferObject);
            common.shadowMapView = shadowSystem.getShadowImageView();
            common.shadowMapSampler = shadowSystem.getShadowSampler();
            common.lightBuffer = globalBufferManager.lightBuffers.buffers[i];
            common.lightBufferSize = sizeof(LightBuffer);
            common.emissiveMapView = sceneManager.getSceneBuilder().getDefaultEmissiveMap().getImageView();
            common.emissiveMapSampler = sceneManager.getSceneBuilder().getDefaultEmissiveMap().getSampler();
            common.pointShadowView = shadowSystem.getPointShadowArrayView(i);
            common.pointShadowSampler = shadowSystem.getPointShadowSampler();
            common.spotShadowView = shadowSystem.getSpotShadowArrayView(i);
            common.spotShadowSampler = shadowSystem.getSpotShadowSampler();
            common.snowMaskView = snowMaskSystem.getSnowMaskView();
            common.snowMaskSampler = snowMaskSystem.getSnowMaskSampler();
            // Placeholder texture for unused PBR bindings (13-16)
            common.placeholderTextureView = sceneManager.getSceneBuilder().getWhiteTexture().getImageView();
            common.placeholderTextureSampler = sceneManager.getSceneBuilder().getWhiteTexture().getSampler();

            MaterialDescriptorFactory::MaterialTextures mat{};
            mat.diffuseView = rockSystem.getRockTexture().getImageView();
            mat.diffuseSampler = rockSystem.getRockTexture().getSampler();
            mat.normalView = rockSystem.getRockNormalMap().getImageView();
            mat.normalSampler = rockSystem.getRockNormalMap().getSampler();
            factory.writeDescriptorSet(rockDescriptorSets[i], common, mat);
        }
    }

    // Initialize weather and leaf subsystems
    if (!RendererInit::initWeatherSubsystems(weatherSystem, leafSystem, initCtx, core.hdr)) return false;

    // Update weather system descriptor sets
    weatherSystem.updateDescriptorSets(device, globalBufferManager.uniformBuffers.buffers, windBuffers,
                                        postProcessSystem.getHDRDepthView(), shadowSystem.getShadowSampler());

    // Connect snow to environment settings and systems
    snowMaskSystem.setEnvironmentSettings(envSettings);
    volumetricSnowSystem.setEnvironmentSettings(envSettings);
    terrainSystem.setSnowMask(device, snowMaskSystem.getSnowMaskView(), snowMaskSystem.getSnowMaskSampler());
    terrainSystem.setVolumetricSnowCascades(device,
        volumetricSnowSystem.getCascadeView(0), volumetricSnowSystem.getCascadeView(1),
        volumetricSnowSystem.getCascadeView(2), volumetricSnowSystem.getCascadeSampler());
    grassSystem.setSnowMask(device, snowMaskSystem.getSnowMaskView(), snowMaskSystem.getSnowMaskSampler());

    // Update leaf system descriptor sets
    leafSystem.updateDescriptorSets(device, globalBufferManager.uniformBuffers.buffers, windBuffers,
                                     terrainSystem.getHeightMapView(), terrainSystem.getHeightMapSampler(),
                                     grassSystem.getDisplacementImageView(), grassSystem.getDisplacementSampler());

    // Initialize atmosphere subsystems (Froxel, AtmosphereLUT, CloudShadow)
    if (!RendererInit::initAtmosphereSubsystems(froxelSystem, atmosphereLUTSystem, cloudShadowSystem,
                                                 postProcessSystem, initCtx, core.shadow,
                                                 globalBufferManager.lightBuffers.buffers)) return false;

    // Connect froxel volume to weather system
    weatherSystem.setFroxelVolume(froxelSystem.getScatteringVolumeView(), froxelSystem.getVolumeSampler(),
                                   froxelSystem.getVolumetricFarPlane(), FroxelSystem::DEPTH_DISTRIBUTION);

    // Connect cloud shadow map to terrain system
    terrainSystem.setCloudShadowMap(device, cloudShadowSystem.getShadowMapView(), cloudShadowSystem.getShadowMapSampler());

    // Update cloud shadow bindings across all descriptor sets
    RendererInit::updateCloudShadowBindings(device, sceneManager.getSceneBuilder().getMaterialRegistry(),
                                            rockDescriptorSets, skinnedMeshRenderer,
                                            cloudShadowSystem.getShadowMapView(), cloudShadowSystem.getShadowMapSampler());

    // Initialize Catmull-Clark subdivision system
    float suzanneX = 5.0f, suzanneZ = -5.0f;
    glm::vec3 suzannePos(suzanneX, core.terrain.getHeightAt(suzanneX, suzanneZ) + 2.0f, suzanneZ);
    if (!RendererInit::initCatmullClarkSystem(catmullClarkSystem, initCtx, core.hdr, suzannePos)) return false;
    catmullClarkSystem.updateDescriptorSets(device, globalBufferManager.uniformBuffers.buffers);

    // Create sky descriptor sets now that uniform buffers and LUTs are ready
    if (!skySystem.createDescriptorSets(globalBufferManager.uniformBuffers.buffers, sizeof(UniformBufferObject), atmosphereLUTSystem)) return false;

    // Initialize Hi-Z occlusion culling system
    if (!hiZSystem.init(initCtx, depthFormat)) {
        SDL_Log("Warning: Hi-Z system initialization failed, occlusion culling disabled");
        // Continue without Hi-Z - it's an optional optimization
    } else {
        // Connect depth buffer to Hi-Z system - use HDR depth where scene is rendered
        hiZSystem.setDepthBuffer(core.hdr.depthView, depthSampler);

        // Initialize object data for culling
        updateHiZObjectData();
    }

    // Initialize profiler for GPU and CPU timing
    if (!profiler.init(device, physicalDevice, MAX_FRAMES_IN_FLIGHT)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Profiler initialization failed - profiling disabled");
        // Continue without profiling - it's optional
    }

    // Initialize water subsystems (WaterSystem, WaterDisplacement, FlowMap, Foam, SSR, TileCull, GBuffer)
    WaterSubsystems waterSubs{waterSystem, waterDisplacement, flowMapGenerator, foamBuffer, ssrSystem, waterTileCull, waterGBuffer};
    if (!RendererInit::initWaterSubsystems(waterSubs, initCtx, core.hdr.renderPass,
                                            shadowSystem, terrainSystem, terrainConfig, postProcessSystem, depthSampler)) return false;

    // Create water descriptor sets
    if (!RendererInit::createWaterDescriptorSets(waterSubs, globalBufferManager.uniformBuffers.buffers,
                                                  sizeof(UniformBufferObject), shadowSystem, terrainSystem,
                                                  postProcessSystem, depthSampler)) return false;

    // Initialize tree edit system
    if (!RendererInit::initTreeEditSystem(treeEditSystem, initCtx, core.hdr)) return false;
    treeEditSystem.updateDescriptorSets(device, globalBufferManager.uniformBuffers.buffers);

    if (!createSyncObjects()) return false;

    // Initialize debug line system
    if (!RendererInit::initDebugLineSystem(debugLineSystem, initCtx, core.hdr)) return false;
    SDL_Log("Debug line system initialized");

    // Initialize UBO builder with system references
    UBOBuilder::Systems uboSystems{};
    uboSystems.timeSystem = &timeSystem;
    uboSystems.celestialCalculator = &celestialCalculator;
    uboSystems.shadowSystem = &shadowSystem;
    uboSystems.windSystem = &windSystem;
    uboSystems.atmosphereLUTSystem = &atmosphereLUTSystem;
    uboSystems.froxelSystem = &froxelSystem;
    uboSystems.sceneManager = &sceneManager;
    uboSystems.snowMaskSystem = &snowMaskSystem;
    uboSystems.volumetricSnowSystem = &volumetricSnowSystem;
    uboSystems.cloudShadowSystem = &cloudShadowSystem;
    uboSystems.environmentSettings = &environmentSettings;
    uboBuilder.setSystems(uboSystems);

    return true;
}

void Renderer::initResizeCoordinator() {
    // Register systems with resize coordinator
    // Order matters: render targets first, then systems that depend on them, then viewport-only

    // Render targets that need full resize (device/allocator/extent)
    resizeCoordinator.registerWithResize(postProcessSystem, "PostProcessSystem", ResizePriority::RenderTarget);
    resizeCoordinator.registerWithResize(bloomSystem, "BloomSystem", ResizePriority::RenderTarget);
    resizeCoordinator.registerWithResize(froxelSystem, "FroxelSystem", ResizePriority::RenderTarget);

    // Culling systems with simple resize (extent only, but reallocates)
    resizeCoordinator.registerWithSimpleResize(hiZSystem, "HiZSystem", ResizePriority::Culling);
    resizeCoordinator.registerWithSimpleResize(ssrSystem, "SSRSystem", ResizePriority::Culling);
    resizeCoordinator.registerWithSimpleResize(waterTileCull, "WaterTileCull", ResizePriority::Culling);

    // G-buffer systems
    resizeCoordinator.registerWithSimpleResize(waterGBuffer, "WaterGBuffer", ResizePriority::GBuffer);

    // Viewport-only systems (setExtent)
    resizeCoordinator.registerWithExtent(terrainSystem, "TerrainSystem");
    resizeCoordinator.registerWithExtent(skySystem, "SkySystem");
    resizeCoordinator.registerWithExtent(waterSystem, "WaterSystem");
    resizeCoordinator.registerWithExtent(grassSystem, "GrassSystem");
    resizeCoordinator.registerWithExtent(weatherSystem, "WeatherSystem");
    resizeCoordinator.registerWithExtent(leafSystem, "LeafSystem");
    resizeCoordinator.registerWithExtent(catmullClarkSystem, "CatmullClarkSystem");
    resizeCoordinator.registerWithExtent(skinnedMeshRenderer, "SkinnedMeshRenderer");

    // Tree edit system uses updateExtent
    resizeCoordinator.registerWithUpdateExtent(treeEditSystem, "TreeEditSystem");

    // Register callback for bloom texture rebinding (needed after bloom resize)
    resizeCoordinator.registerCallback("BloomRebind",
        [this](VkDevice, VmaAllocator, VkExtent2D) {
            postProcessSystem.setBloomTexture(bloomSystem.getBloomOutput(), bloomSystem.getBloomSampler());
        },
        nullptr,
        ResizePriority::RenderTarget);

    // Register core resize handler for swapchain, depth buffer, and framebuffers
    resizeCoordinator.setCoreResizeHandler([this](VkDevice, VmaAllocator) -> VkExtent2D {
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
