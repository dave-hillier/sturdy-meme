#define VMA_IMPLEMENTATION
#include "Renderer.h"
#include "RendererInit.h"
#include "RendererSystems.h"
#include "RenderPipelineFactory.h"
#include "ShaderLoader.h"
#include "GraphicsPipelineFactory.h"
#include "MaterialDescriptorFactory.h"
#include "VulkanHelpers.h"
#include "VmaResources.h"
#include "Bindings.h"
#include "InitProfiler.h"
#include "QueueSubmitDiagnostics.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/pipeline/FrameGraphBuilder.h"
#include "core/FrameUpdater.h"

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
#include "RoadRiverVisualization.h"
#include "HiZSystem.h"
#include "interfaces/IDebugControl.h"
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

    // Initialize asset registry (after command pool is ready)
    {
        INIT_PROFILE_PHASE("AssetRegistry");
        assetRegistry_.init(
            vulkanContext_->getVkDevice(),
            vulkanContext_->getVkPhysicalDevice(),
            vulkanContext_->getAllocator(),
            **commandPool_,
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
        *vulkanContext_, **commandPool_, &*descriptorManagerPool,
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

    // Setup render pipeline stages with lambdas
    {
        INIT_PROFILE_PHASE("RenderPipeline");
        setupRenderPipeline();
    }
    SDL_Log("Render pipeline configured");

    // Setup frame graph with dependencies (Phase 3: threading migration)
    {
        INIT_PROFILE_PHASE("FrameGraph");
        setupFrameGraph();
    }
    SDL_Log("Frame graph configured");

    return true;
}

void Renderer::setupRenderPipeline() {
    // Use factory to configure the render pipeline
    // This moves all the lambda captures and subsystem includes to RenderPipelineFactory.cpp
    RenderPipelineFactory::PipelineState state{};
    state.terrainEnabled = &terrainEnabled;
    state.physicsDebugEnabled = &physicsDebugEnabled;
    state.currentFrame = frameSync_.currentIndexPtr();
    state.lastViewProj = &lastViewProj;
    state.graphicsPipeline = **graphicsPipeline_;

    RenderPipelineFactory::setupPipeline(
        renderPipeline,
        *systems_,
        state,
        [this](VkCommandBuffer cmd, uint32_t frameIndex) {
            recordSceneObjects(cmd, frameIndex);
        }
    );

    // Apply initial toggle state
    syncPerformanceToggles();
}

void Renderer::syncPerformanceToggles() {
    // Use factory to sync toggles (reduces code duplication)
    RenderPipelineFactory::syncToggles(renderPipeline, perfToggles);
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
    state.perfToggles = &perfToggles;
    state.framebuffers = &framebuffers_;

    // Use FrameGraphBuilder to configure all passes and dependencies
    useFrameGraph = FrameGraphBuilder::build(
        frameGraph_,
        *systems_,
        renderPipeline,
        callbacks,
        state
    );
}


// Note: initCoreVulkanResources(), initDescriptorInfrastructure(), initSubsystems(),
// and initResizeCoordinator() are implemented in RendererInitPhases.cpp

void Renderer::setPlayerPosition(const glm::vec3& position, float radius) {
    setPlayerState(position, glm::vec3(0.0f), radius);
}

void Renderer::setPlayerState(const glm::vec3& position, const glm::vec3& velocity, float radius) {
    playerPosition = position;
    playerVelocity = velocity;
    playerCapsuleRadius = radius;
}

void Renderer::updateRoadRiverVisualization() {
    if (!systems_->debugControl().isRoadRiverVisualizationEnabled()) {
        // Clear persistent lines when disabled
        if (systems_->debugLine().getPersistentLineCount() > 0) {
            systems_->debugLine().clearPersistentLines();
        }
        return;
    }

    // Sync individual road/river toggles to visualization config
    auto& config = systems_->roadRiverVis().getConfig();
    bool needsRebuild = false;

    if (config.showRoads != systems_->debugControl().isRoadVisualizationEnabled()) {
        config.showRoads = systems_->debugControl().isRoadVisualizationEnabled();
        needsRebuild = true;
    }
    if (config.showRivers != systems_->debugControl().isRiverVisualizationEnabled()) {
        config.showRivers = systems_->debugControl().isRiverVisualizationEnabled();
        needsRebuild = true;
    }

    if (needsRebuild) {
        systems_->roadRiverVis().invalidateCache();
        systems_->debugLine().clearPersistentLines();
    }

    // Add road/river visualization to debug lines
    systems_->roadRiverVis().addToDebugLines(systems_->debugLine());
}

#ifdef JPH_DEBUG_RENDERER
void Renderer::updatePhysicsDebug(PhysicsWorld& physics, const glm::vec3& cameraPos) {
    if (!physicsDebugEnabled) return;

    // Begin debug line frame (clear previous and set frame index)
    // This is called here so physics debug lines can be collected before render()
    systems_->debugLine().beginFrame(frameSync_.currentIndex());

    // Create debug renderer on first use (after Jolt is initialized)
    if (!systems_->physicsDebugRenderer()) {
        InitContext initCtx = InitContext::build(
            *vulkanContext_, **commandPool_, &*descriptorManagerPool,
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

        // Shutdown multi-threading infrastructure
        asyncTextureUploader_.shutdown();  // Must shutdown before transfer manager
        asyncTransferManager_.shutdown();
        threadedCommandPool_.shutdown();

        // RAII handles cleanup of sync objects via TripleBuffering
        frameSync_.destroy();

        // Destroy all subsystems via RendererSystems
        if (systems_) {
            systems_->destroy(device, allocator);
            systems_.reset();
        }

        // Clean up the auto-growing descriptor pool
        if (descriptorManagerPool.has_value()) {
            descriptorManagerPool->destroy();
            descriptorManagerPool.reset();
        }

        // RAII handles: graphicsPipeline, pipelineLayout, descriptorSetLayout
        SDL_Log("destroying graphicsPipeline");
        graphicsPipeline_.reset();
        SDL_Log("destroying pipelineLayout");
        pipelineLayout_.reset();
        SDL_Log("destroying descriptorSetLayout");
        descriptorSetLayout_.reset();
        SDL_Log("descriptor layouts destroyed");

        // RAII handles: commandPool
        SDL_Log("destroying commandPool");
        commandPool_.reset();
        SDL_Log("commandPool destroyed");

        // RAII handles: depth resources and framebuffers
        SDL_Log("clearing framebuffers");
        framebuffers_.clear();
        SDL_Log("destroying depthSampler");
        depthSampler_.reset();
        SDL_Log("destroying depthImageView");
        depthImageView_.reset();
        SDL_Log("destroying depthImage");
        depthImage_.reset();
        SDL_Log("destroying renderPass");
        renderPass_.reset();
        SDL_Log("render resources destroyed");
    }

    SDL_Log("calling vulkanContext_->shutdown");
    vulkanContext_->shutdown();
    SDL_Log("vulkanContext shutdown complete");
}

void Renderer::destroyRenderResources() {
    // RAII handles all cleanup - just clear containers and reset managed objects
    framebuffers_.clear();
    depthSampler_.reset();
    depthImageView_.reset();
    depthImage_.reset();
    renderPass_.reset();
}

bool Renderer::createRenderPass() {
    depthFormat = VK_FORMAT_D32_SFLOAT;

    RenderPassConfig config{};
    config.colorFormat = static_cast<VkFormat>(vulkanContext_->getVkSwapchainImageFormat());
    config.depthFormat = depthFormat;
    config.finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    config.finalDepthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // For Hi-Z
    config.storeDepth = true;  // For Hi-Z pyramid generation

    auto result = ::createRenderPass(vulkanContext_->getRaiiDevice(), config);
    if (!result) {
        return false;
    }
    renderPass_.emplace(std::move(*result));
    return true;
}

bool Renderer::createDepthResources() {
    DepthResources depth;
    vk::Extent2D extent{vulkanContext_->getVkSwapchainExtent().width,
                        vulkanContext_->getVkSwapchainExtent().height};
    if (!::createDepthResources(
            vulkanContext_->getRaiiDevice(), vulkanContext_->getAllocator(),
            extent, static_cast<vk::Format>(depthFormat), depth)) {
        return false;
    }
    // Move RAII resources from temporary DepthResources to member fields
    depthImage_ = std::move(depth.image);
    depthImageView_ = std::move(depth.view);
    depthSampler_ = std::move(depth.sampler);
    return true;
}

bool Renderer::createFramebuffers() {
    auto result = ::createFramebuffers(
        vulkanContext_->getRaiiDevice(), *renderPass_,
        vulkanContext_->getSwapchainImageViews(), **depthImageView_,
        vulkanContext_->getVkSwapchainExtent());
    if (!result) {
        return false;
    }

    framebuffers_ = std::move(*result);
    return true;
}

bool Renderer::createCommandPool() {
    try {
        auto poolInfo = vk::CommandPoolCreateInfo{}
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
            .setQueueFamilyIndex(vulkanContext_->getGraphicsQueueFamily());
        commandPool_.emplace(vulkanContext_->getRaiiDevice(), poolInfo);
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create command pool: %s", e.what());
        return false;
    }
}

bool Renderer::createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    auto allocInfo = vk::CommandBufferAllocateInfo{}
        .setCommandPool(**commandPool_)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(MAX_FRAMES_IN_FLIGHT);

    if (vkAllocateCommandBuffers(vulkanContext_->getVkDevice(),
            reinterpret_cast<const VkCommandBufferAllocateInfo*>(&allocInfo),
            commandBuffers.data()) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate command buffers");
        return false;
    }
    return true;
}

bool Renderer::createSyncObjects() {
    return frameSync_.init(vulkanContext_->getRaiiDevice(), MAX_FRAMES_IN_FLIGHT);
}

// Adds the common descriptor bindings shared between main and skinned layouts.
// This ensures both layouts stay in sync for bindings used by shader.frag.
void Renderer::addCommonDescriptorBindings(DescriptorManager::LayoutBuilder& builder) {
    builder
        .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: UBO
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 1: diffuse
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 2: shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 3: normal
        .addStorageBuffer(VK_SHADER_STAGE_FRAGMENT_BIT)         // 4: lights
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 5: emissive
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 6: point shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 7: spot shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 8: snow mask
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 9: cloud shadow map
        .addUniformBuffer(VK_SHADER_STAGE_FRAGMENT_BIT)         // 10: Snow UBO
        .addUniformBuffer(VK_SHADER_STAGE_FRAGMENT_BIT)         // 11: Cloud shadow UBO
        // Note: binding 12 (bone matrices) is added separately for skinned layout
        .addBinding(Bindings::ROUGHNESS_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // 13: roughness
        .addBinding(Bindings::METALLIC_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)   // 14: metallic
        .addBinding(Bindings::AO_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)         // 15: AO
        .addBinding(Bindings::HEIGHT_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)     // 16: height
        .addBinding(Bindings::WIND_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);                // 17: wind UBO
}

bool Renderer::createDescriptorSetLayout() {
    VkDevice device = vulkanContext_->getVkDevice();

    // Main scene descriptor set layout - uses common bindings (0-11, 13-16)
    DescriptorManager::LayoutBuilder builder(device);
    addCommonDescriptorBindings(builder);
    VkDescriptorSetLayout rawLayout = builder.build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create descriptor set layout");
        return false;
    }

    descriptorSetLayout_.emplace(vulkanContext_->getRaiiDevice(), rawLayout);
    return true;
}

bool Renderer::createGraphicsPipeline() {
    vk::Device device(vulkanContext_->getVkDevice());
    VkExtent2D swapchainExtent = vulkanContext_->getVkSwapchainExtent();

    // Create pipeline layout using PipelineLayoutBuilder
    auto layoutOpt = PipelineLayoutBuilder(vulkanContext_->getRaiiDevice())
        .addDescriptorSetLayout(**descriptorSetLayout_)
        .addPushConstantRange<PushConstants>(
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create pipeline layout");
        return false;
    }
    pipelineLayout_ = std::move(layoutOpt);

    // Use factory for pipeline creation
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    GraphicsPipelineFactory factory(device);
    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::Default)
        .setShaders(resourcePath + "/shaders/shader.vert.spv",
                    resourcePath + "/shaders/shader.frag.spv")
        .setVertexInput({bindingDescription},
                        {attributeDescriptions.begin(), attributeDescriptions.end()})
        .setRenderPass(systems_->postProcess().getHDRRenderPass())
        .setPipelineLayout(**pipelineLayout_)
        .setExtent(swapchainExtent)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Alpha)
        .build(rawPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create graphics pipeline");
        return false;
    }

    graphicsPipeline_.emplace(vulkanContext_->getRaiiDevice(), rawPipeline);
    return true;
}

