#define VMA_IMPLEMENTATION
#include "Renderer.h"
#include "Camera.h"
#include "RendererSystems.h"
#include "MaterialDescriptorFactory.h"
#include "passes/ShadowPassRecorder.h"
#include "passes/HDRPassRecorder.h"
#include "InitProfiler.h"
#include "QueueSubmitDiagnostics.h"
#include "core/pipeline/FrameGraphBuilder.h"
#include "core/FrameUpdater.h"
#include "core/FrameDataBuilder.h"
#include "core/updaters/UBOUpdater.h"
#include "core/loading/AsyncSystemLoader.h"
#include "interfaces/IPlayerControl.h"
#include "UBOs.h"
#include "FrameData.h"
#include "RenderContext.h"

// Subsystem includes for render loop
// Core systems
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "ShadowSystem.h"
#include "GlobalBufferManager.h"
#include "Profiler.h"
#include "SceneManager.h"
#include "ResizeCoordinator.h"
#include "SceneBuilder.h"
#include "Mesh.h"
// Time and environment
#include "WindSystem.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"
#include "interfaces/IEnvironmentControl.h"
#include "UBOBuilder.h"
// Terrain and atmosphere
#include "TerrainSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "CloudShadowSystem.h"
#include "AtmosphereLUTSystem.h"
#include "FroxelSystem.h"
#include "SkySystem.h"
// Animation and debug
#include "SkinnedMeshRenderer.h"
#include "npc/NPCRenderer.h"
#include "npc/NPCSimulation.h"
#include "DebugLineSystem.h"
#include "HiZSystem.h"
#include "ScreenSpaceShadowSystem.h"
#include "GPUSceneBuffer.h"
#include "culling/GPUCullPass.h"
#include "interfaces/IDebugControl.h"
#include "controls/DebugControlSubsystem.h"
#include "threading/TaskScheduler.h"
// Vegetation
#include "GrassSystem.h"
#include "ScatterSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "DisplacementSystem.h"
#include "CullCommon.h"  // For extractFrustumPlanes
// Water
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "SSRSystem.h"
// Post-processing
#include "BilateralGridSystem.h"
// Geometry
#include "CatmullClarkSystem.h"
// Weather
#include "WeatherSystem.h"
#include "LeafSystem.h"
// HDR drawable registration
#include "passes/HDRDrawableAdapters.h"
#include "passes/SceneObjectsDrawable.h"
#include "passes/SkinnedCharDrawable.h"
#include "passes/DebugLinesDrawable.h"
#include "passes/WaterDrawable.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <array>
#include <limits>
#include <algorithm>
#include <numeric>
#include <chrono>

std::unique_ptr<Renderer> Renderer::create(const InitInfo& info) {
    auto instance = std::make_unique<Renderer>(ConstructToken{});

    if (info.asyncInit) {
        // Async initialization path - starts background loading
        if (!instance->initInternalAsync(info)) {
            return nullptr;
        }
    } else {
        // Synchronous initialization path (original behavior)
        if (!instance->initInternal(info)) {
            return nullptr;
        }
    }
    return instance;
}

Renderer::Renderer(ConstructToken) {}

Renderer::~Renderer() {
    cleanup();
}

