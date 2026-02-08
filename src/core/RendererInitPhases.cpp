// RendererInitPhases.cpp - High-level initialization phases for Renderer
// Split from Renderer.cpp to keep file sizes manageable

#include <array>
#include <filesystem>
#include "Renderer.h"
#include "core/loading/AsyncSystemLoader.h"
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
#include "GPUSceneBuffer.h"
#include "culling/GPUCullPass.h"
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
#include "CoreResources.h"
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

    // Initialize bindless rendering if descriptor indexing is supported
    if (vulkanContext_->hasDescriptorIndexing()) {
        if (bindlessManager_.init(*vulkanContext_, BindlessManager::DEFAULT_MAX_TEXTURES, MAX_FRAMES_IN_FLIGHT)) {
            descriptorInfra_.setBindlessLayouts(
                bindlessManager_.getTextureSetLayout(),
                bindlessManager_.getMaterialSetLayout());
            SDL_Log("Bindless rendering infrastructure initialized");
        }
    }

    return true;
}

bool Renderer::initSubsystems(const InitContext& initCtx) {
    // Execute the shared task definitions synchronously in dependency order
    auto tasks = buildInitTasks(initCtx);
    for (auto& task : tasks) {
        if (task.cpuWork && !task.cpuWork()) return false;
        if (task.gpuWork && !task.gpuWork()) return false;
    }
    return true;
}