void Renderer::updateLightBuffer(uint32_t currentImage, const Camera& camera) {
    LightBuffer buffer{};
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    systems_->scene().getLightManager().buildLightBuffer(buffer, camera.getPosition(), camera.getFront(), viewProj, lightCullRadius);
    systems_->globalBuffers().updateLightBuffer(currentImage, buffer);
}

bool Renderer::createDescriptorPool() {
    VkDevice device = vulkanContext_->getVkDevice();

    // Create the auto-growing descriptor pool with configurable sizes
    // Will automatically grow if exhausted
    // All subsystems now use this managed pool for consistent descriptor allocation
    descriptorManagerPool.emplace(device, config_.setsPerPool, config_.descriptorPoolSizes);

    return true;
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
        *descriptorManagerPool,
        **descriptorSetLayout_,
        MAX_FRAMES_IN_FLIGHT,
        getCommonBindings);

    if (!materialRegistry.hasDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create MaterialRegistry descriptor sets");
        return false;
    }

    // Rock descriptor sets (RockSystem has its own textures, not in MaterialRegistry)
    // Note: rockSystem is not initialized at this point - allocation only, writing done in initPhase2
    rockDescriptorSets = descriptorManagerPool->allocate(**descriptorSetLayout_, MAX_FRAMES_IN_FLIGHT);
    if (rockDescriptorSets.empty()) {
        SDL_Log("Failed to allocate rock descriptor sets");
        return false;
    }

    // Detritus descriptor sets (fallen branches - allocation only, writing done in initPhase2)
    detritusDescriptorSets = descriptorManagerPool->allocate(**descriptorSetLayout_, MAX_FRAMES_IN_FLIGHT);
    if (detritusDescriptorSets.empty()) {
        SDL_Log("Failed to allocate detritus descriptor sets");
        return false;
    }

    // Tree descriptor sets - allocation deferred to initPhase2 when TreeSystem is available
    // Will allocate per texture type using string-based maps

    return true;
}