bool Renderer::initInternal(const InitInfo& info) {
    INIT_PROFILE_PHASE("Renderer");

    resourcePath = info.resourcePath;
    config_ = info.config;
    progressCallback_ = info.progressCallback;

    // Helper to report progress (no-op if callback not set)
    auto reportProgress = [this](float progress, const char* phase) {
        if (progressCallback_) {
            progressCallback_(progress, phase);
        }
    };

    reportProgress(0.0f, "Initializing...");

    // Create subsystems container
    systems_ = std::make_unique<RendererSystems>();

    // Initialize Vulkan context
    // If a pre-initialized context was provided (instance or device already created),
    // take ownership and complete any remaining initialization.
    // Otherwise, create a new context and fully initialize it.
    {
        INIT_PROFILE_PHASE("VulkanContext");
        if (info.vulkanContext) {
            vulkanContext_ = std::move(const_cast<std::unique_ptr<VulkanContext>&>(info.vulkanContext));
            // Only call initDevice if device isn't already initialized
            // (LoadingRenderer may have already completed device init)
            if (!vulkanContext_->isDeviceReady()) {
                if (!vulkanContext_->initDevice(info.window)) {
                    SDL_Log("Failed to complete Vulkan device initialization");
                    return false;
                }
            }
        } else {
            vulkanContext_ = std::make_unique<VulkanContext>();
            if (!vulkanContext_->init(info.window)) {
                SDL_Log("Failed to initialize Vulkan context");
                return false;
            }
        }
    }

    // Phase 1: Core Vulkan resources (render pass, depth, framebuffers, command pool)
    reportProgress(0.05f, "Creating Vulkan resources");
    {
        INIT_PROFILE_PHASE("CoreVulkanResources");
        if (!initCoreVulkanResources()) return false;
    }

    // Initialize asset registry (after command pool is ready)
    reportProgress(0.08f, "Initializing asset registry");
    {
        INIT_PROFILE_PHASE("AssetRegistry");
        assetRegistry_.init(
            vulkanContext_->getVkDevice(),
            vulkanContext_->getVkPhysicalDevice(),
            vulkanContext_->getAllocator(),
            vulkanContext_->getCommandPool(),
            vulkanContext_->getVkGraphicsQueue());
    }

    // Phase 2: Descriptor infrastructure (layouts, pools)
    reportProgress(0.10f, "Creating descriptor infrastructure");
    {
        INIT_PROFILE_PHASE("DescriptorInfrastructure");
        if (!initDescriptorInfrastructure()) return false;
    }

    // Build shared InitContext for subsystem initialization
    // Pass pool sizes hint so subsystems can create consistent pools if needed
    InitContext initCtx = InitContext::build(
        *vulkanContext_, vulkanContext_->getCommandPool(), getDescriptorPool(),
        resourcePath, MAX_FRAMES_IN_FLIGHT, config_.descriptorPoolSizes);

    // Phase 3: All subsystems (terrain, grass, weather, snow, water, etc.)
    // This is the heaviest phase, so we pass the progress callback for finer updates
    reportProgress(0.12f, "Initializing subsystems");
    {
        INIT_PROFILE_PHASE("Subsystems");
        if (!initSubsystems(initCtx)) return false;
    }

    // Phase 4: Control subsystems (after systems are ready)
    reportProgress(0.95f, "Initializing controls");
    {
        INIT_PROFILE_PHASE("ControlSubsystems");
        initControlSubsystems();
    }

    // Phase 5: Resize coordinator registration
    reportProgress(0.96f, "Configuring resize handler");
    {
        INIT_PROFILE_PHASE("ResizeCoordinator");
        initResizeCoordinator();
    }

    // Phase 5b: Temporal system registration (for ghost frame prevention)
    {
        INIT_PROFILE_PHASE("TemporalSystems");
        initTemporalSystems();
    }

    // Initialize pass recorders (must be after systems_ is set up)
    // Note: These use stateless recording - config is passed to record() each frame
    reportProgress(0.97f, "Creating pass recorders");
    {
        INIT_PROFILE_PHASE("PassRecorders");
        shadowPassRecorder_ = std::make_unique<ShadowPassRecorder>(*systems_);
        createHDRPassRecorder();
    }
    SDL_Log("Pass recorders initialized");

    // Setup frame graph with dependencies
    reportProgress(0.99f, "Configuring frame graph");
    {
        INIT_PROFILE_PHASE("FrameGraph");
        setupFrameGraph();
    }
    SDL_Log("Frame graph configured");

    reportProgress(1.0f, "Ready");

    return true;
}

void Renderer::setupFrameGraph() {
    // Build callbacks for frame graph passes
    FrameGraphBuilder::Callbacks callbacks;
    callbacks.recordShadowPass = [this](VkCommandBuffer cmd, uint32_t frameIndex, float time, const glm::vec3& cameraPos) {
        recordShadowPass(cmd, frameIndex, time, cameraPos);
    };
    callbacks.recordHDRPass = [this](VkCommandBuffer cmd, uint32_t frameIndex, float time) {
        recordHDRPass(cmd, frameIndex, time);
    };
    callbacks.recordHDRPassWithSecondaries = [this](VkCommandBuffer cmd, uint32_t frameIndex, float time, const std::vector<vk::CommandBuffer>& secondaries) {
        recordHDRPassWithSecondaries(cmd, frameIndex, time, secondaries);
    };
    callbacks.recordHDRPassSecondarySlot = [this](VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t slot) {
        recordHDRPassSecondarySlot(cmd, frameIndex, time, slot);
    };
    callbacks.guiRenderCallback = &guiRenderCallback;

    // Build state references for frame graph passes
    FrameGraphBuilder::State state;
    state.lastSunIntensity = &lastSunIntensity;
    state.hdrPassEnabled = &hdrPassEnabled;
    state.terrainEnabled = &terrainEnabled;
    state.perfToggles = &perfToggles;
    state.framebuffers = &vulkanContext_->getFramebuffers();

    // Use FrameGraphBuilder to configure all passes and dependencies
    if (!FrameGraphBuilder::build(frameGraph_, *systems_, callbacks, state)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to build frame graph");
    }
}


// Note: initCoreVulkanResources(), initDescriptorInfrastructure(), initSubsystems(),
// and initResizeCoordinator() are implemented in RendererInitPhases.cpp

