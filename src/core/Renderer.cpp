#define VMA_IMPLEMENTATION
#include "Renderer.h"
#include "RendererInit.h"
#include "RendererSystems.h"
#include "MaterialDescriptorFactory.h"
#include "InitProfiler.h"
#include "QueueSubmitDiagnostics.h"
#include "core/pipeline/FrameGraphBuilder.h"
#include "core/FrameUpdater.h"
#include "core/FrameDataBuilder.h"
#include "core/updaters/UBOUpdater.h"
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
#include "DebugLineSystem.h"
#include "HiZSystem.h"
#include "interfaces/IDebugControl.h"
#include "controls/DebugControlSubsystem.h"
#include "threading/TaskScheduler.h"
// Vegetation
#include "GrassSystem.h"
#include "RockSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "DetritusSystem.h"
#include "CullCommon.h"  // For extractFrustumPlanes
// Water
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "SSRSystem.h"
// Post-processing
#include "BilateralGridSystem.h"
// Geometry
#include "CatmullClarkSystem.h"
// Weather
#include "WeatherSystem.h"
#include "LeafSystem.h"

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
    if (!instance->initInternal(info)) {
        return nullptr;
    }
    return instance;
}

Renderer::~Renderer() {
    cleanup();
}

bool Renderer::initInternal(const InitInfo& info) {
    INIT_PROFILE_PHASE("Renderer");

    resourcePath = info.resourcePath;
    config_ = info.config;

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
    {
        INIT_PROFILE_PHASE("CoreVulkanResources");
        if (!initCoreVulkanResources()) return false;
    }

    // Initialize asset registry via RenderingInfrastructure (after command pool is ready)
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
    {
        INIT_PROFILE_PHASE("Subsystems");
        if (!initSubsystems(initCtx)) return false;
    }

    // Phase 4: Control subsystems (after systems are ready)
    {
        INIT_PROFILE_PHASE("ControlSubsystems");
        initControlSubsystems();
    }

    // Phase 5: Resize coordinator registration
    {
        INIT_PROFILE_PHASE("ResizeCoordinator");
        initResizeCoordinator();
    }

    // Initialize pass recorders (must be after systems_ is set up)
    {
        INIT_PROFILE_PHASE("PassRecorders");
        shadowPassRecorder_ = std::make_unique<ShadowPassRecorder>(*systems_);
        ShadowPassRecorder::Config shadowConfig;
        shadowConfig.terrainEnabled = terrainEnabled;
        shadowConfig.perfToggles = &perfToggles;
        shadowPassRecorder_->setConfig(shadowConfig);

        hdrPassRecorder_ = std::make_unique<HDRPassRecorder>(*systems_);
        HDRPassRecorder::Config hdrConfig;
        hdrConfig.terrainEnabled = terrainEnabled;
        hdrConfig.sceneObjectsPipeline = descriptorInfra_.hasPipeline() ?
            reinterpret_cast<const vk::Pipeline*>(&descriptorInfra_.getGraphicsPipeline()) : nullptr;
        hdrConfig.pipelineLayout = descriptorInfra_.hasPipeline() ?
            reinterpret_cast<const vk::PipelineLayout*>(&descriptorInfra_.getPipelineLayout()) : nullptr;
        hdrConfig.lastViewProj = &lastViewProj;
        hdrPassRecorder_->setConfig(hdrConfig);
    }
    SDL_Log("Pass recorders initialized");

    // Setup frame graph with dependencies
    {
        INIT_PROFILE_PHASE("FrameGraph");
        setupFrameGraph();
    }
    SDL_Log("Frame graph configured");

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
    if (!physicsDebugEnabled) return;

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
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                            frameSync_.currentImageAvailableSemaphore(), VK_NULL_HANDLE, &imageIndex);
    auto acquireEnd = std::chrono::high_resolution_clock::now();
    qsDiag.acquireImageTimeMs = std::chrono::duration<float, std::milli>(acquireEnd - acquireStart).count();
    systems_->profiler().endCpuZone("Wait:AcquireImage");

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
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
        auto uboResult = UBOUpdater::update(*systems_, frameSync_.currentIndex(), camera, uboConfig);
        lastSunIntensity = uboResult.sunIntensity;
        // Track bandwidth: main UBO + dynamic UBO copy + snow + cloudShadow + lights
        bandwidthCounter.recordUboUpdate(sizeof(UniformBufferObject) * 2);  // regular + dynamic
        bandwidthCounter.recordUboUpdate(sizeof(SnowUBO));
        bandwidthCounter.recordUboUpdate(sizeof(CloudShadowUBO));
        bandwidthCounter.recordSsboUpdate(sizeof(LightBuffer));
        systems_->profiler().endCpuZone("UniformUpdates:UBO");
    }

    // Update bone matrices for GPU skinning
    {
        systems_->profiler().beginCpuZone("UniformUpdates:Bones");
        SceneBuilder& sceneBuilder = systems_->scene().getSceneBuilder();
        AnimatedCharacter* character = sceneBuilder.hasCharacter() ? &sceneBuilder.getAnimatedCharacter() : nullptr;
        systems_->skinnedMesh().updateBoneMatrices(frameSync_.currentIndex(), character);
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
    FrameGraph::RenderContext fgCtx{
        .commandBuffer = vkCmd,
        .frameIndex = frame.frameIndex,
        .imageIndex = imageIndex,
        .deltaTime = frame.deltaTime,
        .userData = &ctx,  // Pass full RenderContext to passes
        // Secondary command buffer support
        .threadedCommandPool = &renderingInfra_.threadedCommandPool(),
        .renderPass = vk::RenderPass(systems_->postProcess().getHDRRenderPass()),
        .framebuffer = vk::Framebuffer(systems_->postProcess().getHDRFramebuffer()),
        // Command diagnostics - passes increment these counters
        .diagnostics = &cmdDiag
    };

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
    // Update config in case terrainEnabled changed at runtime
    ShadowPassRecorder::Config config;
    config.terrainEnabled = terrainEnabled;
    config.perfToggles = &perfToggles;
    shadowPassRecorder_->setConfig(config);

    // Delegate to the recorder
    shadowPassRecorder_->record(cmd, frameIndex, grassTime, cameraPosition);
}