void Renderer::updateUniformBuffer(uint32_t currentImage, const Camera& camera) {
    // Get current time of day from time system (already updated in render())
    float currentTimeOfDay = systems_->time().getTimeOfDay();

    // Pure calculations via UBOBuilder
    UBOBuilder::LightingParams lighting = systems_->uboBuilder().calculateLightingParams(currentTimeOfDay);
    systems_->time().setCurrentMoonPhase(lighting.moonPhase);  // Track current effective phase

    // Calculate and apply tide based on celestial positions
    DateTime dateTime = DateTime::fromTimeOfDay(currentTimeOfDay, systems_->time().getCurrentYear(),
                                                 systems_->time().getCurrentMonth(), systems_->time().getCurrentDay());
    TideInfo tide = systems_->celestial().calculateTide(dateTime);
    systems_->water().updateTide(tide.height);

    // Update cascade matrices via shadow system
    systems_->shadow().updateCascadeMatrices(lighting.sunDir, camera);

    // Build UBO data via UBOBuilder (pure calculation)
    UBOBuilder::MainUBOConfig mainConfig{};
    mainConfig.showCascadeDebug = showCascadeDebug;
    mainConfig.useParaboloidClouds = useParaboloidClouds;
    mainConfig.cloudCoverage = cloudCoverage;
    mainConfig.cloudDensity = cloudDensity;
    mainConfig.skyExposure = skyExposure;
    UniformBufferObject ubo = systems_->uboBuilder().buildUniformBufferData(camera, lighting, currentTimeOfDay, mainConfig);

    UBOBuilder::SnowConfig snowConfig{};
    snowConfig.useVolumetricSnow = useVolumetricSnow;
    snowConfig.showSnowDepthDebug = showSnowDepthDebug;
    snowConfig.maxSnowHeight = MAX_SNOW_HEIGHT;
    SnowUBO snowUbo = systems_->uboBuilder().buildSnowUBOData(snowConfig);

    // Set rain wetness from weather system (composable material system integration)
    // Weather type 0 = rain, type 1 = snow - rain causes wetness on vegetation
    if (systems_->weather().getWeatherType() == 0) {
        snowUbo.rainWetness = systems_->weather().getIntensity();
    } else {
        snowUbo.rainWetness = 0.0f;
    }

    CloudShadowUBO cloudShadowUbo = systems_->uboBuilder().buildCloudShadowUBOData();

    // State mutations - use GlobalBufferManager for buffer updates
    lastSunIntensity = lighting.sunIntensity;
    systems_->globalBuffers().updateUniformBuffer(currentImage, ubo);
    systems_->globalBuffers().updateSnowBuffer(currentImage, snowUbo);
    systems_->globalBuffers().updateCloudShadowBuffer(currentImage, cloudShadowUbo);

    // Update light buffer with camera-based culling
    updateLightBuffer(currentImage, camera);

    // Calculate sun screen position (pure) and update post-process (state mutation)
    glm::vec2 sunScreenPos = calculateSunScreenPos(camera, lighting.sunDir);
    systems_->postProcess().setSunScreenPos(sunScreenPos);

    // Update HDR enabled state
    systems_->postProcess().setHDREnabled(hdrEnabled);
}