#ifdef JPH_DEBUG_RENDERER
void Renderer::updatePhysicsDebug(PhysicsWorld& physics, const glm::vec3& cameraPos) {
    if (!systems_->debugControlSubsystem().isPhysicsDebugEnabled()) return;

    // Begin debug line frame (clear previous and set frame index)
    // This is called here so physics debug lines can be collected before render()
    systems_->debugLine().beginFrame(frameExecutor_.currentFrameIndex());

    // Create debug renderer on first use (after Jolt is initialized)
    if (!systems_->physicsDebugRenderer()) {
        InitContext initCtx = InitContext::build(
            *vulkanContext_, vulkanContext_->getCommandPool(), getDescriptorPool(),
            resourcePath, MAX_FRAMES_IN_FLIGHT);
        systems_->createPhysicsDebugRenderer(initCtx, systems_->postProcess().getHDRRenderPass());
    }

    auto* debugRenderer = systems_->physicsDebugRenderer();
    if (!debugRenderer) return;

    // Begin physics debug frame
    debugRenderer->beginFrame(cameraPos);

    // Draw all physics bodies
    if (physics.getPhysicsSystem()) {
        debugRenderer->drawBodies(*physics.getPhysicsSystem());
    }

    // End frame (cleanup cached geometry)
    debugRenderer->endFrame();

    // Import collected lines into our debug line system
    systems_->debugLine().importFromPhysicsDebugRenderer(*debugRenderer);
}
#endif

void Renderer::cleanup() {
    VkDevice device = vulkanContext_->getVkDevice();
    VmaAllocator allocator = vulkanContext_->getAllocator();

    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);

        // Shutdown multi-threading infrastructure in reverse init order
        asyncTextureUploader_.shutdown();
        asyncTransferManager_.shutdown();
        threadedCommandPool_.shutdown();

        // Destroy FrameExecutor (owns TripleBuffering) before its dependencies
        frameExecutor_.destroy();

        // Destroy all subsystems via RendererSystems
        if (systems_) {
            systems_->destroy(device, allocator);
            systems_.reset();
        }

        // Clean up ScenePipeline (RAII objects must be reset while device is alive)
        scenePipeline_.reset();

        // Clean up descriptor pool
        if (descriptorPool_.has_value()) {
            descriptorPool_->destroy();
            descriptorPool_.reset();
        }

        // Note: command pool, render pass, depth resources, and framebuffers
        // are now owned by VulkanContext and cleaned up in its shutdown()
    }

    SDL_Log("calling vulkanContext_->shutdown");
    vulkanContext_->shutdown();
    SDL_Log("vulkanContext shutdown complete");
}

bool Renderer::createDescriptorSets() {
    VkDevice device = vulkanContext_->getVkDevice();

    // Create descriptor sets for all materials via MaterialRegistry
    // This replaces the hardcoded per-material descriptor set allocation
    auto& materialRegistry = systems_->scene().getSceneBuilder().getMaterialRegistry();

    // Lambda to build common bindings for a given frame (using GlobalBufferManager)
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
        // Snow and cloud shadow UBOs (bindings 10 and 11)
        common.snowUboBuffer = systems_->globalBuffers().snowBuffers.buffers[frameIndex];
        common.snowUboBufferSize = sizeof(SnowUBO);
        common.cloudShadowUboBuffer = systems_->globalBuffers().cloudShadowBuffers.buffers[frameIndex];
        common.cloudShadowUboBufferSize = sizeof(CloudShadowUBO);
        // Cloud shadow texture is added later in init() after cloudShadowSystem is initialized
        // Placeholder texture for unused PBR bindings (13-16)
        common.placeholderTextureView = systems_->scene().getSceneBuilder().getWhiteTexture()->getImageView();
        common.placeholderTextureSampler = systems_->scene().getSceneBuilder().getWhiteTexture()->getSampler();
        if (systems_->hasScreenSpaceShadow()) {
            common.screenShadowView = systems_->screenSpaceShadow()->getShadowBufferView();
            common.screenShadowSampler = systems_->screenSpaceShadow()->getShadowBufferSampler();
        }
        return common;
    };

    materialRegistry.createDescriptorSets(
        device,
        *getDescriptorPool(),
        scenePipeline_.getVkDescriptorSetLayout(),
        MAX_FRAMES_IN_FLIGHT,
        getCommonBindings);

    if (!materialRegistry.hasDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create MaterialRegistry descriptor sets");
        return false;
    }

    // Rock and Detritus descriptor sets are now owned by their respective systems
    // They are created in initPhase2 when the systems are initialized

    return true;
}

bool Renderer::render(const Camera& camera) {
    if (windowSuspended) return false;

    if (framebufferResized) {
        handleResize();
        framebufferResized = false;
    }

    FrameResult result = frameExecutor_.execute(
        [&](uint32_t imageIndex, uint32_t frameIndex) {
            return buildFrame(camera, imageIndex, frameIndex);
        });

    if (result == FrameResult::SwapchainOutOfDate ||
        result == FrameResult::SurfaceLost ||
        result == FrameResult::DeviceLost) {
        framebufferResized = true;
    }
    return result == FrameResult::Success;
}

