#define VMA_IMPLEMENTATION
#include "Renderer.h"
#include "RendererSystems.h"
#include "MaterialDescriptorFactory.h"
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
// System groups for async initialization
#include "SnowSystemGroup.h"
#include "VegetationSystemGroup.h"
#include "AtmosphereSystemGroup.h"
#include "WaterSystemGroup.h"
#include "GeometrySystemGroup.h"
// Additional systems for async init
#include "CoreResources.h"
#include "SystemWiring.h"
#include "DeferredTerrainObjects.h"
#include "TerrainFactory.h"
#include "ErosionDataLoader.h"
#include "RoadNetworkLoader.h"
#include "RoadRiverVisualization.h"
#include "ScatterSystemFactory.h"

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

    // Initialize asset registry via RenderingInfrastructure (after command pool is ready)
    reportProgress(0.08f, "Initializing asset registry");
    {
        INIT_PROFILE_PHASE("AssetRegistry");
        renderingInfra_.initAssetRegistry(
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
        *vulkanContext_, vulkanContext_->getCommandPool(), descriptorInfra_.getDescriptorPool(),
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

    // Initialize pass recorders (must be after systems_ is set up)
    // Note: These use stateless recording - config is passed to record() each frame
    reportProgress(0.97f, "Creating pass recorders");
    {
        INIT_PROFILE_PHASE("PassRecorders");
        shadowPassRecorder_ = std::make_unique<ShadowPassRecorder>(*systems_);
        hdrPassRecorder_ = std::make_unique<HDRPassRecorder>(*systems_);
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
    if (!FrameGraphBuilder::build(renderingInfra_.frameGraph(), *systems_, callbacks, state)) {
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
    systems_->debugLine().beginFrame(frameSync_.currentIndex());

    // Create debug renderer on first use (after Jolt is initialized)
    if (!systems_->physicsDebugRenderer()) {
        InitContext initCtx = InitContext::build(
            *vulkanContext_, vulkanContext_->getCommandPool(), descriptorInfra_.getDescriptorPool(),
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

        // Shutdown multi-threading infrastructure via RenderingInfrastructure
        renderingInfra_.shutdown();

        // Destroy RendererCore before its dependencies
        rendererCore_.destroy();

        // RAII handles cleanup of sync objects via TripleBuffering
        frameSync_.destroy();

        // Destroy all subsystems via RendererSystems
        if (systems_) {
            systems_->destroy(device, allocator);
            systems_.reset();
        }

        // Clean up descriptor infrastructure (pool, layouts, pipeline)
        descriptorInfra_.cleanup();

        // Note: command pool, render pass, depth resources, and framebuffers
        // are now owned by VulkanContext and cleaned up in its shutdown()
    }

    SDL_Log("calling vulkanContext_->shutdown");
    vulkanContext_->shutdown();
    SDL_Log("vulkanContext shutdown complete");
}

bool Renderer::createSyncObjects() {
    return frameSync_.init(vulkanContext_->getRaiiDevice(), MAX_FRAMES_IN_FLIGHT);
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
        return common;
    };

    materialRegistry.createDescriptorSets(
        device,
        *descriptorInfra_.getDescriptorPool(),
        descriptorInfra_.getVkDescriptorSetLayout(),
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
    // Skip rendering if window is suspended (e.g., macOS screen lock)
    if (windowSuspended) {
        return false;
    }

    VkDevice device = vulkanContext_->getVkDevice();
    VkSwapchainKHR swapchain = vulkanContext_->getVkSwapchain();
    VkQueue graphicsQueue = vulkanContext_->getVkGraphicsQueue();
    VkQueue presentQueue = vulkanContext_->getVkPresentQueue();

    // Handle pending resize before acquiring next image
    if (framebufferResized) {
        handleResize();
        swapchain = vulkanContext_->getVkSwapchain();  // Update after resize
        framebufferResized = false;
    }

    // Skip rendering if window is minimized
    VkExtent2D extent = vulkanContext_->getVkSwapchainExtent();
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    // Begin CPU profiling for this frame (must be before any CPU zones)
    systems_->profiler().beginCpuFrame();

    // Reset queue submit diagnostics for this frame
    auto& qsDiag = systems_->profiler().getQueueSubmitDiagnostics();
    qsDiag.reset();
    qsDiag.validationLayersEnabled = vulkanContext_->hasValidationLayers();

    // Frame synchronization - use non-blocking check first to avoid unnecessary waits
    // With triple buffering, the fence is often already signaled
    systems_->profiler().beginCpuZone("Wait:FenceSync");

    // Track fence status for diagnostics
    qsDiag.fenceWasAlreadySignaled = frameSync_.isCurrentFenceSignaled();
    auto fenceStart = std::chrono::high_resolution_clock::now();
    frameSync_.waitForCurrentFrameIfNeeded();
    auto fenceEnd = std::chrono::high_resolution_clock::now();
    qsDiag.fenceWaitTimeMs = std::chrono::duration<float, std::milli>(fenceEnd - fenceStart).count();

    systems_->profiler().endCpuZone("Wait:FenceSync");

    systems_->profiler().beginCpuZone("Wait:AcquireImage");
    auto acquireStart = std::chrono::high_resolution_clock::now();
    uint32_t imageIndex;
    // Use finite timeout (100ms) to prevent freezing when surface becomes unavailable
    // (e.g., macOS screen lock). This allows the event loop to continue processing.
    constexpr uint64_t acquireTimeoutNs = 100'000'000; // 100ms in nanoseconds
    VkResult result = vkAcquireNextImageKHR(device, swapchain, acquireTimeoutNs,
                                            frameSync_.currentImageAvailableSemaphore(), VK_NULL_HANDLE, &imageIndex);
    auto acquireEnd = std::chrono::high_resolution_clock::now();
    qsDiag.acquireImageTimeMs = std::chrono::duration<float, std::milli>(acquireEnd - acquireStart).count();
    systems_->profiler().endCpuZone("Wait:AcquireImage");

    if (result == VK_TIMEOUT || result == VK_NOT_READY) {
        // Timeout acquiring image - surface may be unavailable (e.g., macOS screen lock)
        // Return gracefully to allow event loop to continue processing
        return false;
    } else if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        handleResize();
        return false;
    } else if (result == VK_ERROR_SURFACE_LOST_KHR) {
        // Surface lost - can happen on macOS when screen locks/unlocks
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Surface lost, will recreate on next frame");
        framebufferResized = true;
        return false;
    } else if (result == VK_ERROR_DEVICE_LOST) {
        // Device lost - critical error on macOS lock/unlock
        // Log error but return gracefully - app can attempt recovery on next frame
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan device lost - attempting recovery");
        framebufferResized = true;
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to acquire swapchain image: %d", result);
        return false;
    }

    frameSync_.resetCurrentFence();

    // Process completed async transfers (textures, buffers uploaded via AsyncTransferManager)
    // Must be called after fence wait to safely reuse staging buffers
    renderingInfra_.processPendingTransfers();

    // Update time system (frame timing and day/night cycle)
    TimingData timing = systems_->time().update();

    // Begin CPU profiling for this frame
    systems_->profiler().beginCpuZone("UniformUpdates");

    // Track UBO bandwidth
    CommandCounter bandwidthCounter(&qsDiag);

    // Update uniform buffer data via UBOUpdater
    {
        systems_->profiler().beginCpuZone("UniformUpdates:UBO");
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
        auto uboResult = UBOUpdater::update(*systems_, frameSync_.currentIndex(), camera, uboConfig);
        lastSunIntensity = uboResult.sunIntensity;
        // Track bandwidth: main UBO + dynamic UBO copy + snow + cloudShadow + lights
        bandwidthCounter.recordUboUpdate(sizeof(UniformBufferObject) * 2);  // regular + dynamic
        bandwidthCounter.recordUboUpdate(sizeof(SnowUBO));
        bandwidthCounter.recordUboUpdate(sizeof(CloudShadowUBO));
        bandwidthCounter.recordSsboUpdate(sizeof(LightBuffer));
        systems_->profiler().endCpuZone("UniformUpdates:UBO");
    }

    // Update bone matrices for GPU skinning (player uses slot 0)
    {
        systems_->profiler().beginCpuZone("UniformUpdates:Bones");
        SceneBuilder& sceneBuilder = systems_->scene().getSceneBuilder();
        AnimatedCharacter* character = sceneBuilder.hasCharacter() ? &sceneBuilder.getAnimatedCharacter() : nullptr;
        // Slot 0 is reserved for the player character
        constexpr uint32_t PLAYER_BONE_SLOT = 0;
        systems_->skinnedMesh().updateBoneMatrices(frameSync_.currentIndex(), PLAYER_BONE_SLOT, character);
        // Track bone SSBO bandwidth (128 bones * mat4)
        bandwidthCounter.recordSsboUpdate(128 * sizeof(glm::mat4));
        systems_->profiler().endCpuZone("UniformUpdates:Bones");
    }

    systems_->profiler().endCpuZone("UniformUpdates");

    // Build per-frame shared state
    FrameData frame = FrameDataBuilder::buildFrameData(
        camera, *systems_, frameSync_.currentIndex(), timing.deltaTime, timing.elapsedTime);

    // Cache view-projection for debug rendering
    lastViewProj = frame.viewProj;

    // Begin debug line frame if not already started by physics debug
    // Physics debug calls beginFrame before render() if enabled
    // Only call beginFrame if we haven't collected lines yet (no physics debug this frame)
    if (!systems_->debugLine().hasLines()) {
        systems_->debugLine().beginFrame(frameSync_.currentIndex());
    }

    // Add road/river visualization to debug lines (delegated to DebugControlSubsystem)
    systems_->debugControlSubsystem().updateRoadRiverVisualization();

    // Upload debug lines if any are present
    if (systems_->debugLine().hasLines()) {
        systems_->debugLine().uploadLines();
    }

    // Update all subsystems (wind, grass, weather, terrain, snow, trees, water, etc.)
    FrameUpdater::SnowConfig snowConfig;
    snowConfig.maxSnowHeight = MAX_SNOW_HEIGHT;
    snowConfig.useVolumetricSnow = useVolumetricSnow;
    FrameUpdater::updateAllSystems(*systems_, frame, extent, snowConfig);

    // Populate GPU scene buffer for GPU-driven rendering
    if (systems_->hasGPUSceneBuffer()) {
        systems_->profiler().beginCpuZone("GPUSceneBuffer");
        GPUSceneBuffer& sceneBuffer = systems_->gpuSceneBuffer();
        sceneBuffer.beginFrame(frame.frameIndex);

        // Add all static scene objects to the GPU buffer
        const auto& sceneObjects = systems_->scene().getRenderables();
        SceneBuilder& sceneBuilder = systems_->scene().getSceneBuilder();
        size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
        bool hasCharacter = sceneBuilder.hasCharacter();
        const NPCSimulation* npcSim = sceneBuilder.getNPCSimulation();

        for (size_t i = 0; i < sceneObjects.size(); ++i) {
            // Skip player character (rendered with GPU skinning)
            if (hasCharacter && i == playerIndex) continue;

            // Skip NPC characters (rendered with GPU skinning)
            if (npcSim) {
                bool isNPC = false;
                const auto& npcData = npcSim->getData();
                for (size_t npcIdx = 0; npcIdx < npcData.count(); ++npcIdx) {
                    if (i == npcData.renderableIndices[npcIdx]) {
                        isNPC = true;
                        break;
                    }
                }
                if (isNPC) continue;
            }

            sceneBuffer.addObject(sceneObjects[i]);
        }

        sceneBuffer.finalize();
        systems_->profiler().endCpuZone("GPUSceneBuffer");
    }

    // Begin command buffer recording
    systems_->profiler().beginCpuZone("CmdBufferRecord");
    auto recordStart = std::chrono::high_resolution_clock::now();

    VkCommandBuffer cmd = vulkanContext_->getCommandBuffer(frame.frameIndex);
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.reset();
    vkCmd.begin(vk::CommandBufferBeginInfo{});

    // Get reference to diagnostics for command counting
    auto& cmdDiag = systems_->profiler().getQueueSubmitDiagnostics();

    // Set global diagnostics for subsystems that don't have direct access
    ScopedDiagnostics scopedDiag(&cmdDiag);

    // Begin command capture if requested
    auto& cmdCapture = systems_->profiler().getCommandCapture();
    cmdCapture.beginFrame(systems_->profiler().getFrameNumber());

    // Begin GPU profiling frame
    systems_->profiler().beginGpuFrame(cmd, frame.frameIndex);

    // Build render resources and context for frame graph passes
    RenderResources resources = FrameDataBuilder::buildRenderResources(
        *systems_, imageIndex, vulkanContext_->getFramebuffers(),
        vulkanContext_->getRenderPass(), {vulkanContext_->getWidth(), vulkanContext_->getHeight()},
        descriptorInfra_.getGraphicsPipeline(), descriptorInfra_.getPipelineLayout(),
        descriptorInfra_.getDescriptorSetLayout());
    RenderContext ctx(cmd, frame.frameIndex, frame, resources, &cmdDiag);

    // Execute frame graph - dependency-driven scheduling with parallel execution
    FrameGraph::RenderContext fgCtx(vkCmd, frame.frameIndex, frame);
    fgCtx.imageIndex = imageIndex;
    fgCtx.deltaTime = frame.deltaTime;
    fgCtx.withUserData(&ctx)  // Pass full RenderContext to passes
        .withThreading(&renderingInfra_.threadedCommandPool(),
                       vk::RenderPass(systems_->postProcess().getHDRRenderPass()),
                       vk::Framebuffer(systems_->postProcess().getHDRFramebuffer()))
        .withDiagnostics(&cmdDiag);

    // Execute all passes in dependency order
    // TaskScheduler enables parallel execution of independent passes
    renderingInfra_.frameGraph().execute(fgCtx, &TaskScheduler::instance());

    // End GPU profiling frame
    systems_->profiler().endGpuFrame(cmd, frame.frameIndex);

    // End command capture
    cmdCapture.endFrame();

    vkCmd.end();

    auto recordEnd = std::chrono::high_resolution_clock::now();
    cmdDiag.commandRecordTimeMs = std::chrono::duration<float, std::milli>(recordEnd - recordStart).count();

    systems_->profiler().endCpuZone("CmdBufferRecord");

    // Queue submission with timeline semaphore for non-blocking completion tracking
    systems_->profiler().beginCpuZone("QueueSubmit");

    // Binary semaphores for swapchain synchronization
    vk::Semaphore waitSemaphores[] = {frameSync_.currentImageAvailableSemaphore()};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};

    // Signal both render finished (binary, for present) and timeline (for frame sync)
    vk::Semaphore signalSemaphores[] = {
        frameSync_.currentRenderFinishedSemaphore(),
        frameSync_.frameTimelineSemaphore()
    };

    // Get next timeline value to signal for this frame
    uint64_t timelineSignalValue = frameSync_.nextFrameSignalValue();

    // Timeline semaphore submit info (Vulkan 1.2)
    // Wait semaphores: imageAvailable is binary (value ignored, use 0)
    // Signal semaphores: renderFinished is binary (0), timeline gets the new value
    uint64_t waitValues[] = {0};  // Binary semaphore, value ignored
    uint64_t signalValues[] = {0, timelineSignalValue};  // Binary, then timeline

    auto timelineInfo = vk::TimelineSemaphoreSubmitInfo{}
        .setWaitSemaphoreValueCount(1)
        .setPWaitSemaphoreValues(waitValues)
        .setSignalSemaphoreValueCount(2)
        .setPSignalSemaphoreValues(signalValues);

    auto submitInfo = vk::SubmitInfo{}
        .setPNext(&timelineInfo)
        .setWaitSemaphores(waitSemaphores)
        .setWaitDstStageMask(waitStages)
        .setCommandBuffers(vkCmd)
        .setSignalSemaphores(signalSemaphores);

    try {
        // Track queue submit time for diagnostics
        auto submitStart = std::chrono::high_resolution_clock::now();
        // No fence needed - timeline semaphore handles frame completion tracking
        vk::Queue(graphicsQueue).submit(submitInfo, nullptr);
        auto submitEnd = std::chrono::high_resolution_clock::now();
        systems_->profiler().getQueueSubmitDiagnostics().queueSubmitTimeMs =
            std::chrono::duration<float, std::milli>(submitEnd - submitStart).count();
    } catch (const vk::DeviceLostError&) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Device lost during queue submit");
        systems_->profiler().endCpuZone("QueueSubmit");
        framebufferResized = true;
        return false;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to submit draw command buffer: %s", e.what());
        systems_->profiler().endCpuZone("QueueSubmit");
        return false;
    }
    systems_->profiler().endCpuZone("QueueSubmit");

    // Present
    systems_->profiler().beginCpuZone("Wait:Present");

    vk::SwapchainKHR swapChains[] = {swapchain};

    auto presentInfo = vk::PresentInfoKHR{}
        .setWaitSemaphores(signalSemaphores)
        .setSwapchains(swapChains)
        .setImageIndices(imageIndex);

    auto presentStart = std::chrono::high_resolution_clock::now();
    try {
        auto presentResult = vk::Queue(presentQueue).presentKHR(presentInfo);
        if (presentResult == vk::Result::eSuboptimalKHR) {
            framebufferResized = true;
        }
    } catch (const vk::OutOfDateKHRError&) {
        framebufferResized = true;
    } catch (const vk::SurfaceLostKHRError&) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Surface lost during present, will recover");
        framebufferResized = true;
    } catch (const vk::DeviceLostError&) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Device lost during present, will recover");
        framebufferResized = true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to present swapchain image: %s", e.what());
    }
    auto presentEnd = std::chrono::high_resolution_clock::now();
    cmdDiag.presentTimeMs = std::chrono::duration<float, std::milli>(presentEnd - presentStart).count();

    systems_->profiler().endCpuZone("Wait:Present");

    // Advance grass double-buffer sets after frame submission
    // This swaps compute/render buffer sets so next frame can overlap:
    // - Next frame's compute writes to what was the render set
    // - Next frame's render reads from what was the compute set (now contains fresh data)
    systems_->grass().advanceBufferSet();
    systems_->weather().advanceBufferSet();
    systems_->leaf().advanceBufferSet();

    // Update water tile cull visibility tracking (uses absolute frame counter)
    if (systems_->hasWaterTileCull()) {
        systems_->waterTileCull().endFrame(frameSync_.currentIndex());
    }

    frameSync_.advance();

    // End CPU profiling for this frame
    systems_->profiler().endCpuFrame();

    // Advance frame counter (handles auto-capture for flamegraphs)
    systems_->profiler().advanceFrame();

    return true;
}