bool Renderer::render(const Camera& camera) {
    // Skip rendering if window is suspended (e.g., macOS screen lock)
    if (windowSuspended) {
        return false;
    }

    // Sync performance toggles to pipeline stages (allows runtime toggle changes)
    syncPerformanceToggles();

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
    asyncTransferManager_.processPendingTransfers();

    // Update time system (frame timing and day/night cycle)
    TimingData timing = systems_->time().update();

    // Begin CPU profiling for this frame
    systems_->profiler().beginCpuZone("UniformUpdates");

    // Track UBO bandwidth
    CommandCounter bandwidthCounter(&qsDiag);

    // Update uniform buffer data
    {
        systems_->profiler().beginCpuZone("UniformUpdates:UBO");
        updateUniformBuffer(frameSync_.currentIndex(), camera);
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
    FrameData frame = buildFrameData(camera, timing.deltaTime, timing.elapsedTime);

    // Cache view-projection for debug rendering
    lastViewProj = frame.viewProj;

    // Begin debug line frame if not already started by physics debug
    // Physics debug calls beginFrame before render() if enabled
    // Only call beginFrame if we haven't collected lines yet (no physics debug this frame)
    if (!systems_->debugLine().hasLines()) {
        systems_->debugLine().beginFrame(frameSync_.currentIndex());
    }

    // Add road/river visualization to debug lines
    updateRoadRiverVisualization();

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

    vk::CommandBuffer vkCmd(commandBuffers[frame.frameIndex]);
    vkCmd.reset();
    vkCmd.begin(vk::CommandBufferBeginInfo{});

    VkCommandBuffer cmd = commandBuffers[frame.frameIndex];

    // Get reference to diagnostics for command counting
    auto& cmdDiag = systems_->profiler().getQueueSubmitDiagnostics();

    // Set global diagnostics for subsystems that don't have direct access
    ScopedDiagnostics scopedDiag(&cmdDiag);

    // Begin command capture if requested
    auto& cmdCapture = systems_->profiler().getCommandCapture();
    cmdCapture.beginFrame(systems_->profiler().getFrameNumber());

    // Begin GPU profiling frame
    systems_->profiler().beginGpuFrame(cmd, frame.frameIndex);

    // Build render resources and context for pipeline stages
    RenderResources resources = buildRenderResources(imageIndex);
    RenderContext ctx(cmd, frame.frameIndex, frame, resources, &cmdDiag);

    if (useFrameGraph) {
        // ===== FRAME GRAPH EXECUTION PATH =====
        // Uses dependency-driven scheduling with parallel execution opportunities
        FrameGraph::RenderContext fgCtx{
            .commandBuffer = vkCmd,
            .frameIndex = frame.frameIndex,
            .imageIndex = imageIndex,
            .deltaTime = frame.deltaTime,
            .userData = &ctx,  // Pass full RenderContext to passes
            // Phase 4: Secondary command buffer support
            .threadedCommandPool = &threadedCommandPool_,
            .renderPass = vk::RenderPass(systems_->postProcess().getHDRRenderPass()),
            .framebuffer = vk::Framebuffer(systems_->postProcess().getHDRFramebuffer()),
            // Command diagnostics - passes increment these counters
            .diagnostics = &cmdDiag
        };

        // Execute all passes in dependency order
        // TaskScheduler enables parallel execution of independent passes
        frameGraph_.execute(fgCtx, &TaskScheduler::instance());
    } else {
        // ===== LEGACY EXECUTION PATH =====
        // Sequential execution for fallback/debugging

        // Execute all compute passes via pipeline
        systems_->profiler().beginCpuZone("ComputeDispatch");
        renderPipeline.computeStage.execute(ctx);
        systems_->profiler().endCpuZone("ComputeDispatch");

        // Shadow pass (skip when sun is below horizon or shadows disabled)
        if (lastSunIntensity > 0.001f && perfToggles.shadowPass) {
            systems_->profiler().beginCpuZone("ShadowRecord");
            systems_->profiler().beginGpuZone(cmd, "ShadowPass");
            cmdDiag.renderPassCount++;  // Shadow cascade render pass
            cmdCapture.recordBeginRenderPass("ShadowSystem", "ShadowCascades");
            recordShadowPass(cmd, frame.frameIndex, frame.time, frame.cameraPosition);
            cmdCapture.recordEndRenderPass("ShadowSystem");
            systems_->profiler().endGpuZone(cmd, "ShadowPass");
            systems_->profiler().endCpuZone("ShadowRecord");
        }

        // Froxel volumetric fog and atmosphere updates via pipeline
        systems_->postProcess().setCameraPlanes(camera.getNearPlane(), camera.getFarPlane());
        if (renderPipeline.froxelStageFn && (perfToggles.froxelFog || perfToggles.atmosphereLUT)) {
            renderPipeline.froxelStageFn(ctx);
        }

        // Water G-buffer pass (Phase 3) - renders water mesh to mini G-buffer
        // Skip if water was not visible last frame (temporal culling) or disabled
        if (perfToggles.waterGBuffer &&
            systems_->waterGBuffer().getPipeline() != VK_NULL_HANDLE &&
            systems_->hasWaterTileCull() &&
            systems_->waterTileCull().wasWaterVisibleLastFrame(frame.frameIndex)) {
            systems_->profiler().beginGpuZone(cmd, "WaterGBuffer");
            cmdDiag.renderPassCount++;  // Water G-buffer pass
            systems_->waterGBuffer().beginRenderPass(cmd);

            // Bind G-buffer pipeline and descriptor set
            vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, systems_->waterGBuffer().getPipeline());
            cmdDiag.pipelineBindCount++;
            vk::DescriptorSet gbufferDescSet = systems_->waterGBuffer().getDescriptorSet(frame.frameIndex);
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     systems_->waterGBuffer().getPipelineLayout(), 0, gbufferDescSet, {});
            cmdDiag.descriptorSetBindCount++;

            // Draw water mesh
            systems_->water().recordMeshDraw(cmd);
            cmdDiag.drawCallCount++;

            systems_->waterGBuffer().endRenderPass(cmd);
            systems_->profiler().endGpuZone(cmd, "WaterGBuffer");
        }

        // HDR scene render pass (can be disabled for performance debugging)
        // Note: HDRPass is not wrapped in a profiler zone because recordHDRPass()
        // contains granular HDR:* sub-zones. Nesting would confuse the profiler.
        if (hdrPassEnabled) {
            systems_->profiler().beginCpuZone("RenderPassRecord");
            cmdDiag.renderPassCount++;  // HDR main render pass
            cmdCapture.recordBeginRenderPass("HDRPass", "MainScene");
            recordHDRPass(cmd, frame.frameIndex, frame.time);
            cmdCapture.recordEndRenderPass("HDRPass");
            systems_->profiler().endCpuZone("RenderPassRecord");

            // Screen-Space Reflections compute pass (Phase 10)
            // Computes SSR for next frame's water - uses current scene for temporal stability
            if (perfToggles.ssr && systems_->ssr().isEnabled()) {
                systems_->profiler().beginGpuZone(cmd, "SSR");
                cmdDiag.dispatchCount++;  // SSR compute
                systems_->ssr().recordCompute(cmd, frame.frameIndex,
                                        systems_->postProcess().getHDRColorView(),
                                        systems_->postProcess().getHDRDepthView(),
                                        frame.view, frame.projection,
                                        frame.cameraPosition);
                systems_->profiler().endGpuZone(cmd, "SSR");
            }

            // Water tile culling compute pass (Phase 7)
            // Determines which screen tiles contain water for optimized rendering
            if (perfToggles.waterTileCull && systems_->hasWaterTileCull() &&
                systems_->waterTileCull().isEnabled()) {
                systems_->profiler().beginGpuZone(cmd, "WaterTileCull");
                cmdDiag.dispatchCount++;  // Water tile cull compute
                glm::mat4 viewProj = frame.projection * frame.view;
                systems_->waterTileCull().recordTileCull(cmd, frame.frameIndex,
                                              viewProj, frame.cameraPosition,
                                              systems_->water().getWaterLevel(),
                                              systems_->postProcess().getHDRDepthView());
                systems_->profiler().endGpuZone(cmd, "WaterTileCull");
            }
        }

        // Hi-Z pyramid and Bloom via pipeline post stage
        if (renderPipeline.postStage.hiZRecordFn) {
            cmdDiag.dispatchCount++;  // Hi-Z compute
            renderPipeline.postStage.hiZRecordFn(ctx);
        }
        // Only run bloom passes if bloom is enabled (skip for performance)
        if (systems_->postProcess().isBloomEnabled() && renderPipeline.postStage.bloomRecordFn) {
            cmdDiag.dispatchCount += 2;  // Bloom downsample + upsample
            renderPipeline.postStage.bloomRecordFn(ctx);
        }

        // Bilateral grid for local tone mapping (if enabled)
        if (systems_->postProcess().isLocalToneMapEnabled()) {
            systems_->profiler().beginGpuZone(cmd, "BilateralGrid");
            cmdDiag.dispatchCount++;  // Bilateral grid compute
            systems_->bilateralGrid().recordBilateralGrid(cmd, frame.frameIndex,
                                                           systems_->postProcess().getHDRColorView());
            systems_->profiler().endGpuZone(cmd, "BilateralGrid");
        }

        // Post-process pass (with optional GUI overlay callback)
        // Note: This is not in postStage because it needs framebuffer and guiRenderCallback
        systems_->profiler().beginGpuZone(cmd, "PostProcess");
        cmdDiag.renderPassCount++;  // Post-process render pass
        cmdDiag.drawCallCount++;    // Full-screen quad
        systems_->postProcess().recordPostProcess(cmd, frame.frameIndex, *framebuffers_[imageIndex], frame.deltaTime, guiRenderCallback);
        systems_->profiler().endGpuZone(cmd, "PostProcess");
    }

    // End GPU profiling frame
    systems_->profiler().endGpuFrame(cmd, frame.frameIndex);

    // End command capture
    cmdCapture.endFrame();

    vkCmd.end();

    auto recordEnd = std::chrono::high_resolution_clock::now();
    cmdDiag.commandRecordTimeMs = std::chrono::duration<float, std::milli>(recordEnd - recordStart).count();

    systems_->profiler().endCpuZone("CmdBufferRecord");

    // Queue submission
    systems_->profiler().beginCpuZone("QueueSubmit");

    vk::Semaphore waitSemaphores[] = {frameSync_.currentImageAvailableSemaphore()};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::Semaphore signalSemaphores[] = {frameSync_.currentRenderFinishedSemaphore()};

    auto submitInfo = vk::SubmitInfo{}
        .setWaitSemaphores(waitSemaphores)
        .setWaitDstStageMask(waitStages)
        .setCommandBuffers(vkCmd)
        .setSignalSemaphores(signalSemaphores);

    try {
        // Track queue submit time for diagnostics
        auto submitStart = std::chrono::high_resolution_clock::now();
        vk::Queue(graphicsQueue).submit(submitInfo, frameSync_.currentFence());
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

void Renderer::destroyDepthImageAndView() {
    // RAII handles cleanup - just reset the managed objects
    depthImageView_.reset();
    depthImage_.reset();
}

void Renderer::destroyFramebuffers() {
    // RAII handles cleanup - just clear the vector
    framebuffers_.clear();
}

bool Renderer::recreateDepthResources(VkExtent2D newExtent) {
    destroyDepthImageAndView();

    vk::Extent2D extent{newExtent.width, newExtent.height};
    VmaImage newImage;
    std::optional<vk::raii::ImageView> newView;

    if (!::createDepthImageAndView(
        vulkanContext_->getRaiiDevice(), vulkanContext_->getAllocator(),
        extent, static_cast<vk::Format>(depthFormat),
        newImage, newView)) {
        return false;
    }

    depthImage_ = std::move(newImage);
    depthImageView_ = std::move(newView);
    return true;
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

// Pure calculation helper - sun screen position for god rays

glm::vec2 Renderer::calculateSunScreenPos(const Camera& camera, const glm::vec3& sunDir) const {
    glm::vec3 sunWorldPos = camera.getPosition() + sunDir * 1000.0f;
    glm::vec4 sunClipPos = camera.getProjectionMatrix() * camera.getViewMatrix() * glm::vec4(sunWorldPos, 1.0f);

    glm::vec2 sunScreenPos(0.5f, 0.5f);
    if (sunClipPos.w > 0.0f) {
        glm::vec3 sunNDC = glm::vec3(sunClipPos) / sunClipPos.w;
        sunScreenPos = glm::vec2(sunNDC.x * 0.5f + 0.5f, sunNDC.y * 0.5f + 0.5f);
        sunScreenPos.y = 1.0f - sunScreenPos.y;
    }
    return sunScreenPos;
}

FrameData Renderer::buildFrameData(const Camera& camera, float deltaTime, float time) const {
    FrameData frame;

    frame.frameIndex = frameSync_.currentIndex();
    frame.deltaTime = deltaTime;
    frame.time = time;
    frame.timeOfDay = systems_->time().getTimeOfDay();

    frame.cameraPosition = camera.getPosition();
    frame.view = camera.getViewMatrix();
    frame.projection = camera.getProjectionMatrix();
    frame.viewProj = frame.projection * frame.view;
    frame.nearPlane = camera.getNearPlane();
    frame.farPlane = camera.getFarPlane();

    // Extract frustum planes from view-projection matrix (normalized)
    glm::mat4 m = glm::transpose(frame.viewProj);
    frame.frustumPlanes[0] = m[3] + m[0];  // Left
    frame.frustumPlanes[1] = m[3] - m[0];  // Right
    frame.frustumPlanes[2] = m[3] + m[1];  // Bottom
    frame.frustumPlanes[3] = m[3] - m[1];  // Top
    frame.frustumPlanes[4] = m[3] + m[2];  // Near
    frame.frustumPlanes[5] = m[3] - m[2];  // Far
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(frame.frustumPlanes[i]));
        if (len > 0.0f) {
            frame.frustumPlanes[i] /= len;
        }
    }

    // Get sun direction from last computed UBO (already computed in updateUniformBuffer)
    UniformBufferObject* ubo = static_cast<UniformBufferObject*>(systems_->globalBuffers().uniformBuffers.mappedPointers[frameSync_.currentIndex()]);
    frame.sunDirection = glm::normalize(glm::vec3(ubo->toSunDirection));
    frame.sunIntensity = ubo->toSunDirection.w;

    frame.playerPosition = playerPosition;
    frame.playerVelocity = playerVelocity;
    frame.playerCapsuleRadius = playerCapsuleRadius;

    const auto& terrainConfig = systems_->terrain().getConfig();
    frame.terrainSize = terrainConfig.size;
    frame.heightScale = terrainConfig.heightScale;

    // Populate wind/weather/snow data
    const auto& envSettings = systems_->wind().getEnvironmentSettings();
    frame.windDirection = envSettings.windDirection;
    frame.windStrength = envSettings.windStrength;
    frame.windSpeed = envSettings.windSpeed;
    frame.gustFrequency = envSettings.gustFrequency;
    frame.gustAmplitude = envSettings.gustAmplitude;

    frame.weatherType = systems_->weather().getWeatherType();
    frame.weatherIntensity = systems_->weather().getIntensity();

    frame.snowAmount = systems_->environmentSettings().snowAmount;
    frame.snowColor = systems_->environmentSettings().snowColor;

    // Lighting data
    frame.sunColor = glm::vec3(ubo->sunColor);
    frame.moonDirection = glm::normalize(glm::vec3(ubo->moonDirection));
    frame.moonIntensity = ubo->moonDirection.w;

    return frame;
}