VkCommandBuffer Renderer::buildFrame(const Camera& camera, uint32_t imageIndex, uint32_t frameIndex) {
    asyncTransferManager_.processPendingTransfers();

    // Per-frame data updates
    TimingData timing = systems_->time().update();

    UBOUpdater::Config uboConfig;
    uboConfig.showCascadeDebug = showCascadeDebug;
    uboConfig.useVolumetricSnow = useVolumetricSnow;
    uboConfig.showSnowDepthDebug = showSnowDepthDebug;
    uboConfig.shadowsEnabled = perfToggles.shadowPass;
    uboConfig.hdrEnabled = hdrEnabled;
    uboConfig.maxSnowHeight = MAX_SNOW_HEIGHT;
    uboConfig.lightCullRadius = lightCullRadius;
    uboConfig.ecsWorld = ecsWorld_;
    uboConfig.deltaTime = timing.deltaTime;
    auto uboResult = UBOUpdater::update(*systems_, frameIndex, camera, uboConfig);
    lastSunIntensity = uboResult.sunIntensity;

    SceneBuilder& sceneBuilder = systems_->scene().getSceneBuilder();
    AnimatedCharacter* character = sceneBuilder.hasCharacter() ? &sceneBuilder.getAnimatedCharacter() : nullptr;
    constexpr uint32_t PLAYER_BONE_SLOT = 0;
    systems_->skinnedMesh().updateBoneMatrices(frameIndex, PLAYER_BONE_SLOT, character);

    // Build per-frame shared state
    FrameData frame = FrameDataBuilder::buildFrameData(
        camera, *systems_, frameIndex, timing.deltaTime, timing.elapsedTime);
    lastViewProj = frame.viewProj;

    // Subsystem updates
    FrameUpdater::updateDebugLines(*systems_, frameIndex);

    VkExtent2D extent = vulkanContext_->getVkSwapchainExtent();
    FrameUpdater::SnowConfig snowConfig;
    snowConfig.maxSnowHeight = MAX_SNOW_HEIGHT;
    snowConfig.useVolumetricSnow = useVolumetricSnow;
    FrameUpdater::updateAllSystems(*systems_, frame, extent, snowConfig);

    FrameUpdater::populateGPUSceneBuffer(*systems_, frame);

    // Command buffer recording
    VkCommandBuffer cmd = vulkanContext_->getCommandBuffer(frame.frameIndex);
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.reset();
    vkCmd.begin(vk::CommandBufferBeginInfo{});

    systems_->profiler().beginGpuFrame(cmd, frame.frameIndex);

    RenderResources resources = FrameDataBuilder::buildRenderResources(
        *systems_, imageIndex, vulkanContext_->getFramebuffers(),
        vulkanContext_->getRenderPass(), {vulkanContext_->getWidth(), vulkanContext_->getHeight()},
        scenePipeline_.getGraphicsPipeline(), scenePipeline_.getPipelineLayout(),
        scenePipeline_.getDescriptorSetLayout());
    RenderContext renderCtx(cmd, frame.frameIndex, frame, resources, nullptr);

    FrameGraph::RenderContext fgCtx(vkCmd, frame.frameIndex, frame);
    fgCtx.imageIndex = imageIndex;
    fgCtx.deltaTime = frame.deltaTime;
    fgCtx.withUserData(&renderCtx)
        .withThreading(&threadedCommandPool_,
                       vk::RenderPass(systems_->postProcess().getHDRRenderPass()),
                       vk::Framebuffer(systems_->postProcess().getHDRFramebuffer()));

    frameGraph_.execute(fgCtx, &TaskScheduler::instance());

    systems_->profiler().endGpuFrame(cmd, frame.frameIndex);
    vkCmd.end();

    // Advance buffer sets for next frame (safe before submit — command buffer
    // already has current frame's buffer references baked in)
    systems_->grass().advanceBufferSet();
    systems_->weather().advanceBufferSet();
    systems_->leaf().advanceBufferSet();
    if (systems_->hasWaterTileCull()) {
        systems_->waterTileCull().endFrame(frameIndex);
    }

    return cmd;
}

void Renderer::waitIdle() {
    vulkanContext_->waitIdle();
}

void Renderer::waitForPreviousFrame() {
    frameExecutor_.waitForPreviousFrame();
}

bool Renderer::handleResize() {
    // Delegate all resize logic to the coordinator (pass {0,0} to trigger core handler)
    bool success = resizeCoordinator_->performResize(
        vulkanContext_->getVkDevice(),
        vulkanContext_->getAllocator(),
        {0, 0}
    );
    framebufferResized = false;
    return success;
}