void Renderer::waitIdle() {
    vulkanContext_->waitIdle();
}

void Renderer::waitForPreviousFrame() {
    // Wait for the previous frame's fence to ensure GPU is done with resources
    // we might be about to destroy/update.
    //
    // With triple buffering (MAX_FRAMES_IN_FLIGHT=3):
    // - Frame N uses fence[N % 3]
    // - Before updating meshes for frame N, we need frame N-1's GPU work complete
    // - Previous frame's fence is fence[(N-1) % 3]
    //
    // This prevents race conditions where we destroy mesh buffers while the GPU
    // is still reading them from the previous frame's commands.
    frameSync_.waitForPreviousFrame();
}

bool Renderer::handleResize() {
    // Delegate all resize logic to the coordinator (pass {0,0} to trigger core handler)
    bool success = systems_->resizeCoordinator().performResize(
        vulkanContext_->getVkDevice(),
        vulkanContext_->getAllocator(),
        {0, 0}
    );
    framebufferResized = false;
    return success;
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

void Renderer::recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime) {
    // Build params for stateless recording
    HDRPassRecorder::Params params;
    params.terrainEnabled = terrainEnabled;
    params.sceneObjectsPipeline = descriptorInfra_.getGraphicsPipelinePtr();
    params.pipelineLayout = descriptorInfra_.getPipelineLayoutPtr();
    params.viewProj = lastViewProj;

    // GPU-driven rendering params
    // Note: useIndirectDraw is disabled for now as the full GPU-driven rendering path
    // requires mesh batching and proper indirect draw command generation.
    // The GPUCullPass runs to populate visibility data, but rendering uses the traditional path.
    if (systems_->hasGPUSceneBuffer() && systems_->hasGPUCullPass()) {
        params.gpuSceneBuffer = &systems_->gpuSceneBuffer();
        params.instancedPipelineLayout = descriptorInfra_.getPipelineLayoutPtr();
        params.instancedPipeline = descriptorInfra_.getGraphicsPipelinePtr();
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
    params.sceneObjectsPipeline = descriptorInfra_.getGraphicsPipelinePtr();
    params.pipelineLayout = descriptorInfra_.getPipelineLayoutPtr();
    params.viewProj = lastViewProj;

    // GPU-driven rendering params
    // Note: useIndirectDraw is disabled for now as the full GPU-driven rendering path
    // requires mesh batching and proper indirect draw command generation.
    if (systems_->hasGPUSceneBuffer() && systems_->hasGPUCullPass()) {
        params.gpuSceneBuffer = &systems_->gpuSceneBuffer();
        params.instancedPipelineLayout = descriptorInfra_.getPipelineLayoutPtr();
        params.instancedPipeline = descriptorInfra_.getGraphicsPipelinePtr();
        // params.useIndirectDraw = true;  // TODO: Enable when indirect draw path is fully implemented
    }

    // Delegate to the recorder
    hdrPassRecorder_->recordWithSecondaries(cmd, frameIndex, grassTime, secondaries, params);
}

void Renderer::recordHDRPassSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime, uint32_t slot) {
    // Build params for stateless recording
    HDRPassRecorder::Params params;
    params.terrainEnabled = terrainEnabled;
    params.sceneObjectsPipeline = descriptorInfra_.getGraphicsPipelinePtr();
    params.pipelineLayout = descriptorInfra_.getPipelineLayoutPtr();
    params.viewProj = lastViewProj;

    // GPU-driven rendering params
    // Note: useIndirectDraw is disabled for now as the full GPU-driven rendering path
    // requires mesh batching and proper indirect draw command generation.
    if (systems_->hasGPUSceneBuffer() && systems_->hasGPUCullPass()) {
        params.gpuSceneBuffer = &systems_->gpuSceneBuffer();
        params.instancedPipelineLayout = descriptorInfra_.getPipelineLayoutPtr();
        params.instancedPipeline = descriptorInfra_.getGraphicsPipelinePtr();
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
    info.descriptorPool = descriptorInfra_.getDescriptorPool();
    info.renderPass = systems_->postProcess().getHDRRenderPass();
    info.extent = vulkanContext_->getVkSwapchainExtent();
    info.shaderPath = resourcePath + "/shaders";
    info.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    info.addCommonBindings = [](DescriptorManager::LayoutBuilder& builder) {
        DescriptorInfrastructure::addCommonDescriptorBindings(builder);
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

    const auto& sceneObjects = sceneBuilder.getRenderables();
    size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
    if (playerIndex < sceneObjects.size()) {
        MaterialId playerMaterialId = sceneObjects[playerIndex].materialId;
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
DescriptorManager::Pool* Renderer::getDescriptorPool() { return descriptorInfra_.getDescriptorPool(); }

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
        renderingInfra_.initAssetRegistry(
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
        *vulkanContext_, vulkanContext_->getCommandPool(), descriptorInfra_.getDescriptorPool(),
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
        return true;  // Already complete
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
            return false;  // Indicate failure
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

        // Initialize pass recorders
        if (progressCallback_) progressCallback_(0.97f, "Creating pass recorders");
        {
            INIT_PROFILE_PHASE("PassRecorders");
            shadowPassRecorder_ = std::make_unique<ShadowPassRecorder>(*systems_);
            hdrPassRecorder_ = std::make_unique<HDRPassRecorder>(*systems_);
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
    // This method sets up async tasks for heavy subsystem initialization
    // Tasks declare dependencies to ensure correct initialization order
    //
    // Initialization Tiers:
    // Tier 0 (Core): PostProcess, Pipeline, SkinnedMesh, GlobalBuffers, Shadow
    //   - Must be synchronous (GPU-heavy, provides render passes)
    // Tier 1: Terrain
    //   - Depends on Tier 0 (needs HDR/shadow render passes)
    //   - Heavy: heightmap loading, tile generation
    // Tier 2a: Scene
    //   - Depends on Terrain (needs height queries)
    //   - Heavy: texture loading, mesh loading
    // Tier 2b: Snow/Weather (parallel with Scene)
    //   - Depends on Tier 0 only
    // Tier 3: Vegetation
    //   - Depends on Terrain (needs height queries)
    //   - Heavy: tree generation, rock mesh generation
    // Tier 4: Atmosphere (parallel with Vegetation)
    //   - Depends on Tier 0 only
    // Tier 5: Water, Geometry
    //   - Depends on Terrain
    // Tier 6: Wiring and finalization

    VkDevice device = vulkanContext_->getVkDevice();
    VmaAllocator allocator = vulkanContext_->getAllocator();
    VkPhysicalDevice physicalDevice = vulkanContext_->getVkPhysicalDevice();
    VkQueue graphicsQueue = vulkanContext_->getVkGraphicsQueue();
    VkFormat swapchainImageFormat = static_cast<VkFormat>(vulkanContext_->getVkSwapchainImageFormat());

    // Use member asyncInitContext_ - it persists for the lifetime of async tasks
    const InitContext* ctxPtr = &asyncInitContext_;

    // ========== TASK: Core Systems (Tier 0) ==========
    // Must run first - creates render passes and core GPU resources
    {
        Loading::SystemInitTask task;
        task.id = "core";
        task.displayName = "Core GPU systems";
        task.weight = 0.1f;
        task.cpuWork = nullptr;  // All GPU work
        task.gpuWork = [this, ctxPtr, swapchainImageFormat]() -> bool {
            auto reportProgress = [this](float p, const char* phase) {
                if (progressCallback_) progressCallback_(0.12f + p * 0.08f, phase);
            };

            // PostProcess (creates HDR render pass - needed by almost everything)
            reportProgress(0.0f, "Post-processing systems");
            {
                INIT_PROFILE_PHASE("PostProcessing");
                auto bundle = PostProcessSystem::createWithDependencies(*ctxPtr, vulkanContext_->getRenderPass(), swapchainImageFormat);
                if (!bundle) return false;
                systems_->setPostProcess(std::move(bundle->postProcess));
                systems_->setBloom(std::move(bundle->bloom));
                systems_->setBilateralGrid(std::move(bundle->bilateralGrid));
            }

            // Graphics pipeline
            reportProgress(0.2f, "Graphics pipeline");
            {
                INIT_PROFILE_PHASE("GraphicsPipeline");
                if (!descriptorInfra_.createGraphicsPipeline(*vulkanContext_,
                        systems_->postProcess().getHDRRenderPass(), resourcePath)) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create graphics pipeline");
                    return false;
                }
            }

            // Skinned mesh renderer
            reportProgress(0.4f, "Skinned mesh renderer");
            {
                INIT_PROFILE_PHASE("SkinnedMeshRenderer");
                if (!initSkinnedMeshRenderer()) return false;
            }

            // Global buffer manager
            reportProgress(0.6f, "Global buffers");
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

            // Initialize light buffers
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                LightBuffer emptyBuffer{};
                emptyBuffer.lightCount = glm::uvec4(0, 0, 0, 0);
                systems_->globalBuffers().updateLightBuffer(i, emptyBuffer);
            }

            // Shadow system
            reportProgress(0.8f, "Shadow system");
            {
                INIT_PROFILE_PHASE("ShadowSystem");
                auto shadowSystem = ShadowSystem::create(*ctxPtr,
                    descriptorInfra_.getVkDescriptorSetLayout(),
                    systems_->skinnedMesh().getDescriptorSetLayout());
                if (!shadowSystem) return false;
                systems_->setShadow(std::move(shadowSystem));
            }

            reportProgress(1.0f, "Core systems ready");
            return true;
        };
        asyncLoader_->addTask(std::move(task));
    }

    // ========== TASK: Terrain System (Tier 1) ==========
    // Heavy: heightmap loading, tile cache initialization
    {
        Loading::SystemInitTask task;
        task.id = "terrain";
        task.displayName = "Terrain system";
        task.dependencies = {"core"};
        task.weight = 0.15f;
        task.cpuWork = nullptr;  // TerrainFactory handles internal threading
        task.gpuWork = [this, ctxPtr]() -> bool {
            if (progressCallback_) progressCallback_(0.20f, "Terrain system");
            INIT_PROFILE_PHASE("TerrainSystem");

            TerrainFactory::Config terrainFactoryConfig{};
            terrainFactoryConfig.hdrRenderPass = systems_->postProcess().getHDRRenderPass();
            terrainFactoryConfig.shadowRenderPass = systems_->shadow().getShadowRenderPass();
            terrainFactoryConfig.shadowMapSize = systems_->shadow().getShadowMapSize();
            terrainFactoryConfig.resourcePath = resourcePath;

            // Provide yield callback to keep loading screen responsive during terrain init
            terrainFactoryConfig.yieldCallback = [this](float subProgress, const char* phase) {
                // Map sub-progress (0-1) to terrain's portion of overall progress (0.20-0.28)
                float overallProgress = 0.20f + subProgress * 0.08f;
                if (progressCallback_) {
                    progressCallback_(overallProgress, phase);
                }
                // Pump events to keep window responsive
                SDL_PumpEvents();
            };

            auto terrainSystem = TerrainFactory::create(*ctxPtr, terrainFactoryConfig);
            if (!terrainSystem) return false;
            systems_->setTerrain(std::move(terrainSystem));
            return true;
        };
        asyncLoader_->addTask(std::move(task));
    }

    // ========== TASK: Snow/Weather Systems (Tier 2b - parallel with scene) ==========
    {
        Loading::SystemInitTask task;
        task.id = "snow_weather";
        task.displayName = "Snow and weather";
        task.dependencies = {"core"};
        task.weight = 0.05f;
        task.cpuWork = nullptr;
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
        asyncLoader_->addTask(std::move(task));
    }

    // ========== TASK: Scene Manager (Tier 2a) ==========
    // Heavy: texture loading, mesh loading, material setup
    {
        Loading::SystemInitTask task;
        task.id = "scene";
        task.displayName = "Scene manager";
        task.dependencies = {"terrain", "snow_weather"};
        task.weight = 0.15f;
        task.cpuWork = nullptr;  // SceneManager handles internal asset loading
        task.gpuWork = [this, ctxPtr]() -> bool {
            if (progressCallback_) progressCallback_(0.32f, "Scene manager");
            INIT_PROFILE_PHASE("SceneManager");

            const float halfTerrain = 8192.0f;
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
            sceneInfo.sceneOrigin = glm::vec2(9200.0f - halfTerrain, 3000.0f - halfTerrain);
            sceneInfo.deferRenderables = true;

            auto sceneManager = SceneManager::create(sceneInfo);
            if (!sceneManager) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SceneManager");
                return false;
            }
            systems_->setScene(std::move(sceneManager));

            // Create descriptor sets (needs scene and snow)
            if (!createDescriptorSets()) return false;
            if (!createSkinnedMeshRendererDescriptorSets()) return false;

            return true;
        };
        asyncLoader_->addTask(std::move(task));
    }

    // ========== TASK: Vegetation Systems (Tier 3) ==========
    // Heavy: tree generation, rock mesh generation
    {
        Loading::SystemInitTask task;
        task.id = "vegetation";
        task.displayName = "Vegetation systems";
        task.dependencies = {"scene"};
        task.weight = 0.2f;
        task.cpuWork = nullptr;  // VegetationSystemGroup handles internal threading for trees
        task.gpuWork = [this, ctxPtr]() -> bool {
            if (progressCallback_) progressCallback_(0.45f, "Vegetation systems");
            INIT_PROFILE_PHASE("VegetationSystems");

            CoreResources core = CoreResources::collect(
                systems_->postProcess(), systems_->shadow(), systems_->terrain(), MAX_FRAMES_IN_FLIGHT);

            const float halfTerrain = 8192.0f;
            glm::vec2 sceneOrigin(9200.0f - halfTerrain, 3000.0f - halfTerrain);

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
        asyncLoader_->addTask(std::move(task));
    }

    // ========== TASK: Atmosphere Systems (Tier 4 - parallel with vegetation) ==========
    {
        Loading::SystemInitTask task;
        task.id = "atmosphere";
        task.displayName = "Atmosphere systems";
        task.dependencies = {"scene"};
        task.weight = 0.1f;
        task.cpuWork = nullptr;
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
        asyncLoader_->addTask(std::move(task));
    }

    // ========== TASK: Water Systems (Tier 5) ==========
    {
        Loading::SystemInitTask task;
        task.id = "water";
        task.displayName = "Water systems";
        task.dependencies = {"vegetation", "atmosphere"};
        task.weight = 0.1f;
        task.cpuWork = nullptr;
        task.gpuWork = [this, ctxPtr]() -> bool {
            if (progressCallback_) progressCallback_(0.75f, "Water systems");
            INIT_PROFILE_PHASE("WaterSystems");

            CoreResources core = CoreResources::collect(
                systems_->postProcess(), systems_->shadow(), systems_->terrain(), MAX_FRAMES_IN_FLIGHT);

            WaterSystemGroup::CreateDeps waterDeps{
                *ctxPtr,
                core.hdr.renderPass,
                65536.0f,
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
        asyncLoader_->addTask(std::move(task));
    }

    // ========== TASK: Geometry & Finalization (Tier 6) ==========
    {
        Loading::SystemInitTask task;
        task.id = "finalize";
        task.displayName = "Finalizing systems";
        task.dependencies = {"water"};
        task.weight = 0.15f;
        task.cpuWork = nullptr;
        task.gpuWork = [this, ctxPtr]() -> bool {
            if (progressCallback_) progressCallback_(0.85f, "Finalizing systems");

            VkDevice device = vulkanContext_->getVkDevice();
            CoreResources core = CoreResources::collect(
                systems_->postProcess(), systems_->shadow(), systems_->terrain(), MAX_FRAMES_IN_FLIGHT);
            const float halfTerrain = 8192.0f;
            glm::vec2 sceneOrigin(9200.0f - halfTerrain, 3000.0f - halfTerrain);

            // System wiring
            SystemWiring wiring(device, MAX_FRAMES_IN_FLIGHT);
            wiring.wireTerrainDescriptors(*systems_);

            // Deferred terrain objects
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

            // Common bindings function for descriptor sets
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

            // Create rocks descriptor sets
            if (!systems_->rocks().createDescriptorSets(
                    device, *descriptorInfra_.getDescriptorPool(),
                    descriptorInfra_.getVkDescriptorSetLayout(),
                    MAX_FRAMES_IN_FLIGHT, getCommonBindings)) {
                return false;
            }

            if (systems_->deferredTerrainObjects()) {
                systems_->deferredTerrainObjects()->setCommonBindingsFunc(getCommonBindings);
            }

            // Wire remaining systems
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

            // Hi-Z system
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
                } else {
                    SDL_Log("Warning: GPUSceneBuffer initialization failed, GPU-driven rendering disabled");
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
                    // Wire Hi-Z pyramid to GPU cull pass if Hi-Z is available
                    if (systems_->hiZ().getHiZPyramidView() != VK_NULL_HANDLE) {
                        gpuCullPass->setHiZPyramid(
                            systems_->hiZ().getHiZPyramidView(),
                            systems_->hiZ().getHiZSampler());
                    }
                    // Set placeholder image for MoltenVK compatibility (all bindings must be valid)
                    const auto* whiteTexture = systems_->scene().getSceneBuilder().getWhiteTexture();
                    if (whiteTexture) {
                        gpuCullPass->setPlaceholderImage(
                            whiteTexture->getImageView(),
                            whiteTexture->getSampler());
                    }
                    systems_->setGPUCullPass(std::move(gpuCullPass));
                    SDL_Log("GPUCullPass: Initialized for frustum culling");
                } else {
                    SDL_Log("Warning: GPUCullPass initialization failed, GPU culling disabled");
                }
            }

            // Profiler
            systems_->setProfiler(Profiler::create(device, vulkanContext_->getVkPhysicalDevice(), MAX_FRAMES_IN_FLIGHT));

            // Wire caustics
            wiring.wireCausticsToTerrain(*systems_);

            // Sync objects
            if (!createSyncObjects()) return false;

            // RendererCore
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
        asyncLoader_->addTask(std::move(task));
    }

    return true;
}