RenderResources Renderer::buildRenderResources(uint32_t swapchainImageIndex) const {
    RenderResources resources;

    // HDR target (from PostProcessSystem)
    resources.hdrRenderPass = systems_->postProcess().getHDRRenderPass();
    resources.hdrFramebuffer = systems_->postProcess().getHDRFramebuffer();
    resources.hdrExtent = systems_->postProcess().getExtent();
    resources.hdrColorView = systems_->postProcess().getHDRColorView();
    resources.hdrDepthView = systems_->postProcess().getHDRDepthView();

    // Shadow resources (from ShadowSystem)
    resources.shadowRenderPass = systems_->shadow().getShadowRenderPass();
    resources.shadowMapView = systems_->shadow().getShadowImageView();
    resources.shadowSampler = systems_->shadow().getShadowSampler();
    resources.shadowPipeline = systems_->shadow().getShadowPipeline();
    resources.shadowPipelineLayout = systems_->shadow().getShadowPipelineLayout();

    // Copy cascade matrices
    const auto& cascadeMatrices = systems_->shadow().getCascadeMatrices();
    for (size_t i = 0; i < cascadeMatrices.size(); ++i) {
        resources.cascadeMatrices[i] = cascadeMatrices[i];
    }

    // Copy cascade split depths
    const auto& splitDepths = systems_->shadow().getCascadeSplitDepths();
    for (size_t i = 0; i < std::min(splitDepths.size(), size_t(4)); ++i) {
        resources.cascadeSplitDepths[i] = splitDepths[i];
    }

    // Bloom output (from BloomSystem)
    resources.bloomOutput = systems_->bloom().getBloomOutput();
    resources.bloomSampler = systems_->bloom().getBloomSampler();

    // Swapchain target
    resources.swapchainRenderPass = **renderPass_;
    resources.swapchainFramebuffer = *framebuffers_[swapchainImageIndex];
    resources.swapchainExtent = {vulkanContext_->getWidth(), vulkanContext_->getHeight()};

    // Main scene pipeline
    resources.graphicsPipeline = **graphicsPipeline_;
    resources.pipelineLayout = **pipelineLayout_;
    resources.descriptorSetLayout = **descriptorSetLayout_;

    return resources;
}

// Render pass recording helpers - pure command recording, no state mutation