void Renderer::notifyWindowFocusGained() {
    // When window regains focus (especially on macOS), the compositor may have
    // cached stale content. Invalidate ALL temporal history to prevent ghost frames
    // from any temporal blending systems.

    if (!windowFocusLost_) {
        // Focus wasn't lost, nothing to do
        return;
    }

    windowFocusLost_ = false;

    SDL_Log("Window focus gained - invalidating temporal history to prevent ghost frames");

    // Use the temporal system registry to reset all registered systems
    if (systems_) {
        systems_->resetAllTemporalHistory();
    }

    // Force swapchain clear on next frame to flush compositor cache
    // We set framebufferResized to trigger a full swapchain recreation
    // which includes clearing all swapchain images
    framebufferResized = true;
}

// Render pass recording helpers - pure command recording, no state mutation

void Renderer::recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime, const glm::vec3& cameraPosition) {
    // Build params for stateless recording
    ShadowPassRecorder::Params params;
    params.terrainEnabled = terrainEnabled;
    params.terrainShadows = perfToggles.terrainShadows;
    params.grassShadows = perfToggles.grassShadows;

    // Delegate to the recorder
    shadowPassRecorder_->record(cmd, frameIndex, grassTime, cameraPosition, params);
}

void Renderer::createHDRPassRecorder() {
    hdrPassRecorder_ = std::make_unique<HDRPassRecorder>(
        systems_->profiler(), systems_->postProcess());

    // Draw order constants - controls rendering sequence within the HDR pass.
    // Slot assignment groups drawables for parallel secondary command buffer recording.
    // Slot 0: geometry base, Slot 1: scene meshes, Slot 2: effects/vegetation/debug

    // Slot 0: Sky + Terrain + Catmull-Clark (geometry base)
    hdrPassRecorder_->registerDrawable(
        std::make_unique<RecordableDrawable>(systems_->sky()),
        0, 0, "HDR:Sky");

    hdrPassRecorder_->registerDrawable(
        std::make_unique<TerrainDrawable>(systems_->terrain()),
        100, 0, "HDR:Terrain");

    hdrPassRecorder_->registerDrawable(
        std::make_unique<RecordableDrawable>(systems_->catmullClark()),
        200, 0, "HDR:CatmullClark");

    // Slot 1: Scene Objects + Skinned Characters (scene meshes)
    {
        SceneObjectsDrawable::Resources sceneRes;
        sceneRes.scene = &systems_->scene();
        sceneRes.globalBuffers = &systems_->globalBuffers();
        sceneRes.shadow = &systems_->shadow();
        sceneRes.wind = &systems_->wind();
        sceneRes.ecsWorld = systems_->ecsWorld();
        sceneRes.rocks = &systems_->rocks();
        sceneRes.detritus = systems_->detritus();
        sceneRes.tree = systems_->tree();
        sceneRes.treeRenderer = systems_->treeRenderer();
        sceneRes.treeLOD = systems_->treeLOD();
        sceneRes.impostorCull = systems_->impostorCull();
        // V-buffer active = false for now — traditional scene object rendering
        // until V-buffer pipeline is fully bootstrapped (culler + fallback draws)
        sceneRes.visBufferActive = false;

        hdrPassRecorder_->registerDrawable(
            std::make_unique<SceneObjectsDrawable>(sceneRes),
            300, 1, "HDR:SceneObjects");
    }

    {
        SkinnedCharDrawable::Resources charRes;
        charRes.scene = &systems_->scene();
        charRes.skinnedMesh = &systems_->skinnedMesh();
        charRes.npcRenderer = systems_->npcRenderer();

        hdrPassRecorder_->registerDrawable(
            std::make_unique<SkinnedCharDrawable>(charRes),
            400, 1, "HDR:SkinnedChar");
    }

    // Slot 2: Grass + Water + Leaves + Weather + Debug (effects/vegetation)
    hdrPassRecorder_->registerDrawable(
        std::make_unique<AnimatedRecordableDrawable>(systems_->grass()),
        500, 2, "HDR:Grass");

    hdrPassRecorder_->registerDrawable(
        std::make_unique<WaterDrawable>(
            systems_->water(),
            systems_->hasWaterTileCull() ? &systems_->waterTileCull() : nullptr),
        600, 2, "HDR:Water");

    hdrPassRecorder_->registerDrawable(
        std::make_unique<AnimatedRecordableDrawable>(systems_->leaf()),
        700, 2, "HDR:Leaves");

    hdrPassRecorder_->registerDrawable(
        std::make_unique<AnimatedRecordableDrawable>(systems_->weather()),
        800, 2, "HDR:Weather");

    hdrPassRecorder_->registerDrawable(
        std::make_unique<DebugLinesDrawable>(systems_->debugLine(), systems_->postProcess()),
        900, 2, "HDR:DebugLines");
}