void Renderer::recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime) {
    // Update config in case state changed at runtime
    HDRPassRecorder::Config config;
    config.terrainEnabled = terrainEnabled;
    config.sceneObjectsPipeline = descriptorInfra_.hasPipeline() ?
        reinterpret_cast<const vk::Pipeline*>(&descriptorInfra_.getGraphicsPipeline()) : nullptr;
    config.pipelineLayout = descriptorInfra_.hasPipeline() ?
        reinterpret_cast<const vk::PipelineLayout*>(&descriptorInfra_.getPipelineLayout()) : nullptr;
    config.lastViewProj = &lastViewProj;
    hdrPassRecorder_->setConfig(config);

    // Delegate to the recorder
    hdrPassRecorder_->record(cmd, frameIndex, grassTime);
}

void Renderer::recordHDRPassWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime,
                                            const std::vector<vk::CommandBuffer>& secondaries) {
    // Update config in case state changed at runtime
    HDRPassRecorder::Config config;
    config.terrainEnabled = terrainEnabled;
    config.sceneObjectsPipeline = descriptorInfra_.hasPipeline() ?
        reinterpret_cast<const vk::Pipeline*>(&descriptorInfra_.getGraphicsPipeline()) : nullptr;
    config.pipelineLayout = descriptorInfra_.hasPipeline() ?
        reinterpret_cast<const vk::PipelineLayout*>(&descriptorInfra_.getPipelineLayout()) : nullptr;
    config.lastViewProj = &lastViewProj;
    hdrPassRecorder_->setConfig(config);

    // Delegate to the recorder
    hdrPassRecorder_->recordWithSecondaries(cmd, frameIndex, grassTime, secondaries);
}

void Renderer::recordHDRPassSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime, uint32_t slot) {
    // Update config in case state changed at runtime
    HDRPassRecorder::Config config;
    config.terrainEnabled = terrainEnabled;
    config.sceneObjectsPipeline = descriptorInfra_.hasPipeline() ?
        reinterpret_cast<const vk::Pipeline*>(&descriptorInfra_.getGraphicsPipeline()) : nullptr;
    config.pipelineLayout = descriptorInfra_.hasPipeline() ?
        reinterpret_cast<const vk::PipelineLayout*>(&descriptorInfra_.getPipelineLayout()) : nullptr;
    config.lastViewProj = &lastViewProj;
    hdrPassRecorder_->setConfig(config);

    // Delegate to the recorder
    hdrPassRecorder_->recordSecondarySlot(cmd, frameIndex, grassTime, slot);
}

// ===== GPU Skinning Implementation =====

bool Renderer::initSkinnedMeshRenderer() {
    SkinnedMeshRenderer::InitInfo info{};
    info.device = vulkanContext_->getVkDevice();
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