void Renderer::recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime, const glm::vec3& cameraPosition) {
    // Setup phase: build callbacks and collect shadow-casting objects
    systems_->profiler().beginCpuZone("Shadow:Setup");

    // Delegate to the shadow system with callbacks for terrain and grass
    auto terrainCallback = [this, frameIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (terrainEnabled && perfToggles.terrainShadows) {
            systems_->profiler().beginGpuZone(cb, "Shadow:Terrain");
            systems_->terrain().recordShadowDraw(cb, frameIndex, lightMatrix, static_cast<int>(cascade));
            systems_->profiler().endGpuZone(cb, "Shadow:Terrain");
        }
    };

    auto grassCallback = [this, frameIndex, grassTime](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;  // Grass uses cascade index only
        if (perfToggles.grassShadows) {
            systems_->profiler().beginGpuZone(cb, "Shadow:Grass");
            systems_->grass().recordShadowDraw(cb, frameIndex, grassTime, cascade);
            systems_->profiler().endGpuZone(cb, "Shadow:Grass");
        }
    };

    auto treeCallback = [this, frameIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;
        if (systems_->tree() && systems_->treeRenderer()) {
            systems_->profiler().beginGpuZone(cb, "Shadow:Trees");
            systems_->treeRenderer()->renderShadows(cb, frameIndex, *systems_->tree(), static_cast<int>(cascade), systems_->treeLOD());
            systems_->profiler().endGpuZone(cb, "Shadow:Trees");
        }
        // Render impostor shadows
        if (systems_->treeLOD()) {
            systems_->profiler().beginGpuZone(cb, "Shadow:Impostors");
            VkBuffer uniformBuffer = systems_->globalBuffers().uniformBuffers.buffers[frameIndex];
            auto* impostorCull = systems_->impostorCull();
            if (impostorCull && impostorCull->getTreeCount() > 0) {
                // Use GPU-culled indirect rendering
                systems_->treeLOD()->renderImpostorShadowsGPUCulled(
                    cb, frameIndex, static_cast<int>(cascade), uniformBuffer,
                    impostorCull->getVisibleImpostorBuffer(),
                    impostorCull->getIndirectDrawBuffer()
                );
            } else {
                // Fall back to CPU-culled rendering
                systems_->treeLOD()->renderImpostorShadows(cb, frameIndex, static_cast<int>(cascade), uniformBuffer);
            }
            systems_->profiler().endGpuZone(cb, "Shadow:Impostors");
        }
    };

    // Combine scene objects and rock objects for shadow rendering
    // Skip player character - it's rendered separately with skinned shadow pipeline
    std::vector<Renderable> allObjects;
    const auto& sceneObjects = systems_->scene().getRenderables();
    size_t playerIndex = systems_->scene().getSceneBuilder().getPlayerObjectIndex();
    bool hasCharacter = systems_->scene().getSceneBuilder().hasCharacter();

    size_t detritusCount = systems_->detritus() ? systems_->detritus()->getSceneObjects().size() : 0;
    allObjects.reserve(sceneObjects.size() + systems_->rock().getSceneObjects().size() + detritusCount);
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        // Skip player character - rendered with skinned shadow pipeline
        if (hasCharacter && i == playerIndex) {
            continue;
        }
        allObjects.push_back(sceneObjects[i]);
    }
    allObjects.insert(allObjects.end(), systems_->rock().getSceneObjects().begin(), systems_->rock().getSceneObjects().end());
    if (systems_->detritus()) {
        allObjects.insert(allObjects.end(), systems_->detritus()->getSceneObjects().begin(), systems_->detritus()->getSceneObjects().end());
    }

    // Skinned character shadow callback (renders with GPU skinning)
    ShadowSystem::DrawCallback skinnedCallback = nullptr;
    if (hasCharacter) {
        skinnedCallback = [this, frameIndex, playerIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
            (void)lightMatrix;  // Not used, cascade matrices are in UBO
            SceneBuilder& sceneBuilder = systems_->scene().getSceneBuilder();
            const auto& sceneObjs = sceneBuilder.getRenderables();
            if (playerIndex >= sceneObjs.size()) return;

            systems_->profiler().beginGpuZone(cb, "Shadow:Skinned");
            const Renderable& playerObj = sceneObjs[playerIndex];
            AnimatedCharacter& character = sceneBuilder.getAnimatedCharacter();
            SkinnedMesh& skinnedMesh = character.getSkinnedMesh();

            // Bind skinned shadow pipeline with descriptor set that has bone matrices
            systems_->shadow().bindSkinnedShadowPipeline(cb, systems_->skinnedMesh().getDescriptorSet(frameIndex));

            // Record the skinned mesh shadow
            systems_->shadow().recordSkinnedMeshShadow(cb, cascade, playerObj.transform, skinnedMesh);
            systems_->profiler().endGpuZone(cb, "Shadow:Skinned");
        };
    }

    // Pre-cascade compute callback for GPU culling (runs before each cascade's render pass)
    ShadowSystem::ComputeCallback preCascadeComputeCallback = [this, cameraPosition](
        VkCommandBuffer cb, uint32_t frame, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (systems_->treeRenderer() && systems_->tree() && systems_->treeLOD()) {
            // Extract frustum planes from the light view-projection matrix
            glm::vec4 cascadeFrustumPlanes[6];
            extractFrustumPlanes(lightMatrix, cascadeFrustumPlanes);

            // Record GPU culling pass for branch shadows
            systems_->treeRenderer()->recordBranchShadowCulling(
                cb, frame, cascade, cascadeFrustumPlanes, cameraPosition, systems_->treeLOD());
        }
    };

    // Use any MaterialRegistry descriptor set for shadow pass (only needs common bindings/UBO)
    // MaterialId 0 is the first registered material (crate)
    const auto& materialRegistry = systems_->scene().getSceneBuilder().getMaterialRegistry();
    VkDescriptorSet shadowDescriptorSet = materialRegistry.getDescriptorSet(0, frameIndex);

    systems_->profiler().endCpuZone("Shadow:Setup");

    // Record all shadow cascades
    systems_->profiler().beginCpuZone("Shadow:Cascades");
    systems_->shadow().recordShadowPass(cmd, frameIndex, shadowDescriptorSet,
                                   allObjects,
                                   terrainCallback, grassCallback, treeCallback, skinnedCallback,
                                   preCascadeComputeCallback);
    systems_->profiler().endCpuZone("Shadow:Cascades");
}

void Renderer::recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex) {
    vk::CommandBuffer vkCmd(cmd);

    // Get MaterialRegistry for descriptor set lookup
    const auto& materialRegistry = systems_->scene().getSceneBuilder().getMaterialRegistry();

    // Helper lambda to render a scene object with a descriptor set
    auto renderObject = [&](const Renderable& obj, VkDescriptorSet descSet) {
        PushConstants push{};
        push.model = obj.transform;
        push.roughness = obj.roughness;
        push.metallic = obj.metallic;
        push.emissiveIntensity = obj.emissiveIntensity;
        push.opacity = obj.opacity;
        push.emissiveColor = glm::vec4(obj.emissiveColor, 1.0f);
        push.pbrFlags = obj.pbrFlags;
        push.alphaTestThreshold = obj.alphaTestThreshold;

        vkCmd.pushConstants<PushConstants>(
            **pipelineLayout_,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, push);

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                 **pipelineLayout_, 0, vk::DescriptorSet(descSet), {});

        vk::Buffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(obj.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        vkCmd.drawIndexed(obj.mesh->getIndexCount(), 1, 0, 0, 0);
    };

    // Render scene manager objects using MaterialRegistry for descriptor set lookup
    const auto& sceneObjects = systems_->scene().getRenderables();
    size_t playerIndex = systems_->scene().getSceneBuilder().getPlayerObjectIndex();
    bool hasCharacter = systems_->scene().getSceneBuilder().hasCharacter();

    // Build sorted indices by materialId to minimize descriptor set switches
    std::vector<size_t> sortedIndices(sceneObjects.size());
    std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](size_t a, size_t b) {
        return sceneObjects[a].materialId < sceneObjects[b].materialId;
    });

    MaterialId lastMaterialId = INVALID_MATERIAL_ID;
    VkDescriptorSet currentDescSet = VK_NULL_HANDLE;

    for (size_t i : sortedIndices) {
        // Skip player character (rendered separately with GPU skinning)
        if (hasCharacter && i == playerIndex) {
            continue;
        }

        const auto& obj = sceneObjects[i];

        // Only update descriptor set when material changes
        if (obj.materialId != lastMaterialId) {
            currentDescSet = materialRegistry.getDescriptorSet(obj.materialId, frameIndex);
            if (currentDescSet == VK_NULL_HANDLE) {
                // Skip objects with invalid materialId
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Skipping object with invalid materialId %u", obj.materialId);
                continue;
            }
            lastMaterialId = obj.materialId;
        }
        renderObject(obj, currentDescSet);
    }

    // Render procedural rocks (RockSystem uses its own descriptor sets)
    VkDescriptorSet rockDescSet = rockDescriptorSets[frameIndex];
    for (const auto& rock : systems_->rock().getSceneObjects()) {
        renderObject(rock, rockDescSet);
    }

    // Render woodland detritus (fallen branches - uses its own descriptor sets)
    // Bounds check: frameIndex must be within range, not just non-empty
    if (systems_->detritus() && frameIndex < detritusDescriptorSets.size()) {
        VkDescriptorSet detritusDescSet = detritusDescriptorSets[frameIndex];
        for (const auto& detritus : systems_->detritus()->getSceneObjects()) {
            renderObject(detritus, detritusDescSet);
        }
    }

    // Render procedural trees using dedicated TreeRenderer with wind animation
    if (systems_->tree() && systems_->treeRenderer()) {
        systems_->treeRenderer()->render(cmd, frameIndex, systems_->wind().getTime(), *systems_->tree(), systems_->treeLOD());
    }

    // Render tree impostors for distant trees
    if (systems_->treeLOD()) {
        auto* impostorCull = systems_->impostorCull();
        if (impostorCull && impostorCull->getTreeCount() > 0) {
            // Use GPU-culled indirect rendering
            systems_->treeLOD()->renderImpostorsGPUCulled(
                cmd, frameIndex,
                systems_->globalBuffers().uniformBuffers.buffers[frameIndex],
                systems_->shadow().getShadowImageView(),
                systems_->shadow().getShadowSampler(),
                impostorCull->getVisibleImpostorBuffer(),
                impostorCull->getIndirectDrawBuffer()
            );
        } else {
            // Fall back to CPU-culled rendering
            systems_->treeLOD()->renderImpostors(
                cmd, frameIndex,
                systems_->globalBuffers().uniformBuffers.buffers[frameIndex],
                systems_->shadow().getShadowImageView(),
                systems_->shadow().getShadowSampler()
            );
        }
    }
}