void Renderer::recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime) {
    // Build params for stateless recording
    HDRPassRecorder::Params params;
    params.terrainEnabled = terrainEnabled;
    params.sceneObjectsPipeline = scenePipeline_.getGraphicsPipelinePtr();
    params.pipelineLayout = scenePipeline_.getPipelineLayoutPtr();
    params.viewProj = lastViewProj;

    // GPU-driven rendering params
    // Note: useIndirectDraw is disabled for now as the full GPU-driven rendering path
    // requires mesh batching and proper indirect draw command generation.
    // The GPUCullPass runs to populate visibility data, but rendering uses the traditional path.
    if (systems_->hasGPUSceneBuffer() && systems_->hasGPUCullPass()) {
        params.gpuSceneBuffer = &systems_->gpuSceneBuffer();
        params.instancedPipelineLayout = scenePipeline_.getPipelineLayoutPtr();
        params.instancedPipeline = scenePipeline_.getGraphicsPipelinePtr();
        // params.useIndirectDraw = true;  // TODO: Enable when indirect draw path is fully implemented
    }

    // Delegate to the recorder
    hdrPassRecorder_->record(cmd, frameIndex, grassTime, params);
}

void Renderer::recordHDRPassWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime,
                                            const std::vector<vk::CommandBuffer>& secondaries) {
    // Build params for stateless recording
    HDRPassRecorder::Params params;
    params.terrainEnabled = terrainEnabled;
    params.sceneObjectsPipeline = scenePipeline_.getGraphicsPipelinePtr();
    params.pipelineLayout = scenePipeline_.getPipelineLayoutPtr();
    params.viewProj = lastViewProj;

    // GPU-driven rendering params
    // Note: useIndirectDraw is disabled for now as the full GPU-driven rendering path
    // requires mesh batching and proper indirect draw command generation.
    if (systems_->hasGPUSceneBuffer() && systems_->hasGPUCullPass()) {
        params.gpuSceneBuffer = &systems_->gpuSceneBuffer();
        params.instancedPipelineLayout = scenePipeline_.getPipelineLayoutPtr();
        params.instancedPipeline = scenePipeline_.getGraphicsPipelinePtr();
        // params.useIndirectDraw = true;  // TODO: Enable when indirect draw path is fully implemented
    }

    // Delegate to the recorder
    hdrPassRecorder_->recordWithSecondaries(cmd, frameIndex, grassTime, secondaries, params);
}

void Renderer::recordHDRPassSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime, uint32_t slot) {
    // Build params for stateless recording
    HDRPassRecorder::Params params;
    params.terrainEnabled = terrainEnabled;
    params.sceneObjectsPipeline = scenePipeline_.getGraphicsPipelinePtr();
    params.pipelineLayout = scenePipeline_.getPipelineLayoutPtr();
    params.viewProj = lastViewProj;

    // GPU-driven rendering params
    // Note: useIndirectDraw is disabled for now as the full GPU-driven rendering path
    // requires mesh batching and proper indirect draw command generation.
    if (systems_->hasGPUSceneBuffer() && systems_->hasGPUCullPass()) {
        params.gpuSceneBuffer = &systems_->gpuSceneBuffer();
        params.instancedPipelineLayout = scenePipeline_.getPipelineLayoutPtr();
        params.instancedPipeline = scenePipeline_.getGraphicsPipelinePtr();
        // params.useIndirectDraw = true;  // TODO: Enable when indirect draw path is fully implemented
    }

    // Delegate to the recorder
    hdrPassRecorder_->recordSecondarySlot(cmd, frameIndex, grassTime, slot, params);
}

// ===== GPU Skinning Implementation =====

bool Renderer::initSkinnedMeshRenderer() {
    SkinnedMeshRenderer::InitInfo info{};
    info.device = vulkanContext_->getVkDevice();
    info.physicalDevice = vulkanContext_->getVkPhysicalDevice();  // For dynamic UBO alignment
    info.raiiDevice = &vulkanContext_->getRaiiDevice();
    info.allocator = vulkanContext_->getAllocator();
    info.descriptorPool = getDescriptorPool();
    info.renderPass = systems_->postProcess().getHDRRenderPass();
    info.extent = vulkanContext_->getVkSwapchainExtent();
    info.shaderPath = resourcePath + "/shaders";
    info.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    info.addCommonBindings = [](DescriptorManager::LayoutBuilder& builder) {
        ScenePipeline::addCommonDescriptorBindings(builder);
    };

    auto skinnedMeshRenderer = SkinnedMeshRenderer::create(info);
    if (!skinnedMeshRenderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SkinnedMeshRenderer");
        return false;
    }
    systems_->setSkinnedMesh(std::move(skinnedMeshRenderer));

    // Create NPCRenderer (uses SkinnedMeshRenderer for draw calls)
    NPCRenderer::InitInfo npcInfo{};
    npcInfo.skinnedMeshRenderer = &systems_->skinnedMesh();
    auto npcRenderer = NPCRenderer::create(npcInfo);
    if (npcRenderer) {
        systems_->setNPCRenderer(std::move(npcRenderer));
        SDL_Log("NPCRenderer created successfully");
    }

    return true;
}