// ============================================================================
// buildInitTasks - Single source of truth for subsystem initialization
//
// Returns tasks in valid dependency order. Each task declares its dependencies
// so the AsyncSystemLoader can exploit parallelism when running async.
// The sync path simply executes them sequentially.
//
// Initialization Tiers:
//   Tier 0 (core):         PostProcess, Pipeline, SkinnedMesh, GlobalBuffers, Shadow
//   Tier 1 (terrain):      Terrain system (depends on core)
//   Tier 2a (snow_weather): Snow/weather systems (depends on core, parallel with terrain)
//   Tier 2b (scene):       Scene manager + descriptor sets (depends on terrain + snow_weather)
//   Tier 3 (vegetation):   Vegetation systems (depends on scene)
//   Tier 3b (atmosphere):  Atmosphere systems (depends on scene, parallel with vegetation)
//   Tier 4 (water):        Water systems (depends on vegetation + atmosphere)
//   Tier 5 (finalize):     Wiring, geometry, culling, debug, profiler (depends on water)
// ============================================================================
std::vector<Loading::SystemInitTask> Renderer::buildInitTasks(const InitContext& initCtx) {
    std::vector<Loading::SystemInitTask> tasks;
    const InitContext* ctxPtr = &initCtx;
    VkFormat swapchainImageFormat = static_cast<VkFormat>(vulkanContext_->getVkSwapchainImageFormat());

    // Shared constants
    const float halfTerrain = 8192.0f;
    glm::vec2 sceneOrigin(9200.0f - halfTerrain, 3000.0f - halfTerrain);

    // ========== TASK: Core Systems (Tier 0) ==========
    {
        Loading::SystemInitTask task;
        task.id = "core";
        task.displayName = "Core GPU systems";
        task.weight = 0.1f;
        task.gpuWork = [this, ctxPtr, swapchainImageFormat]() -> bool {
            // PostProcess (creates HDR render pass needed by almost everything)
            if (progressCallback_) progressCallback_(0.12f, "Post-processing systems");
            {
                INIT_PROFILE_PHASE("PostProcessing");
                auto bundle = PostProcessSystem::createWithDependencies(*ctxPtr, vulkanContext_->getRenderPass(), swapchainImageFormat);
                if (!bundle) return false;
                systems_->setPostProcess(std::move(bundle->postProcess));
                systems_->setBloom(std::move(bundle->bloom));
                systems_->setBilateralGrid(std::move(bundle->bilateralGrid));
            }

            // Graphics pipeline
            if (progressCallback_) progressCallback_(0.14f, "Graphics pipeline");
            {
                INIT_PROFILE_PHASE("GraphicsPipeline");
                if (!descriptorInfra_.createGraphicsPipeline(*vulkanContext_,
                        systems_->postProcess().getHDRRenderPass(), resourcePath)) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create graphics pipeline");
                    return false;
                }
            }

            // Skinned mesh renderer
            if (progressCallback_) progressCallback_(0.15f, "Skinned mesh renderer");
            {
                INIT_PROFILE_PHASE("SkinnedMeshRenderer");
                if (!initSkinnedMeshRenderer()) return false;
            }

            // Global buffer manager
            if (progressCallback_) progressCallback_(0.17f, "Global buffers");
            {
                INIT_PROFILE_PHASE("GlobalBufferManager");
                auto globalBuffers = GlobalBufferManager::create(vulkanContext_->getAllocator(),
                    vulkanContext_->getVkPhysicalDevice(), MAX_FRAMES_IN_FLIGHT);
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

            // Shadow system
            if (progressCallback_) progressCallback_(0.19f, "Shadow system");
            {
                INIT_PROFILE_PHASE("ShadowSystem");
                auto shadowSystem = ShadowSystem::create(*ctxPtr,
                    descriptorInfra_.getVkDescriptorSetLayout(),
                    systems_->skinnedMesh().getDescriptorSetLayout());
                if (!shadowSystem) return false;
                systems_->setShadow(std::move(shadowSystem));
            }

            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Terrain System (Tier 1) ==========
    {
        Loading::SystemInitTask task;
        task.id = "terrain";
        task.displayName = "Terrain system";
        task.dependencies = {"core"};
        task.weight = 0.15f;
        task.gpuWork = [this, ctxPtr]() -> bool {
            if (progressCallback_) progressCallback_(0.20f, "Terrain system");
            INIT_PROFILE_PHASE("TerrainSystem");

            TerrainFactory::Config terrainFactoryConfig{};
            terrainFactoryConfig.hdrRenderPass = systems_->postProcess().getHDRRenderPass();
            terrainFactoryConfig.shadowRenderPass = systems_->shadow().getShadowRenderPass();
            terrainFactoryConfig.shadowMapSize = systems_->shadow().getShadowMapSize();
            terrainFactoryConfig.resourcePath = resourcePath;

            // Yield callback keeps the window responsive during heavy terrain loading
            terrainFactoryConfig.yieldCallback = [this](float subProgress, const char* phase) {
                float overallProgress = 0.20f + subProgress * 0.08f;
                if (progressCallback_) progressCallback_(overallProgress, phase);
                SDL_PumpEvents();
            };

            auto terrainSystem = TerrainFactory::create(*ctxPtr, terrainFactoryConfig);
            if (!terrainSystem) return false;
            systems_->setTerrain(std::move(terrainSystem));
            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Snow/Weather Systems (Tier 2a - parallel with terrain) ==========
    {
        Loading::SystemInitTask task;
        task.id = "snow_weather";
        task.displayName = "Snow and weather";
        task.dependencies = {"core"};
        task.weight = 0.05f;
        task.gpuWork = [this, ctxPtr]() -> bool {
            if (progressCallback_) progressCallback_(0.28f, "Snow and weather systems");
            INIT_PROFILE_PHASE("SnowWeather");

            VkRenderPass hdrRenderPass = systems_->postProcess().getHDRRenderPass();
            SnowSystemGroup::CreateDeps snowDeps{*ctxPtr, hdrRenderPass};
            auto snowBundle = SnowSystemGroup::createAll(snowDeps);
            if (!snowBundle) return false;

            systems_->setSnowMask(std::move(snowBundle->snowMask));
            systems_->setVolumetricSnow(std::move(snowBundle->volumetricSnow));
            systems_->setWeather(std::move(snowBundle->weather));
            systems_->setLeaf(std::move(snowBundle->leaf));
            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Scene Manager (Tier 2b) ==========
    {
        Loading::SystemInitTask task;
        task.id = "scene";
        task.displayName = "Scene manager";
        task.dependencies = {"terrain", "snow_weather"};
        task.weight = 0.15f;
        task.gpuWork = [this, sceneOrigin]() -> bool {
            if (progressCallback_) progressCallback_(0.32f, "Scene manager");
            INIT_PROFILE_PHASE("SceneManager");

            SceneBuilder::InitInfo sceneInfo{};
            sceneInfo.allocator = vulkanContext_->getAllocator();
            sceneInfo.device = vulkanContext_->getVkDevice();
            sceneInfo.commandPool = vulkanContext_->getCommandPool();
            sceneInfo.graphicsQueue = vulkanContext_->getVkGraphicsQueue();
            sceneInfo.physicalDevice = vulkanContext_->getVkPhysicalDevice();
            sceneInfo.resourcePath = resourcePath;
            sceneInfo.assetRegistry = &renderingInfra_.assetRegistry();
            sceneInfo.getTerrainHeight = [this](float x, float z) {
                return systems_->terrain().getHeightAt(x, z);
            };
            sceneInfo.sceneOrigin = sceneOrigin;
            sceneInfo.deferRenderables = true;

            auto sceneManager = SceneManager::create(sceneInfo);
            if (!sceneManager) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SceneManager");
                return false;
            }
            systems_->setScene(std::move(sceneManager));

            // Initialize bindless TextureRegistry with placeholder textures
            if (bindlessManager_.isInitialized()) {
                const auto& sceneBuilder = systems_->scene().getSceneBuilder();
                const Texture* whiteTex = sceneBuilder.getWhiteTexture();
                const Texture* blackTex = sceneBuilder.getDefaultEmissiveMap();

                if (whiteTex && blackTex) {
                    textureRegistry_.init(
                        whiteTex->getImageView(), whiteTex->getSampler(),   // white placeholder
                        blackTex->getImageView(), blackTex->getSampler(),   // black placeholder
                        whiteTex->getImageView(), whiteTex->getSampler()); // normal placeholder (temporary)

                    // Connect TextureRegistry to MaterialRegistry for auto-registration
                    auto& materialRegistry = systems_->scene().getSceneBuilder().getMaterialRegistry();
                    materialRegistry.setTextureRegistry(&textureRegistry_);
                    materialRegistry.registerTexturesWithRegistry();

                    SDL_Log("TextureRegistry initialized and connected to MaterialRegistry");
                }
            }

            // Create descriptor sets (needs scene + snow systems)
            if (!createDescriptorSets()) return false;
            if (!createSkinnedMeshRendererDescriptorSets()) return false;

            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Vegetation Systems (Tier 3) ==========
    {
        Loading::SystemInitTask task;
        task.id = "vegetation";
        task.displayName = "Vegetation systems";
        task.dependencies = {"scene"};
        task.weight = 0.2f;
        task.gpuWork = [this, ctxPtr, sceneOrigin]() -> bool {
            if (progressCallback_) progressCallback_(0.45f, "Vegetation systems");
            INIT_PROFILE_PHASE("VegetationSystems");

            CoreResources core = CoreResources::collect(
                systems_->postProcess(), systems_->shadow(), systems_->terrain(), MAX_FRAMES_IN_FLIGHT);

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

            VegetationSystemGroup::CreateDeps vegDeps{
                *ctxPtr,
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

            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Atmosphere Systems (Tier 3b - parallel with vegetation) ==========
    {
        Loading::SystemInitTask task;
        task.id = "atmosphere";
        task.displayName = "Atmosphere systems";
        task.dependencies = {"scene"};
        task.weight = 0.1f;
        task.gpuWork = [this, ctxPtr]() -> bool {
            if (progressCallback_) progressCallback_(0.60f, "Atmosphere systems");
            INIT_PROFILE_PHASE("AtmosphereSubsystems");

            CoreResources core = CoreResources::collect(
                systems_->postProcess(), systems_->shadow(), systems_->terrain(), MAX_FRAMES_IN_FLIGHT);

            AtmosphereSystemGroup::CreateDeps atmosDeps{
                *ctxPtr,
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

            AtmosphereSystemGroup::wireToPostProcess(systems_->froxel(), systems_->postProcess());
            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Water Systems (Tier 4) ==========
    {
        Loading::SystemInitTask task;
        task.id = "water";
        task.displayName = "Water systems";
        task.dependencies = {"vegetation", "atmosphere"};
        task.weight = 0.1f;
        task.gpuWork = [this, ctxPtr]() -> bool {
            if (progressCallback_) progressCallback_(0.75f, "Water systems");
            INIT_PROFILE_PHASE("WaterSystems");

            CoreResources core = CoreResources::collect(
                systems_->postProcess(), systems_->shadow(), systems_->terrain(), MAX_FRAMES_IN_FLIGHT);

            WaterSystemGroup::CreateDeps waterDeps{
                *ctxPtr,
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

            // Configure water subsystems
            TerrainFactory::Config terrainFactoryConfig{};
            terrainFactoryConfig.resourcePath = resourcePath;
            TerrainConfig terrainConfig = TerrainFactory::buildTerrainConfig(terrainFactoryConfig);

            if (!WaterSystemGroup::configureSubsystems(*systems_, terrainConfig)) return false;
            if (!WaterSystemGroup::createDescriptorSets(*systems_,
                    systems_->globalBuffers().uniformBuffers.buffers,
                    sizeof(UniformBufferObject), systems_->shadow(), systems_->terrain(),
                    systems_->postProcess(), vulkanContext_->getDepthSampler())) return false;

            return true;
        };
        tasks.push_back(std::move(task));
    }

    // ========== TASK: Finalization (Tier 5) ==========
    // Wiring, geometry, culling, debug, profiler, roads, UBO builder
    {
        Loading::SystemInitTask task;
        task.id = "finalize";
        task.displayName = "Finalizing systems";
        task.dependencies = {"water"};
        task.weight = 0.15f;
        task.gpuWork = [this, ctxPtr, sceneOrigin]() -> bool {
            if (progressCallback_) progressCallback_(0.85f, "Finalizing systems");

            VkDevice device = vulkanContext_->getVkDevice();
            CoreResources core = CoreResources::collect(
                systems_->postProcess(), systems_->shadow(), systems_->terrain(), MAX_FRAMES_IN_FLIGHT);

            // Cross-system descriptor set wiring
            SystemWiring wiring(device, MAX_FRAMES_IN_FLIGHT);
            wiring.wireTerrainDescriptors(*systems_);

            // Deferred terrain objects (trees, detritus - generated on first frame)
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
                deferredConfig.allocator = vulkanContext_->getAllocator();
                deferredConfig.commandPool = vulkanContext_->getCommandPool();
                deferredConfig.graphicsQueue = vulkanContext_->getVkGraphicsQueue();
                deferredConfig.physicalDevice = vulkanContext_->getVkPhysicalDevice();
                deferredConfig.descriptorPool = descriptorInfra_.getDescriptorPool();
                deferredConfig.descriptorSetLayout = descriptorInfra_.getVkDescriptorSetLayout();
                deferredConfig.framesInFlight = MAX_FRAMES_IN_FLIGHT;

                auto deferredObjects = DeferredTerrainObjects::create(deferredConfig);
                if (deferredObjects) {
                    systems_->setDeferredTerrainObjects(std::move(deferredObjects));
                }
            }

            // Common bindings for descriptor sets
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

            // Rocks descriptor sets
            if (!systems_->rocks().createDescriptorSets(
                    device, *descriptorInfra_.getDescriptorPool(),
                    descriptorInfra_.getVkDescriptorSetLayout(),
                    MAX_FRAMES_IN_FLIGHT, getCommonBindings)) {
                return false;
            }

            if (systems_->deferredTerrainObjects()) {
                systems_->deferredTerrainObjects()->setCommonBindingsFunc(getCommonBindings);
            }

            // Wire all remaining cross-system descriptors
            wiring.wireSnowSystems(*systems_);
            wiring.wireLeafDescriptors(*systems_);
            wiring.wireWeatherDescriptors(*systems_);
            wiring.wireGrassDescriptors(*systems_);
            wiring.wireFroxelToWeather(*systems_);
            wiring.wireCloudShadowToTerrain(*systems_);
            wiring.wireCloudShadowBindings(*systems_);

            // Geometry systems
            {
                INIT_PROFILE_PHASE("GeometrySubsystems");
                GeometrySystemGroup::CreateDeps geomDeps{
                    *ctxPtr,
                    core.hdr.renderPass,
                    systems_->globalBuffers().uniformBuffers.buffers,
                    resourcePath,
                    core.terrain.getHeightAt
                };
                auto geomBundle = GeometrySystemGroup::createAll(geomDeps);
                if (!geomBundle) return false;
                systems_->setCatmullClark(std::move(geomBundle->catmullClark));
            }

            // Sky descriptor sets
            if (!systems_->sky().createDescriptorSets(
                    systems_->globalBuffers().uniformBuffers.buffers,
                    sizeof(UniformBufferObject), systems_->atmosphereLUT())) return false;

            // Hi-Z occlusion culling (optional)
            auto hiZSystem = HiZSystem::create(*ctxPtr, vulkanContext_->getDepthFormat());
            if (hiZSystem) {
                systems_->setHiZ(std::move(hiZSystem));
                systems_->hiZ().setDepthBuffer(core.hdr.depthView, vulkanContext_->getDepthSampler());
                systems_->hiZ().gatherObjects(systems_->scene().getRenderables(),
                                              systems_->rocks().getSceneObjects());
            }

            // GPU scene buffer for GPU-driven rendering
            {
                auto gpuSceneBuffer = std::make_unique<GPUSceneBuffer>();
                if (gpuSceneBuffer->init(vulkanContext_->getAllocator(), MAX_FRAMES_IN_FLIGHT)) {
                    systems_->setGPUSceneBuffer(std::move(gpuSceneBuffer));
                    SDL_Log("GPUSceneBuffer: Initialized for GPU-driven rendering");
                }
            }

            // GPU culling pass
            if (systems_->hasGPUSceneBuffer()) {
                GPUCullPass::InitInfo cullInfo{};
                cullInfo.device = device;
                cullInfo.raiiDevice = &vulkanContext_->getRaiiDevice();
                cullInfo.allocator = vulkanContext_->getAllocator();
                cullInfo.shaderPath = resourcePath + "/shaders";
                cullInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
                cullInfo.descriptorPool = descriptorInfra_.getDescriptorPool();

                auto gpuCullPass = GPUCullPass::create(cullInfo);
                if (gpuCullPass) {
                    if (systems_->hiZ().getHiZPyramidView() != VK_NULL_HANDLE) {
                        gpuCullPass->setHiZPyramid(
                            systems_->hiZ().getHiZPyramidView(),
                            systems_->hiZ().getHiZSampler());
                    }
                    const auto* whiteTexture = systems_->scene().getSceneBuilder().getWhiteTexture();
                    if (whiteTexture) {
                        gpuCullPass->setPlaceholderImage(
                            whiteTexture->getImageView(),
                            whiteTexture->getSampler());
                    }
                    systems_->setGPUCullPass(std::move(gpuCullPass));
                    SDL_Log("GPUCullPass: Initialized for frustum culling");
                }
            }

            // Profiler
            systems_->setProfiler(Profiler::create(device, vulkanContext_->getVkPhysicalDevice(), MAX_FRAMES_IN_FLIGHT));

            // Wire caustics (after water is fully initialized)
            wiring.wireCausticsToTerrain(*systems_);

            // FrameExecutor (owns sync objects / TripleBuffering)
            if (!frameExecutor_.init(vulkanContext_.get(), MAX_FRAMES_IN_FLIGHT)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize FrameExecutor");
                return false;
            }

            // Debug line system
            auto debugLineSystem = DebugLineSystem::create(*ctxPtr, core.hdr.renderPass);
            if (!debugLineSystem) return false;
            systems_->setDebugLineSystem(std::move(debugLineSystem));

            // Road/river data
            {
                std::string terrainDataPath = resourcePath + "/terrain_data";
                std::string roadsSubdir = terrainDataPath + "/roads";
                std::string roadsPath = roadsSubdir + "/roads.geojson";
                std::string roadsPathAlt = terrainDataPath + "/roads.geojson";

                if (systems_->roadData().loadFromGeoJson(roadsPath)) {
                    SDL_Log("Loaded road network from %s", roadsPath.c_str());
                } else if (systems_->roadData().loadFromGeoJson(roadsPathAlt)) {
                    SDL_Log("Loaded road network from %s", roadsPathAlt.c_str());
                }

                std::string watershedPath = terrainDataPath + "/watershed";
                ErosionLoadConfig erosionConfig{};
                erosionConfig.cacheDirectory = watershedPath;
                erosionConfig.seaLevel = 0.0f;
                if (systems_->erosionData().loadFromCache(erosionConfig)) {
                    SDL_Log("Loaded water placement data from %s", watershedPath.c_str());
                }

                auto& vis = systems_->roadRiverVis();
                vis.setWaterData(&systems_->erosionData().getWaterData());
                vis.setRoadNetwork(&systems_->roadData().getRoadNetwork());
                vis.setTerrainTileCache(systems_->terrain().getTileCache());

                RoadRiverVisConfig visConfig{};
                visConfig.showRivers = true;
                visConfig.showRoads = true;
                visConfig.coneRadius = 0.5f;
                visConfig.coneLength = 2.0f;
                visConfig.heightAboveGround = 1.0f;
                visConfig.riverConeSpacing = 50.0f;
                visConfig.roadConeSpacing = 50.0f;
                vis.setConfig(visConfig);
            }

            // UBO builder
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

            if (progressCallback_) progressCallback_(0.95f, "Systems ready");
            return true;
        };
        tasks.push_back(std::move(task));
    }

    return tasks;
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

        // Clear all swapchain images to prevent ghost frames from stale content
        // This is especially important after window restore (e.g., from screen lock)
        vulkanContext_->clearSwapchainImages();

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

void Renderer::initTemporalSystems() {
    // Register all systems that implement ITemporalSystem
    // These systems have temporal state (history buffers, ping-pong buffers, frame counters)
    // that needs to be reset when the window regains focus to prevent ghost frames

    // SSR - has temporal filtering with 90% previous frame blend
    systems_->registerTemporalSystem(&systems_->ssr());

    // Froxel - has temporal reprojection for volumetric fog
    if (systems_->hasFroxel()) {
        systems_->registerTemporalSystem(&systems_->froxel());
    }

    // Water systems with temporal state
    systems_->registerTemporalSystem(&systems_->foam());
    systems_->registerTemporalSystem(&systems_->waterDisplacement());

    // Vegetation systems with temporal state
    if (systems_->impostorCull()) {
        systems_->registerTemporalSystem(systems_->impostorCull());
    }

    SDL_Log("Registered %zu temporal systems for ghost frame prevention",
            systems_->getTemporalSystemCount());
}