void Renderer::recordHDRPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime) {
    vk::CommandBuffer vkCmd(cmd);

    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    VkExtent2D rawExtent = systems_->postProcess().getExtent();
    vk::Extent2D hdrExtent = vk::Extent2D{}.setWidth(rawExtent.width).setHeight(rawExtent.height);

    auto hdrPassInfo = vk::RenderPassBeginInfo{}
        .setRenderPass(systems_->postProcess().getHDRRenderPass())
        .setFramebuffer(systems_->postProcess().getHDRFramebuffer())
        .setRenderArea(vk::Rect2D{{0, 0}, hdrExtent})
        .setClearValues(clearValues);

    vkCmd.beginRenderPass(hdrPassInfo, vk::SubpassContents::eInline);

    // Draw sky (with atmosphere LUT bindings)
    systems_->profiler().beginGpuZone(cmd, "HDR:Sky");
    systems_->sky().recordDraw(cmd, frameIndex);
    systems_->profiler().endGpuZone(cmd, "HDR:Sky");

    // Draw terrain (LEB adaptive tessellation)
    if (terrainEnabled) {
        systems_->profiler().beginGpuZone(cmd, "HDR:Terrain");
        systems_->terrain().recordDraw(cmd, frameIndex);
        systems_->profiler().endGpuZone(cmd, "HDR:Terrain");
    }

    // Draw Catmull-Clark subdivision surfaces
    systems_->profiler().beginGpuZone(cmd, "HDR:CatmullClark");
    systems_->catmullClark().recordDraw(cmd, frameIndex);
    systems_->profiler().endGpuZone(cmd, "HDR:CatmullClark");

    // Draw scene objects (static meshes)
    systems_->profiler().beginGpuZone(cmd, "HDR:SceneObjects");
    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **graphicsPipeline_);
    recordSceneObjects(cmd, frameIndex);
    systems_->profiler().endGpuZone(cmd, "HDR:SceneObjects");

    // Draw skinned character with GPU skinning
    systems_->profiler().beginGpuZone(cmd, "HDR:SkinnedChar");
    {
        SceneBuilder& sceneBuilder = systems_->scene().getSceneBuilder();
        if (sceneBuilder.hasCharacter()) {
            const auto& sceneObjects = sceneBuilder.getRenderables();
            size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
            if (playerIndex < sceneObjects.size()) {
                const Renderable& playerObj = sceneObjects[playerIndex];
                systems_->skinnedMesh().record(cmd, frameIndex, playerObj, sceneBuilder.getAnimatedCharacter());
            }
        }
    }
    systems_->profiler().endGpuZone(cmd, "HDR:SkinnedChar");

    // Draw grass
    systems_->profiler().beginGpuZone(cmd, "HDR:Grass");
    systems_->grass().recordDraw(cmd, frameIndex, grassTime);
    systems_->profiler().endGpuZone(cmd, "HDR:Grass");

    // Draw water surface (after opaque geometry, blended)
    // Use temporal tile culling: skip if no tiles were visible last frame
    if (!systems_->hasWaterTileCull() ||
        systems_->waterTileCull().wasWaterVisibleLastFrame(frameIndex)) {
        systems_->profiler().beginGpuZone(cmd, "HDR:Water");
        systems_->water().recordDraw(cmd, frameIndex);
        systems_->profiler().endGpuZone(cmd, "HDR:Water");
    }

    // Draw falling leaves - after grass, before weather
    systems_->profiler().beginGpuZone(cmd, "HDR:Leaves");
    systems_->leaf().recordDraw(cmd, frameIndex, grassTime);
    systems_->profiler().endGpuZone(cmd, "HDR:Leaves");

    // Draw weather particles (rain/snow) - after opaque geometry
    systems_->profiler().beginGpuZone(cmd, "HDR:Weather");
    systems_->weather().recordDraw(cmd, frameIndex, grassTime);
    systems_->profiler().endGpuZone(cmd, "HDR:Weather");

    // Draw debug lines (if any are present - includes physics debug and road/river visualization)
    if (systems_->debugLine().hasLines()) {
        // Set up viewport and scissor for debug rendering
        auto viewport = vk::Viewport{}
            .setX(0.0f)
            .setY(0.0f)
            .setWidth(static_cast<float>(systems_->postProcess().getExtent().width))
            .setHeight(static_cast<float>(systems_->postProcess().getExtent().height))
            .setMinDepth(0.0f)
            .setMaxDepth(1.0f);
        vkCmd.setViewport(0, viewport);

        VkExtent2D debugExtent = systems_->postProcess().getExtent();
        auto scissor = vk::Rect2D{}
            .setOffset({0, 0})
            .setExtent(vk::Extent2D{}.setWidth(debugExtent.width).setHeight(debugExtent.height));
        vkCmd.setScissor(0, scissor);

        // Need to get viewProj from the current frame data
        // For now, use the last known values (could be improved by passing as parameter)
        systems_->debugLine().recordCommands(cmd, lastViewProj);
    }

    vkCmd.endRenderPass();
}

void Renderer::recordHDRPassWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime,
                                            const std::vector<vk::CommandBuffer>& secondaries) {
    vk::CommandBuffer vkCmd(cmd);

    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    VkExtent2D rawExtent = systems_->postProcess().getExtent();
    vk::Extent2D hdrExtent = vk::Extent2D{}.setWidth(rawExtent.width).setHeight(rawExtent.height);

    auto hdrPassInfo = vk::RenderPassBeginInfo{}
        .setRenderPass(systems_->postProcess().getHDRRenderPass())
        .setFramebuffer(systems_->postProcess().getHDRFramebuffer())
        .setRenderArea(vk::Rect2D{{0, 0}, hdrExtent})
        .setClearValues(clearValues);

    // Begin render pass with secondary command buffer execution
    vkCmd.beginRenderPass(hdrPassInfo, vk::SubpassContents::eSecondaryCommandBuffers);

    // Execute all pre-recorded secondary command buffers
    // Secondary buffers include all draw calls including debug lines
    if (!secondaries.empty()) {
        vkCmd.executeCommands(secondaries);
    }

    vkCmd.endRenderPass();
}