bool Renderer::createSkinnedMeshRendererDescriptorSets() {
    const auto* whiteTexture = systems_->scene().getSceneBuilder().getWhiteTexture();
    const auto* emissiveMap = systems_->scene().getSceneBuilder().getDefaultEmissiveMap();
    const auto& sceneBuilder = systems_->scene().getSceneBuilder();
    const auto& materialRegistry = sceneBuilder.getMaterialRegistry();

    // Build point and spot shadow views for all frames
    std::vector<VkImageView> pointShadowViews(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkImageView> spotShadowViews(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        pointShadowViews[i] = systems_->shadow().getPointShadowArrayView(i);
        spotShadowViews[i] = systems_->shadow().getSpotShadowArrayView(i);
    }

    // Get the player's actual material from MaterialRegistry based on their materialId
    // This fixes the race condition where player could have different material based on FBX load success
    VkImageView playerDiffuseView = whiteTexture->getImageView();
    VkSampler playerDiffuseSampler = whiteTexture->getSampler();
    VkImageView playerNormalView = whiteTexture->getImageView();
    VkSampler playerNormalSampler = whiteTexture->getSampler();

    const Renderable* playerRenderable = sceneBuilder.getRenderableForEntity(sceneBuilder.getPlayerEntity());
    if (playerRenderable) {
        MaterialId playerMaterialId = playerRenderable->materialId;
        const auto* playerMaterial = materialRegistry.getMaterial(playerMaterialId);
        if (playerMaterial) {
            if (playerMaterial->diffuse) {
                playerDiffuseView = playerMaterial->diffuse->getImageView();
                playerDiffuseSampler = playerMaterial->diffuse->getSampler();
            }
            if (playerMaterial->normal) {
                playerNormalView = playerMaterial->normal->getImageView();
                playerNormalSampler = playerMaterial->normal->getSampler();
            }
            SDL_Log("SkinnedMeshRenderer: Using player material '%s'", playerMaterial->name.c_str());
        }
    }

    SkinnedMeshRenderer::DescriptorResources resources{};
    resources.globalBufferManager = &systems_->globalBuffers();
    resources.shadowMapView = systems_->shadow().getShadowImageView();
    resources.shadowMapSampler = systems_->shadow().getShadowSampler();
    resources.emissiveMapView = emissiveMap->getImageView();
    resources.emissiveMapSampler = emissiveMap->getSampler();
    resources.pointShadowViews = &pointShadowViews;
    resources.pointShadowSampler = systems_->shadow().getPointShadowSampler();
    resources.spotShadowViews = &spotShadowViews;
    resources.spotShadowSampler = systems_->shadow().getSpotShadowSampler();
    resources.snowMaskView = systems_->snowMask().getSnowMaskView();
    resources.snowMaskSampler = systems_->snowMask().getSnowMaskSampler();
    resources.whiteTextureView = whiteTexture->getImageView();
    resources.whiteTextureSampler = whiteTexture->getSampler();
    resources.playerDiffuseView = playerDiffuseView;
    resources.playerDiffuseSampler = playerDiffuseSampler;
    resources.playerNormalView = playerNormalView;
    resources.playerNormalSampler = playerNormalSampler;

    return systems_->skinnedMesh().createDescriptorSets(resources);
}

// Resource access
DescriptorManager::Pool* Renderer::getDescriptorPool() { return descriptorPool_.has_value() ? &*descriptorPool_ : nullptr; }

// ===== Async Initialization Implementation =====

bool Renderer::initInternalAsync(const InitInfo& info) {
    INIT_PROFILE_PHASE("Renderer");

    resourcePath = info.resourcePath;
    config_ = info.config;
    progressCallback_ = info.progressCallback;

    // Helper to report progress
    auto reportProgress = [this](float progress, const char* phase) {
        if (progressCallback_) {
            progressCallback_(progress, phase);
        }
    };

    reportProgress(0.0f, "Initializing...");

    // Create subsystems container
    systems_ = std::make_unique<RendererSystems>();

    // Initialize Vulkan context (must be synchronous - needed for everything else)
    {
        INIT_PROFILE_PHASE("VulkanContext");
        if (info.vulkanContext) {
            vulkanContext_ = std::move(const_cast<std::unique_ptr<VulkanContext>&>(info.vulkanContext));
            if (!vulkanContext_->isDeviceReady()) {
                if (!vulkanContext_->initDevice(info.window)) {
                    SDL_Log("Failed to complete Vulkan device initialization");
                    return false;
                }
            }
        } else {
            vulkanContext_ = std::make_unique<VulkanContext>();
            if (!vulkanContext_->init(info.window)) {
                SDL_Log("Failed to initialize Vulkan context");
                return false;
            }
        }
    }

    // Phase 1: Core Vulkan resources (synchronous - quick)
    reportProgress(0.05f, "Creating Vulkan resources");
    {
        INIT_PROFILE_PHASE("CoreVulkanResources");
        if (!initCoreVulkanResources()) return false;
    }

    // Initialize asset registry (synchronous - quick)
    reportProgress(0.08f, "Initializing asset registry");
    {
        INIT_PROFILE_PHASE("AssetRegistry");
        assetRegistry_.init(
            vulkanContext_->getVkDevice(),
            vulkanContext_->getVkPhysicalDevice(),
            vulkanContext_->getAllocator(),
            vulkanContext_->getCommandPool(),
            vulkanContext_->getVkGraphicsQueue());
    }

    // Phase 2: Descriptor infrastructure (synchronous - quick)
    reportProgress(0.10f, "Creating descriptor infrastructure");
    {
        INIT_PROFILE_PHASE("DescriptorInfrastructure");
        if (!initDescriptorInfrastructure()) return false;
    }

    // Build InitContext for subsystem initialization and store for async access
    asyncInitContext_ = InitContext::build(
        *vulkanContext_, vulkanContext_->getCommandPool(), getDescriptorPool(),
        resourcePath, MAX_FRAMES_IN_FLIGHT, config_.descriptorPoolSizes);

    // Phase 3: Start async subsystem initialization
    reportProgress(0.12f, "Starting async subsystem loading");
    asyncInitComplete_ = false;
    asyncInitStarted_ = true;

    // Create async loader and set up tasks
    Loading::AsyncSystemLoader::InitInfo loaderInfo;
    loaderInfo.vulkanContext = vulkanContext_.get();
    loaderInfo.loadingRenderer = nullptr;  // We handle rendering separately
    loaderInfo.workerCount = 0;  // Auto-detect

    asyncLoader_ = Loading::AsyncSystemLoader::create(loaderInfo);
    if (!asyncLoader_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create AsyncSystemLoader");
        // Fall back to synchronous initialization
        if (!initSubsystems(asyncInitContext_)) return false;
        asyncInitComplete_ = true;
    } else {
        // Start async subsystem initialization
        if (!initSubsystemsAsync()) {
            return false;
        }
        asyncLoader_->start();
    }

    return true;
}

bool Renderer::pollAsyncInit() {
    if (asyncInitComplete_) {
        return !asyncInitFailed_;  // Return false if init failed
    }

    if (!asyncLoader_) {
        asyncInitComplete_ = true;
        return true;
    }

    // Poll for completed tasks
    asyncLoader_->pollCompletions();

    // Update progress callback
    if (progressCallback_) {
        auto progress = asyncLoader_->getProgress();
        // Map 0.0-1.0 to 0.12-0.95 (subsystem init range)
        float mappedProgress = 0.12f + progress.progress * 0.83f;
        progressCallback_(mappedProgress, progress.currentPhase.c_str());
    }

    // Check if all tasks are complete
    if (asyncLoader_->isComplete()) {
        if (asyncLoader_->hasError()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Async init failed: %s", asyncLoader_->getErrorMessage().c_str());
            asyncInitComplete_ = true;
            asyncInitFailed_ = true;
            return false;  // Indicate failure consistently
        }

        // Finalize initialization (quick synchronous steps)
        SDL_Log("Async subsystem loading complete, finalizing...");

        // Phase 4: Control subsystems
        if (progressCallback_) progressCallback_(0.95f, "Initializing controls");
        {
            INIT_PROFILE_PHASE("ControlSubsystems");
            initControlSubsystems();
        }

        // Phase 5: Resize coordinator
        if (progressCallback_) progressCallback_(0.96f, "Configuring resize handler");
        {
            INIT_PROFILE_PHASE("ResizeCoordinator");
            initResizeCoordinator();
        }

        // Phase 5b: Temporal systems
        {
            INIT_PROFILE_PHASE("TemporalSystems");
            initTemporalSystems();
        }

        // Initialize pass recorders
        if (progressCallback_) progressCallback_(0.97f, "Creating pass recorders");
        {
            INIT_PROFILE_PHASE("PassRecorders");
            shadowPassRecorder_ = std::make_unique<ShadowPassRecorder>(*systems_);
            createHDRPassRecorder();
        }
        SDL_Log("Pass recorders initialized");

        // Setup frame graph
        if (progressCallback_) progressCallback_(0.99f, "Configuring frame graph");
        {
            INIT_PROFILE_PHASE("FrameGraph");
            setupFrameGraph();
        }
        SDL_Log("Frame graph configured");

        if (progressCallback_) progressCallback_(1.0f, "Ready");

        // Clean up async loader
        asyncLoader_->shutdown();
        asyncLoader_.reset();

        asyncInitComplete_ = true;
        SDL_Log("Async initialization complete");
    }

    return asyncInitComplete_;
}

bool Renderer::initSubsystemsAsync() {
    // Build the shared task definitions and feed them to the async loader.
    // The tasks declare dependencies so AsyncSystemLoader can exploit parallelism.
    auto tasks = buildInitTasks(asyncInitContext_);
    for (auto& task : tasks) {
        asyncLoader_->addTask(std::move(task));
    }
    return true;
}