void Renderer::recordHDRPassSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime, uint32_t slot) {
    vk::CommandBuffer vkCmd(cmd);

    // Each slot records a group of draw calls to a secondary command buffer
    // The secondary buffer has already been begun with render pass inheritance

    switch (slot) {
    case 0:
        // Slot 0: Sky + Terrain + Catmull-Clark (geometry base)
        systems_->profiler().beginGpuZone(cmd, "HDR:Sky");
        systems_->sky().recordDraw(cmd, frameIndex);
        systems_->profiler().endGpuZone(cmd, "HDR:Sky");

        if (terrainEnabled) {
            systems_->profiler().beginGpuZone(cmd, "HDR:Terrain");
            systems_->terrain().recordDraw(cmd, frameIndex);
            systems_->profiler().endGpuZone(cmd, "HDR:Terrain");
        }

        systems_->profiler().beginGpuZone(cmd, "HDR:CatmullClark");
        systems_->catmullClark().recordDraw(cmd, frameIndex);
        systems_->profiler().endGpuZone(cmd, "HDR:CatmullClark");
        break;

    case 1:
        // Slot 1: Scene Objects + Skinned Character (scene meshes)
        systems_->profiler().beginGpuZone(cmd, "HDR:SceneObjects");
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **graphicsPipeline_);
        recordSceneObjects(cmd, frameIndex);
        systems_->profiler().endGpuZone(cmd, "HDR:SceneObjects");

        systems_->profiler().beginGpuZone(cmd, "HDR:SkinnedChar");
        {
            SceneBuilder& sceneBuilder = systems_->scene().getSceneBuilder();
            if (sceneBuilder.hasCharacter()) {
                const auto& sceneObjects = sceneBuilder.getRenderables();
                size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
                if (playerIndex < sceneObjects.size()) {
                    const Renderable& playerObj = sceneObjects[playerIndex];
                    systems_->skinnedMesh().record(cmd, frameIndex, playerObj, sceneBuilder.getAnimatedCharacter());
                }
            }
        }
        systems_->profiler().endGpuZone(cmd, "HDR:SkinnedChar");
        break;

    case 2:
        // Slot 2: Grass + Water + Leaves + Weather + Debug lines (vegetation/effects/debug)
        systems_->profiler().beginGpuZone(cmd, "HDR:Grass");
        systems_->grass().recordDraw(cmd, frameIndex, grassTime);
        systems_->profiler().endGpuZone(cmd, "HDR:Grass");

        if (!systems_->hasWaterTileCull() ||
            systems_->waterTileCull().wasWaterVisibleLastFrame(frameIndex)) {
            systems_->profiler().beginGpuZone(cmd, "HDR:Water");
            systems_->water().recordDraw(cmd, frameIndex);
            systems_->profiler().endGpuZone(cmd, "HDR:Water");
        }

        systems_->profiler().beginGpuZone(cmd, "HDR:Leaves");
        systems_->leaf().recordDraw(cmd, frameIndex, grassTime);
        systems_->profiler().endGpuZone(cmd, "HDR:Leaves");

        systems_->profiler().beginGpuZone(cmd, "HDR:Weather");
        systems_->weather().recordDraw(cmd, frameIndex, grassTime);
        systems_->profiler().endGpuZone(cmd, "HDR:Weather");

        // Debug lines (recorded in secondary buffer for thread safety)
        if (systems_->debugLine().hasLines()) {
            VkExtent2D debugExtent = systems_->postProcess().getExtent();
            auto viewport = vk::Viewport{}
                .setX(0.0f)
                .setY(0.0f)
                .setWidth(static_cast<float>(debugExtent.width))
                .setHeight(static_cast<float>(debugExtent.height))
                .setMinDepth(0.0f)
                .setMaxDepth(1.0f);
            vkCmd.setViewport(0, viewport);

            auto scissor = vk::Rect2D{}
                .setOffset({0, 0})
                .setExtent(vk::Extent2D{}.setWidth(debugExtent.width).setHeight(debugExtent.height));
            vkCmd.setScissor(0, scissor);

            systems_->debugLine().recordCommands(cmd, lastViewProj);
        }
        break;

    default:
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "recordHDRPassSecondarySlot: Unknown slot %u", slot);
        break;
    }
}

// ===== GPU Skinning Implementation =====

bool Renderer::initSkinnedMeshRenderer() {
    SkinnedMeshRenderer::InitInfo info{};
    info.device = vulkanContext_->getVkDevice();
    info.raiiDevice = &vulkanContext_->getRaiiDevice();
    info.allocator = vulkanContext_->getAllocator();
    info.descriptorPool = &*descriptorManagerPool;
    info.renderPass = systems_->postProcess().getHDRRenderPass();
    info.extent = vulkanContext_->getVkSwapchainExtent();
    info.shaderPath = resourcePath + "/shaders";
    info.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    info.addCommonBindings = [this](DescriptorManager::LayoutBuilder& builder) {
        addCommonDescriptorBindings(builder);
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

void Renderer::updateHiZObjectData() {
    std::vector<CullObjectData> cullObjects;

    // Gather scene objects for culling
    const auto& sceneObjects = systems_->scene().getRenderables();
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        const auto& obj = sceneObjects[i];
        if (obj.mesh == nullptr) continue;

        // Get local AABB and transform to world space
        const AABB& localBounds = obj.mesh->getBounds();
        AABB worldBounds = localBounds.transformed(obj.transform);

        CullObjectData cullData{};

        // Calculate bounding sphere from transformed AABB
        glm::vec3 center = worldBounds.getCenter();
        glm::vec3 extents = worldBounds.getExtents();
        float radius = glm::length(extents);

        cullData.boundingSphere = glm::vec4(center, radius);
        cullData.aabbMin = glm::vec4(worldBounds.min, 0.0f);
        cullData.aabbMax = glm::vec4(worldBounds.max, 0.0f);
        cullData.meshIndex = static_cast<uint32_t>(i);
        cullData.firstIndex = 0;  // Single mesh per object
        cullData.indexCount = obj.mesh->getIndexCount();
        cullData.vertexOffset = 0;

        cullObjects.push_back(cullData);
    }

    // Also add procedural rocks
    const auto& rockObjects = systems_->rock().getSceneObjects();
    for (size_t i = 0; i < rockObjects.size(); ++i) {
        const auto& obj = rockObjects[i];
        if (obj.mesh == nullptr) continue;

        const AABB& localBounds = obj.mesh->getBounds();
        AABB worldBounds = localBounds.transformed(obj.transform);

        CullObjectData cullData{};
        glm::vec3 center = worldBounds.getCenter();
        glm::vec3 extents = worldBounds.getExtents();
        float radius = glm::length(extents);

        cullData.boundingSphere = glm::vec4(center, radius);
        cullData.aabbMin = glm::vec4(worldBounds.min, 0.0f);
        cullData.aabbMax = glm::vec4(worldBounds.max, 0.0f);
        cullData.meshIndex = static_cast<uint32_t>(sceneObjects.size() + i);
        cullData.firstIndex = 0;
        cullData.indexCount = obj.mesh->getIndexCount();
        cullData.vertexOffset = 0;

        cullObjects.push_back(cullData);
    }

    systems_->hiZ().updateObjectData(cullObjects);
}

// ============================================================================
// Cloud and sky exposure control (synced to multiple subsystems)
// ============================================================================

void Renderer::setCloudCoverage(float coverage) {
    cloudCoverage = glm::clamp(coverage, 0.0f, 1.0f);
    systems_->cloudShadow().setCloudCoverage(cloudCoverage);
    systems_->atmosphereLUT().setCloudCoverage(cloudCoverage);
}

void Renderer::setCloudDensity(float density) {
    cloudDensity = glm::clamp(density, 0.0f, 1.0f);
    systems_->cloudShadow().setCloudDensity(cloudDensity);
    systems_->atmosphereLUT().setCloudDensity(cloudDensity);
}

void Renderer::setSkyExposure(float exposure) {
    skyExposure = glm::clamp(exposure, 1.0f, 20.0f);
}

// Resource access
DescriptorManager::Pool* Renderer::getDescriptorPool() { return &*descriptorManagerPool; }
