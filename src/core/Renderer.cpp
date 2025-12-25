#define VMA_IMPLEMENTATION
#include "Renderer.h"
#include "RendererInit.h"
#include "RendererSystems.h"
#include "ShaderLoader.h"
#include "GraphicsPipelineFactory.h"
#include "MaterialDescriptorFactory.h"
#include "VulkanResourceFactory.h"
#include "Bindings.h"
#include "VulkanRAII.h"

// Subsystem headers for render pipeline lambda captures
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "SkySystem.h"
#include "GrassSystem.h"
#include "WindSystem.h"
#include "WeatherSystem.h"
#include "LeafSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "CloudShadowSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "CatmullClarkSystem.h"
#include "HiZSystem.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "DebugLineSystem.h"
#include "Profiler.h"
#include "SceneManager.h"
#include "GlobalBufferManager.h"
#include "SkinnedMeshRenderer.h"
#include "ErosionDataLoader.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"
#include "RockSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "DetritusSystem.h"
#include "UBOBuilder.h"
#include "WaterGBuffer.h"
#include "WaterDisplacement.h"
#include "SSRSystem.h"
#include "ResizeCoordinator.h"
#include "SceneBuilder.h"
#include "Mesh.h"

#include <SDL3/SDL_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <array>
#include <limits>

std::unique_ptr<Renderer> Renderer::create(const InitInfo& info) {
    std::unique_ptr<Renderer> instance(new Renderer());
    if (!instance->initInternal(info)) {
        return nullptr;
    }
    return instance;
}

Renderer::~Renderer() {
    cleanup();
}

bool Renderer::initInternal(const InitInfo& info) {
    window = info.window;
    resourcePath = info.resourcePath;
    config_ = info.config;

    // Create subsystems container
    systems_ = std::make_unique<RendererSystems>();

    // Initialize Vulkan context (instance, device, queues, allocator, swapchain)
    if (!vulkanContext.init(window)) {
        SDL_Log("Failed to initialize Vulkan context");
        return false;
    }

    // Phase 1: Core Vulkan resources (render pass, depth, framebuffers, command pool)
    if (!initCoreVulkanResources()) return false;

    // Phase 2: Descriptor infrastructure (layouts, pools)
    if (!initDescriptorInfrastructure()) return false;

    // Build shared InitContext for subsystem initialization
    // Pass pool sizes hint so subsystems can create consistent pools if needed
    InitContext initCtx = RendererInit::buildContext(
        vulkanContext, commandPool.get(), &*descriptorManagerPool,
        resourcePath, MAX_FRAMES_IN_FLIGHT, config_.descriptorPoolSizes);

    // Phase 3: All subsystems (terrain, grass, weather, snow, water, etc.)
    if (!initSubsystems(initCtx)) return false;

    // Phase 4: Control subsystems (after systems are ready)
    initControlSubsystems();

    // Phase 5: Resize coordinator registration
    initResizeCoordinator();

    // Setup render pipeline stages with lambdas
    setupRenderPipeline();
    SDL_Log("Render pipeline configured");

    return true;
}

void Renderer::setupRenderPipeline() {
    // Clear any existing passes
    renderPipeline.clear();

    // ===== COMPUTE STAGE =====
    // Terrain compute pass (adaptive subdivision)
    renderPipeline.computeStage.addPass("terrain", [this](RenderContext& ctx) {
        if (!terrainEnabled) return;
        systems_->profiler().beginGpuZone(ctx.cmd, "TerrainCompute");
        systems_->terrain().recordCompute(ctx.cmd, ctx.frameIndex, &systems_->profiler().getGpuProfiler());
        systems_->profiler().endGpuZone(ctx.cmd, "TerrainCompute");
    });

    // Catmull-Clark subdivision compute pass
    renderPipeline.computeStage.addPass("subdivision", [this](RenderContext& ctx) {
        systems_->profiler().beginGpuZone(ctx.cmd, "SubdivisionCompute");
        systems_->catmullClark().recordCompute(ctx.cmd, ctx.frameIndex);
        systems_->profiler().endGpuZone(ctx.cmd, "SubdivisionCompute");
    });

    // Grass compute pass (displacement + simulation)
    renderPipeline.computeStage.addPass("grass", [this](RenderContext& ctx) {
        systems_->profiler().beginGpuZone(ctx.cmd, "GrassCompute");
        systems_->grass().recordDisplacementUpdate(ctx.cmd, ctx.frameIndex);
        systems_->grass().recordResetAndCompute(ctx.cmd, ctx.frameIndex, ctx.frame.time);
        systems_->profiler().endGpuZone(ctx.cmd, "GrassCompute");
    });

    // Weather particle compute pass
    renderPipeline.computeStage.addPass("weather", [this](RenderContext& ctx) {
        systems_->profiler().beginGpuZone(ctx.cmd, "WeatherCompute");
        systems_->weather().recordResetAndCompute(ctx.cmd, ctx.frameIndex, ctx.frame.time, ctx.frame.deltaTime);
        systems_->profiler().endGpuZone(ctx.cmd, "WeatherCompute");
    });

    // Snow compute passes (mask + volumetric)
    renderPipeline.computeStage.addPass("snow", [this](RenderContext& ctx) {
        systems_->profiler().beginGpuZone(ctx.cmd, "SnowCompute");
        systems_->snowMask().recordCompute(ctx.cmd, ctx.frameIndex);
        systems_->volumetricSnow().recordCompute(ctx.cmd, ctx.frameIndex);
        systems_->profiler().endGpuZone(ctx.cmd, "SnowCompute");
    });

    // Leaf particle compute pass
    renderPipeline.computeStage.addPass("leaf", [this](RenderContext& ctx) {
        systems_->profiler().beginGpuZone(ctx.cmd, "LeafCompute");
        systems_->leaf().recordResetAndCompute(ctx.cmd, ctx.frameIndex, ctx.frame.time, ctx.frame.deltaTime);
        systems_->profiler().endGpuZone(ctx.cmd, "LeafCompute");
    });

    // Tree leaf culling compute pass
    renderPipeline.computeStage.addPass("treeLeafCull", [this](RenderContext& ctx) {
        if (!systems_->tree() || !systems_->treeRenderer()) return;
        if (!systems_->treeRenderer()->isLeafCullingEnabled()) return;

        systems_->profiler().beginGpuZone(ctx.cmd, "TreeLeafCull");
        systems_->treeRenderer()->recordLeafCulling(
            ctx.cmd, ctx.frameIndex, *systems_->tree(),
            systems_->treeLOD(),  // Pass LOD system for blend factor lookup
            ctx.frame.cameraPosition, ctx.frame.frustumPlanes);
        systems_->profiler().endGpuZone(ctx.cmd, "TreeLeafCull");
    });

    // Tree impostor Hi-Z occlusion culling compute pass (Phase 2)
    // Uses Hi-Z pyramid from previous frame for occlusion testing
    renderPipeline.computeStage.addPass("impostorCull", [this](RenderContext& ctx) {
        auto* impostorCull = systems_->impostorCull();
        if (!impostorCull || !systems_->tree()) return;

        systems_->profiler().beginGpuZone(ctx.cmd, "ImpostorCull");

        // Get Hi-Z pyramid from previous frame for occlusion testing
        VkImageView hiZView = systems_->hiZ().getHiZPyramidView();
        VkSampler hiZSampler = systems_->hiZ().getHiZSampler();

        // Build LOD params from settings
        ImpostorCullSystem::LODParams lodParams;
        if (systems_->treeLOD()) {
            const auto& lodSettings = systems_->treeLOD()->getLODSettings();
            lodParams.fullDetailDistance = lodSettings.fullDetailDistance;
            lodParams.impostorDistance = lodSettings.impostorDistance;
            lodParams.hysteresis = lodSettings.hysteresis;
            lodParams.blendRange = lodSettings.blendRange;
            lodParams.useScreenSpaceError = lodSettings.useScreenSpaceError;
            lodParams.errorThresholdFull = lodSettings.errorThresholdFull;
            lodParams.errorThresholdImpostor = lodSettings.errorThresholdImpostor;
            lodParams.errorThresholdCull = lodSettings.errorThresholdCull;
        }
        // Extract tanHalfFOV from projection matrix: proj[1][1] = 1/tan(fov/2)
        lodParams.tanHalfFOV = 1.0f / ctx.frame.projection[1][1];

        impostorCull->recordCulling(
            ctx.cmd, ctx.frameIndex,
            ctx.frame.cameraPosition,
            ctx.frame.frustumPlanes,
            ctx.frame.viewProj,
            hiZView, hiZSampler,
            lodParams
        );

        systems_->profiler().endGpuZone(ctx.cmd, "ImpostorCull");
    });

    // Water foam persistence compute pass
    renderPipeline.computeStage.addPass("foam", [this](RenderContext& ctx) {
        systems_->profiler().beginGpuZone(ctx.cmd, "FoamCompute");
        systems_->foam().recordCompute(ctx.cmd, ctx.frameIndex, ctx.frame.deltaTime,
                                 systems_->flowMap().getFlowMapView(), systems_->flowMap().getFlowMapSampler());
        systems_->profiler().endGpuZone(ctx.cmd, "FoamCompute");
    });

    // Cloud shadow map compute pass
    renderPipeline.computeStage.addPass("cloudShadow", [this](RenderContext& ctx) {
        if (!systems_->cloudShadow().isEnabled()) return;
        systems_->profiler().beginGpuZone(ctx.cmd, "CloudShadow");

        // Wind offset for cloud animation
        glm::vec2 windDir = systems_->wind().getWindDirection();
        float windSpeed = systems_->wind().getWindSpeed();
        float windTime = systems_->wind().getTime();
        float cloudTimeScale = 0.02f;
        glm::vec3 windOffset = glm::vec3(windDir.x * windSpeed * windTime * cloudTimeScale,
                                          windTime * 0.002f,
                                          windDir.y * windSpeed * windTime * cloudTimeScale);

        systems_->cloudShadow().recordUpdate(ctx.cmd, ctx.frameIndex, ctx.frame.sunDirection, ctx.frame.sunIntensity,
                                        windOffset, windTime * cloudTimeScale, ctx.frame.cameraPosition);
        systems_->profiler().endGpuZone(ctx.cmd, "CloudShadow");
    });

    // ===== SHADOW STAGE =====
    renderPipeline.shadowStage.setTerrainCallback([this](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (terrainEnabled) {
            // Need to get frameIndex from somewhere - we'll use currentFrame
            systems_->terrain().recordShadowDraw(cb, currentFrame, lightMatrix, static_cast<int>(cascade));
        }
    });

    renderPipeline.shadowStage.setGrassCallback([this](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;
        // Need grassTime from ctx - this callback doesn't have access to it
        // For now, use windSystem.getTime() as a proxy
        systems_->grass().recordShadowDraw(cb, currentFrame, systems_->wind().getTime(), cascade);
    });

    renderPipeline.shadowStage.setTreeCallback([this](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;
        if (systems_->tree() && systems_->treeRenderer()) {
            systems_->treeRenderer()->renderShadows(cb, currentFrame, *systems_->tree(), static_cast<int>(cascade), systems_->treeLOD());
        }
    });

    renderPipeline.shadowStage.getDescriptorSet = [this](uint32_t frameIndex) -> VkDescriptorSet {
        const auto& materialRegistry = systems_->scene().getSceneBuilder().getMaterialRegistry();
        return materialRegistry.getDescriptorSet(0, frameIndex);
    };

    renderPipeline.shadowStage.getSceneObjects = [this]() -> const std::vector<Renderable>& {
        return systems_->scene().getRenderables();
    };

    // ===== ATMOSPHERE/FROXEL STAGES =====
    renderPipeline.setFroxelStageFn([this](RenderContext& ctx) {
        systems_->profiler().beginGpuZone(ctx.cmd, "Atmosphere");

        UniformBufferObject* ubo = static_cast<UniformBufferObject*>(systems_->globalBuffers().uniformBuffers.mappedPointers[ctx.frameIndex]);
        glm::vec3 sunColor = glm::vec3(ubo->sunColor);

        // Froxel volumetric fog
        systems_->froxel().recordFroxelUpdate(ctx.cmd, ctx.frameIndex,
                                        ctx.frame.view, ctx.frame.projection,
                                        ctx.frame.cameraPosition,
                                        ctx.frame.sunDirection, ctx.frame.sunIntensity, sunColor,
                                        systems_->shadow().getCascadeMatrices().data(),
                                        ubo->cascadeSplits);

        // Recompute static LUTs if atmosphere parameters changed
        if (systems_->atmosphereLUT().needsRecompute()) {
            systems_->atmosphereLUT().recomputeStaticLUTs(ctx.cmd);
        }

        // Update sky-view LUT with current sun direction
        systems_->atmosphereLUT().updateSkyViewLUT(ctx.cmd, ctx.frameIndex, ctx.frame.sunDirection, ctx.frame.cameraPosition, 0.0f);

        // Update cloud map LUT with wind animation
        glm::vec2 windDir = systems_->wind().getWindDirection();
        float windSpeed = systems_->wind().getWindSpeed();
        float windTime = systems_->wind().getTime();
        float cloudTimeScale = 0.02f;
        glm::vec3 windOffset = glm::vec3(windDir.x * windSpeed * windTime * cloudTimeScale,
                                          windTime * 0.002f,
                                          windDir.y * windSpeed * windTime * cloudTimeScale);
        systems_->atmosphereLUT().updateCloudMapLUT(ctx.cmd, ctx.frameIndex, windOffset, windTime * cloudTimeScale);

        systems_->profiler().endGpuZone(ctx.cmd, "Atmosphere");
    });

    // ===== HDR STAGE =====
    // Sky rendering
    renderPipeline.hdrStage.addDrawCall("sky", [this](RenderContext& ctx) {
        systems_->sky().recordDraw(ctx.cmd, ctx.frameIndex);
    });

    // Terrain rendering
    renderPipeline.hdrStage.addDrawCall("terrain", [this](RenderContext& ctx) {
        if (terrainEnabled) {
            systems_->terrain().recordDraw(ctx.cmd, ctx.frameIndex);
        }
    });

    // Catmull-Clark subdivision surfaces
    renderPipeline.hdrStage.addDrawCall("catmullClark", [this](RenderContext& ctx) {
        systems_->catmullClark().recordDraw(ctx.cmd, ctx.frameIndex);
    });

    // Scene objects (static meshes)
    renderPipeline.hdrStage.addDrawCall("sceneObjects", [this](RenderContext& ctx) {
        vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline.get());
        recordSceneObjects(ctx.cmd, ctx.frameIndex);
    });

    // Skinned character (GPU skinning)
    renderPipeline.hdrStage.addDrawCall("skinnedCharacter", [this](RenderContext& ctx) {
        SceneBuilder& sceneBuilder = systems_->scene().getSceneBuilder();
        if (sceneBuilder.hasCharacter()) {
            const auto& sceneObjects = sceneBuilder.getRenderables();
            size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
            if (playerIndex < sceneObjects.size()) {
                const Renderable& playerObj = sceneObjects[playerIndex];
                systems_->skinnedMesh().record(ctx.cmd, ctx.frameIndex, playerObj, sceneBuilder.getAnimatedCharacter());
            }
        }
    });

    // Grass
    renderPipeline.hdrStage.addDrawCall("grass", [this](RenderContext& ctx) {
        systems_->grass().recordDraw(ctx.cmd, ctx.frameIndex, ctx.frame.time);
    });

    // Water surface (after opaque geometry)
    renderPipeline.hdrStage.addDrawCall("water", [this](RenderContext& ctx) {
        if (systems_->waterTileCull().wasWaterVisibleLastFrame(ctx.frameIndex)) {
            systems_->water().recordDraw(ctx.cmd, ctx.frameIndex);
        }
    });

    // Leaves (after grass, before weather)
    renderPipeline.hdrStage.addDrawCall("leaves", [this](RenderContext& ctx) {
        systems_->leaf().recordDraw(ctx.cmd, ctx.frameIndex, ctx.frame.time);
    });

    // Weather particles (rain/snow)
    renderPipeline.hdrStage.addDrawCall("weather", [this](RenderContext& ctx) {
        systems_->weather().recordDraw(ctx.cmd, ctx.frameIndex, ctx.frame.time);
    });

    // Physics debug lines
    renderPipeline.hdrStage.addDrawCall("debugLines", [this](RenderContext& ctx) {
#ifdef JPH_DEBUG_RENDERER
        if (physicsDebugEnabled && systems_->debugLine().hasLines()) {
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(systems_->postProcess().getExtent().width);
            viewport.height = static_cast<float>(systems_->postProcess().getExtent().height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(ctx.cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = systems_->postProcess().getExtent();
            vkCmdSetScissor(ctx.cmd, 0, 1, &scissor);

            systems_->debugLine().recordCommands(ctx.cmd, lastViewProj);
        }
#endif
    });

    // ===== POST STAGE =====
    renderPipeline.postStage.setHiZRecordFn([this](RenderContext& ctx) {
        systems_->profiler().beginGpuZone(ctx.cmd, "HiZPyramid");
        systems_->hiZ().recordPyramidGeneration(ctx.cmd, ctx.frameIndex);
        systems_->profiler().endGpuZone(ctx.cmd, "HiZPyramid");
    });

    renderPipeline.postStage.setBloomRecordFn([this](RenderContext& ctx) {
        systems_->profiler().beginGpuZone(ctx.cmd, "Bloom");
        systems_->bloom().setThreshold(systems_->postProcess().getBloomThreshold());
        systems_->bloom().recordBloomPass(ctx.cmd, systems_->postProcess().getHDRColorView());
        systems_->profiler().endGpuZone(ctx.cmd, "Bloom");
    });

    // Post-process is handled separately since it needs guiRenderCallback and framebuffer

    // Apply initial toggle state
    syncPerformanceToggles();
}

void Renderer::syncPerformanceToggles() {
    // Sync compute stage passes
    renderPipeline.computeStage.setPassEnabled("terrain", perfToggles.terrainCompute);
    renderPipeline.computeStage.setPassEnabled("subdivision", perfToggles.subdivisionCompute);
    renderPipeline.computeStage.setPassEnabled("grass", perfToggles.grassCompute);
    renderPipeline.computeStage.setPassEnabled("weather", perfToggles.weatherCompute);
    renderPipeline.computeStage.setPassEnabled("snow", perfToggles.snowCompute);
    renderPipeline.computeStage.setPassEnabled("leaf", perfToggles.leafCompute);
    renderPipeline.computeStage.setPassEnabled("foam", perfToggles.foamCompute);
    renderPipeline.computeStage.setPassEnabled("cloudShadow", perfToggles.cloudShadowCompute);

    // Sync HDR stage draw calls
    renderPipeline.hdrStage.setDrawCallEnabled("sky", perfToggles.skyDraw);
    renderPipeline.hdrStage.setDrawCallEnabled("terrain", perfToggles.terrainDraw);
    renderPipeline.hdrStage.setDrawCallEnabled("catmullClark", perfToggles.catmullClarkDraw);
    renderPipeline.hdrStage.setDrawCallEnabled("sceneObjects", perfToggles.sceneObjectsDraw);
    renderPipeline.hdrStage.setDrawCallEnabled("skinnedCharacter", perfToggles.skinnedCharacterDraw);
    renderPipeline.hdrStage.setDrawCallEnabled("grass", perfToggles.grassDraw);
    renderPipeline.hdrStage.setDrawCallEnabled("water", perfToggles.waterDraw);
    renderPipeline.hdrStage.setDrawCallEnabled("leaves", perfToggles.leavesDraw);
    renderPipeline.hdrStage.setDrawCallEnabled("weather", perfToggles.weatherDraw);
    renderPipeline.hdrStage.setDrawCallEnabled("debugLines", perfToggles.debugLinesDraw);

    // Sync post stage
    renderPipeline.postStage.setHiZEnabled(perfToggles.hiZPyramid);
    renderPipeline.postStage.setBloomEnabled(perfToggles.bloom);
}

// Note: initCoreVulkanResources(), initDescriptorInfrastructure(), initSubsystems(),
// and initResizeCoordinator() are implemented in RendererInitPhases.cpp

void Renderer::setWeatherIntensity(float intensity) {
    systems_->weather().setIntensity(intensity);
}

void Renderer::setWeatherType(uint32_t type) {
    systems_->weather().setWeatherType(type);
}

void Renderer::setPlayerPosition(const glm::vec3& position, float radius) {
    setPlayerState(position, glm::vec3(0.0f), radius);
}

void Renderer::setPlayerState(const glm::vec3& position, const glm::vec3& velocity, float radius) {
    playerPosition = position;
    playerVelocity = velocity;
    playerCapsuleRadius = radius;
}

#ifdef JPH_DEBUG_RENDERER
void Renderer::updatePhysicsDebug(PhysicsWorld& physics, const glm::vec3& cameraPos) {
    if (!physicsDebugEnabled) return;

    // Begin debug line frame (clear previous and set frame index)
    systems_->debugLine().beginFrame(currentFrame);

    // Create debug renderer on first use (after Jolt is initialized)
    if (!systems_->physicsDebugRenderer()) {
        InitContext initCtx = RendererInit::buildContext(
            vulkanContext, commandPool.get(), &*descriptorManagerPool,
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
    VkDevice device = vulkanContext.getDevice();
    VmaAllocator allocator = vulkanContext.getAllocator();

    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);

        // RAII handles cleanup of sync objects
        imageAvailableSemaphores.clear();
        renderFinishedSemaphores.clear();
        inFlightFences.clear();

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
        graphicsPipeline = ManagedPipeline();
        SDL_Log("destroying pipelineLayout");
        pipelineLayout = ManagedPipelineLayout();
        SDL_Log("destroying descriptorSetLayout");
        descriptorSetLayout = ManagedDescriptorSetLayout();
        SDL_Log("descriptor layouts destroyed");

        // RAII handles: commandPool
        SDL_Log("destroying commandPool");
        commandPool = ManagedCommandPool();
        SDL_Log("commandPool destroyed");

        // RAII handles: depth resources and framebuffers
        SDL_Log("clearing framebuffers");
        framebuffers.clear();
        SDL_Log("destroying depthSampler");
        depthSampler = ManagedSampler();
        SDL_Log("destroying depthImageView");
        depthImageView = ManagedImageView();
        SDL_Log("destroying depthImage");
        depthImage = ManagedImage();
        SDL_Log("destroying renderPass");
        renderPass = ManagedRenderPass();
        SDL_Log("render resources destroyed");
    }

    SDL_Log("calling vulkanContext.shutdown");
    vulkanContext.shutdown();
    SDL_Log("vulkanContext shutdown complete");
}

void Renderer::destroyRenderResources() {
    // RAII handles all cleanup - just clear containers and reset managed objects
    framebuffers.clear();
    depthSampler = ManagedSampler();
    depthImageView = ManagedImageView();
    depthImage = ManagedImage();
    renderPass = ManagedRenderPass();
}

bool Renderer::createRenderPass() {
    depthFormat = VK_FORMAT_D32_SFLOAT;

    VulkanResourceFactory::RenderPassConfig config{};
    config.colorFormat = vulkanContext.getSwapchainImageFormat();
    config.depthFormat = depthFormat;
    config.finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    config.finalDepthLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // For Hi-Z
    config.storeDepth = true;  // For Hi-Z pyramid generation

    VkRenderPass rawRenderPass = VK_NULL_HANDLE;
    if (!VulkanResourceFactory::createRenderPass(vulkanContext.getDevice(), config, rawRenderPass)) {
        return false;
    }
    renderPass = ManagedRenderPass::fromRaw(vulkanContext.getDevice(), rawRenderPass);
    return true;
}

bool Renderer::createDepthResources() {
    VulkanResourceFactory::DepthResources depth;
    if (!VulkanResourceFactory::createDepthResources(
            vulkanContext.getDevice(), vulkanContext.getAllocator(),
            vulkanContext.getSwapchainExtent(), depthFormat, depth)) {
        return false;
    }
    depthImage = ManagedImage::fromRaw(vulkanContext.getAllocator(), depth.image, depth.allocation);
    depthImageView = ManagedImageView::fromRaw(vulkanContext.getDevice(), depth.view);
    depthSampler = std::move(depth.sampler);
    return true;
}

bool Renderer::createFramebuffers() {
    std::vector<VkFramebuffer> rawFramebuffers;
    if (!VulkanResourceFactory::createFramebuffers(
        vulkanContext.getDevice(), renderPass.get(),
        vulkanContext.getSwapchainImageViews(), depthImageView.get(),
        vulkanContext.getSwapchainExtent(), rawFramebuffers)) {
        return false;
    }

    framebuffers.clear();
    framebuffers.reserve(rawFramebuffers.size());
    for (VkFramebuffer fb : rawFramebuffers) {
        framebuffers.push_back(ManagedFramebuffer::fromRaw(vulkanContext.getDevice(), fb));
    }
    return true;
}

bool Renderer::createCommandPool() {
    return ManagedCommandPool::create(
        vulkanContext.getDevice(),
        vulkanContext.getGraphicsQueueFamily(),
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        commandPool);
}

bool Renderer::createCommandBuffers() {
    return VulkanResourceFactory::createCommandBuffers(
        vulkanContext.getDevice(), commandPool.get(), MAX_FRAMES_IN_FLIGHT, commandBuffers);
}

bool Renderer::createSyncObjects() {
    VkDevice device = vulkanContext.getDevice();

    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (!ManagedSemaphore::create(device, imageAvailableSemaphores[i])) {
            return false;
        }
        if (!ManagedSemaphore::create(device, renderFinishedSemaphores[i])) {
            return false;
        }
        if (!ManagedFence::createSignaled(device, inFlightFences[i])) {
            return false;
        }
    }
    return true;
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
    VkDevice device = vulkanContext.getDevice();

    // Main scene descriptor set layout - uses common bindings (0-11, 13-16)
    DescriptorManager::LayoutBuilder builder(device);
    addCommonDescriptorBindings(builder);
    VkDescriptorSetLayout rawLayout = builder.build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create descriptor set layout");
        return false;
    }

    descriptorSetLayout = ManagedDescriptorSetLayout::fromRaw(device, rawLayout);
    return true;
}

bool Renderer::createGraphicsPipeline() {
    VkDevice device = vulkanContext.getDevice();
    VkExtent2D swapchainExtent = vulkanContext.getSwapchainExtent();

    // Create pipeline layout (still needed - factory expects it to be provided)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkDescriptorSetLayout rawDescSetLayout = descriptorSetLayout.get();
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &rawDescSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkPipelineLayout rawPipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &rawPipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create pipeline layout");
        return false;
    }
    pipelineLayout = ManagedPipelineLayout::fromRaw(device, rawPipelineLayout);

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
        .setPipelineLayout(pipelineLayout.get())
        .setExtent(swapchainExtent)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Alpha)
        .build(rawPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create graphics pipeline");
        return false;
    }

    graphicsPipeline = ManagedPipeline::fromRaw(device, rawPipeline);
    return true;
}

void Renderer::updateLightBuffer(uint32_t currentImage, const Camera& camera) {
    LightBuffer buffer{};
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    systems_->scene().getLightManager().buildLightBuffer(buffer, camera.getPosition(), camera.getFront(), viewProj, lightCullRadius);
    systems_->globalBuffers().updateLightBuffer(currentImage, buffer);
}

bool Renderer::createDescriptorPool() {
    VkDevice device = vulkanContext.getDevice();

    // Create the auto-growing descriptor pool with configurable sizes
    // Will automatically grow if exhausted
    // All subsystems now use this managed pool for consistent descriptor allocation
    descriptorManagerPool.emplace(device, config_.setsPerPool, config_.descriptorPoolSizes);

    return true;
}

bool Renderer::createDescriptorSets() {
    VkDevice device = vulkanContext.getDevice();

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
        common.emissiveMapView = systems_->scene().getSceneBuilder().getDefaultEmissiveMap().getImageView();
        common.emissiveMapSampler = systems_->scene().getSceneBuilder().getDefaultEmissiveMap().getSampler();
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
        common.placeholderTextureView = systems_->scene().getSceneBuilder().getWhiteTexture().getImageView();
        common.placeholderTextureSampler = systems_->scene().getSceneBuilder().getWhiteTexture().getSampler();
        return common;
    };

    materialRegistry.createDescriptorSets(
        device,
        *descriptorManagerPool,
        descriptorSetLayout.get(),
        MAX_FRAMES_IN_FLIGHT,
        getCommonBindings);

    if (!materialRegistry.hasDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create MaterialRegistry descriptor sets");
        return false;
    }

    // Rock descriptor sets (RockSystem has its own textures, not in MaterialRegistry)
    // Note: rockSystem is not initialized at this point - allocation only, writing done in initPhase2
    rockDescriptorSets = descriptorManagerPool->allocate(descriptorSetLayout.get(), MAX_FRAMES_IN_FLIGHT);
    if (rockDescriptorSets.empty()) {
        SDL_Log("Failed to allocate rock descriptor sets");
        return false;
    }

    // Detritus descriptor sets (fallen branches - allocation only, writing done in initPhase2)
    detritusDescriptorSets = descriptorManagerPool->allocate(descriptorSetLayout.get(), MAX_FRAMES_IN_FLIGHT);
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

    VkDevice device = vulkanContext.getDevice();
    VkSwapchainKHR swapchain = vulkanContext.getSwapchain();
    VkQueue graphicsQueue = vulkanContext.getGraphicsQueue();
    VkQueue presentQueue = vulkanContext.getPresentQueue();

    // Handle pending resize before acquiring next image
    if (framebufferResized) {
        handleResize();
        swapchain = vulkanContext.getSwapchain();  // Update after resize
        framebufferResized = false;
    }

    // Skip rendering if window is minimized
    VkExtent2D extent = vulkanContext.getSwapchainExtent();
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    // Frame synchronization
    systems_->profiler().beginCpuZone("FenceWait");
    inFlightFences[currentFrame].wait();
    systems_->profiler().endCpuZone("FenceWait");

    systems_->profiler().beginCpuZone("AcquireImage");
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                            imageAvailableSemaphores[currentFrame].get(), VK_NULL_HANDLE, &imageIndex);
    systems_->profiler().endCpuZone("AcquireImage");

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

    inFlightFences[currentFrame].resetFence();

    // Update time system (frame timing and day/night cycle)
    TimingData timing = systems_->time().update();

    // Begin CPU profiling for this frame
    systems_->profiler().beginCpuZone("UniformUpdates");

    // Update uniform buffer data
    updateUniformBuffer(currentFrame, camera);

    // Update bone matrices for GPU skinning
    SceneBuilder& sceneBuilder = systems_->scene().getSceneBuilder();
    AnimatedCharacter* character = sceneBuilder.hasCharacter() ? &sceneBuilder.getAnimatedCharacter() : nullptr;
    systems_->skinnedMesh().updateBoneMatrices(currentFrame, character);

    systems_->profiler().endCpuZone("UniformUpdates");

    // Build per-frame shared state
    FrameData frame = buildFrameData(camera, timing.deltaTime, timing.elapsedTime);

    // Cache view-projection for debug rendering
    lastViewProj = frame.viewProj;

    // Upload debug lines if enabled (lines were collected in updatePhysicsDebug before render)
#ifdef JPH_DEBUG_RENDERER
    if (physicsDebugEnabled && systems_->debugLine().hasLines()) {
        systems_->debugLine().uploadLines();
    }
#endif

    // Update subsystems (state mutations)
    systems_->profiler().beginCpuZone("SystemUpdates");

    systems_->wind().update(frame.deltaTime);
    systems_->wind().updateUniforms(frame.frameIndex);

    // Update tree renderer descriptor sets with current frame resources and textures
    if (systems_->treeRenderer() && systems_->tree()) {
        VkDescriptorBufferInfo windInfo = systems_->wind().getBufferInfo(frame.frameIndex);

        // Update descriptor sets for each bark texture type
        for (const auto& barkType : systems_->tree()->getBarkTextureTypes()) {
            Texture* barkTex = systems_->tree()->getBarkTexture(barkType);
            Texture* barkNormal = systems_->tree()->getBarkNormalMap(barkType);

            systems_->treeRenderer()->updateBarkDescriptorSet(
                frame.frameIndex,
                barkType,
                systems_->globalBuffers().uniformBuffers.buffers[frame.frameIndex],
                windInfo.buffer,
                systems_->shadow().getShadowImageView(),
                systems_->shadow().getShadowSampler(),
                barkTex->getImageView(),
                barkNormal->getImageView(),
                barkTex->getImageView(),  // roughness placeholder
                barkTex->getImageView(),  // AO placeholder
                barkTex->getSampler());
        }

        // Update descriptor sets for each leaf texture type
        for (const auto& leafType : systems_->tree()->getLeafTextureTypes()) {
            Texture* leafTex = systems_->tree()->getLeafTexture(leafType);

            systems_->treeRenderer()->updateLeafDescriptorSet(
                frame.frameIndex,
                leafType,
                systems_->globalBuffers().uniformBuffers.buffers[frame.frameIndex],
                windInfo.buffer,
                systems_->shadow().getShadowImageView(),
                systems_->shadow().getShadowSampler(),
                leafTex->getImageView(),
                leafTex->getSampler(),
                systems_->tree()->getLeafInstanceBuffer(),
                systems_->tree()->getLeafInstanceBufferSize());
        }

        // Update culled leaf descriptor sets (for GPU culling path)
        // Update for each leaf type to support multiple textures
        for (const auto& leafType : systems_->tree()->getLeafTextureTypes()) {
            Texture* leafTex = systems_->tree()->getLeafTexture(leafType);
            if (leafTex) {
                systems_->treeRenderer()->updateCulledLeafDescriptorSet(
                    frame.frameIndex,
                    leafType,
                    systems_->globalBuffers().uniformBuffers.buffers[frame.frameIndex],
                    windInfo.buffer,
                    systems_->shadow().getShadowImageView(),
                    systems_->shadow().getShadowSampler(),
                    leafTex->getImageView(),
                    leafTex->getSampler());
            }
        }
    }

    systems_->grass().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.viewProj,
                               frame.terrainSize, frame.heightScale);
    systems_->grass().updateDisplacementSources(frame.playerPosition, frame.playerCapsuleRadius, frame.deltaTime);
    systems_->weather().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.viewProj, frame.deltaTime, frame.time, systems_->wind());
    systems_->terrain().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.view, frame.projection,
                                  systems_->volumetricSnow().getCascadeParams(), useVolumetricSnow, MAX_SNOW_HEIGHT);

    // Update snow mask system - accumulation/melting based on weather type
    bool isSnowing = (systems_->weather().getWeatherType() == 1);  // 1 = snow
    float weatherIntensity = systems_->weather().getIntensity();
    auto& envSettings = systems_->environmentSettings();
    // Auto-adjust snow amount based on weather state
    if (isSnowing && weatherIntensity > 0.0f) {
        envSettings.snowAmount = glm::min(envSettings.snowAmount + envSettings.snowAccumulationRate * frame.deltaTime, 1.0f);
    } else if (envSettings.snowAmount > 0.0f) {
        envSettings.snowAmount = glm::max(envSettings.snowAmount - envSettings.snowMeltRate * frame.deltaTime, 0.0f);
    }
    systems_->snowMask().setMaskCenter(frame.cameraPosition);
    systems_->snowMask().updateUniforms(frame.frameIndex, frame.deltaTime, isSnowing, weatherIntensity, envSettings);

    // Update volumetric snow system
    systems_->volumetricSnow().setCameraPosition(frame.cameraPosition);
    systems_->volumetricSnow().setWindDirection(glm::vec2(systems_->wind().getEnvironmentSettings().windDirection.x,
                                                     systems_->wind().getEnvironmentSettings().windDirection.y));
    systems_->volumetricSnow().setWindStrength(systems_->wind().getEnvironmentSettings().windStrength);
    systems_->volumetricSnow().updateUniforms(frame.frameIndex, frame.deltaTime, isSnowing, weatherIntensity, envSettings);

    // Add player footprint interaction with snow
    if (envSettings.snowAmount > 0.1f) {
        systems_->snowMask().addInteraction(frame.playerPosition, frame.playerCapsuleRadius * 1.5f, 0.3f);
        systems_->volumetricSnow().addInteraction(frame.playerPosition, frame.playerCapsuleRadius * 1.5f, 0.3f);
    }

    // Update leaf system with player position and velocity for disruption
    systems_->leaf().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.viewProj, frame.cameraPosition, frame.playerVelocity, frame.deltaTime, frame.time,
                               frame.terrainSize, frame.heightScale);

    // Update tree LOD system for impostor rendering
    if (systems_->treeLOD() && systems_->tree()) {
        // Enable GPU culling optimization when ImpostorCullSystem is available
        // This skips expensive CPU impostor list building since GPU handles it
        auto* impostorCull = systems_->impostorCull();
        bool gpuCullingAvailable = impostorCull && impostorCull->getTreeCount() > 0;
        systems_->treeLOD()->setGPUCullingEnabled(gpuCullingAvailable);

        // Compute screen params for screen-space error LOD
        TreeLODSystem::ScreenParams screenParams;
        screenParams.screenHeight = static_cast<float>(extent.height);
        // Extract tanHalfFOV from projection matrix: proj[1][1] = 1/tan(fov/2)
        screenParams.tanHalfFOV = 1.0f / frame.projection[1][1];
        systems_->treeLOD()->update(frame.deltaTime, frame.cameraPosition, *systems_->tree(), screenParams);
    }

    // Update water system uniforms
    systems_->water().updateUniforms(frame.frameIndex);

    // Update underwater state for postprocess (Water Volume Renderer Phase 2)
    auto underwaterParams = systems_->water().getUnderwaterParams(frame.cameraPosition);
    systems_->postProcess().setUnderwaterState(
        underwaterParams.isUnderwater,
        underwaterParams.depth,
        underwaterParams.absorptionCoeffs,
        underwaterParams.turbidity,
        underwaterParams.waterColor,
        underwaterParams.waterLevel
    );

    systems_->profiler().endCpuZone("SystemUpdates");

    // Begin command buffer recording
    vkResetCommandBuffer(commandBuffers[frame.frameIndex], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffers[frame.frameIndex], &beginInfo);

    VkCommandBuffer cmd = commandBuffers[frame.frameIndex];

    // Begin GPU profiling frame
    systems_->profiler().beginFrame(cmd, frame.frameIndex);

    // Build render resources and context for pipeline stages
    RenderResources resources = buildRenderResources(imageIndex);
    RenderContext ctx(cmd, frame.frameIndex, frame, resources);

    // Execute all compute passes via pipeline
    renderPipeline.computeStage.execute(ctx);

    // Shadow pass (skip when sun is below horizon or shadows disabled)
    if (lastSunIntensity > 0.001f && perfToggles.shadowPass) {
        systems_->profiler().beginGpuZone(cmd, "ShadowPass");
        recordShadowPass(cmd, frame.frameIndex, frame.time);
        systems_->profiler().endGpuZone(cmd, "ShadowPass");
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
        systems_->waterTileCull().wasWaterVisibleLastFrame(frame.frameIndex)) {
        systems_->profiler().beginGpuZone(cmd, "WaterGBuffer");
        systems_->waterGBuffer().beginRenderPass(cmd);

        // Bind G-buffer pipeline and descriptor set
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, systems_->waterGBuffer().getPipeline());
        VkDescriptorSet gbufferDescSet = systems_->waterGBuffer().getDescriptorSet(frame.frameIndex);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                systems_->waterGBuffer().getPipelineLayout(), 0, 1,
                                &gbufferDescSet, 0, nullptr);

        // Draw water mesh
        systems_->water().recordMeshDraw(cmd);

        systems_->waterGBuffer().endRenderPass(cmd);
        systems_->profiler().endGpuZone(cmd, "WaterGBuffer");
    }

    // HDR scene render pass (can be disabled for performance debugging)
    // Note: HDRPass is not wrapped in a profiler zone because recordHDRPass()
    // contains granular HDR:* sub-zones. Nesting would confuse the profiler.
    if (hdrPassEnabled) {
        recordHDRPass(cmd, frame.frameIndex, frame.time);

        // Screen-Space Reflections compute pass (Phase 10)
        // Computes SSR for next frame's water - uses current scene for temporal stability
        if (perfToggles.ssr && systems_->ssr().isEnabled()) {
            systems_->profiler().beginGpuZone(cmd, "SSR");
            systems_->ssr().recordCompute(cmd, frame.frameIndex,
                                    systems_->postProcess().getHDRColorView(),
                                    systems_->postProcess().getHDRDepthView(),
                                    frame.view, frame.projection,
                                    frame.cameraPosition);
            systems_->profiler().endGpuZone(cmd, "SSR");
        }

        // Water tile culling compute pass (Phase 7)
        // Determines which screen tiles contain water for optimized rendering
        if (perfToggles.waterTileCull && systems_->waterTileCull().isEnabled()) {
            systems_->profiler().beginGpuZone(cmd, "WaterTileCull");
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
        renderPipeline.postStage.hiZRecordFn(ctx);
    }
    // Only run bloom passes if bloom is enabled (skip for performance)
    if (systems_->postProcess().isBloomEnabled() && renderPipeline.postStage.bloomRecordFn) {
        renderPipeline.postStage.bloomRecordFn(ctx);
    }

    // Bilateral grid for local tone mapping (if enabled)
    if (systems_->postProcess().isLocalToneMapEnabled()) {
        systems_->profiler().beginGpuZone(cmd, "BilateralGrid");
        systems_->bilateralGrid().recordBilateralGrid(cmd, frame.frameIndex,
                                                       systems_->postProcess().getHDRColorView());
        systems_->profiler().endGpuZone(cmd, "BilateralGrid");
    }

    // Post-process pass (with optional GUI overlay callback)
    // Note: This is not in postStage because it needs framebuffer and guiRenderCallback
    systems_->profiler().beginGpuZone(cmd, "PostProcess");
    systems_->postProcess().recordPostProcess(cmd, frame.frameIndex, framebuffers[imageIndex].get(), frame.deltaTime, guiRenderCallback);
    systems_->profiler().endGpuZone(cmd, "PostProcess");

    // End GPU profiling frame
    systems_->profiler().endFrame(cmd, frame.frameIndex);

    vkEndCommandBuffer(cmd);

    // Queue submission
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame].get()};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame].get()};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame].get());
    if (result == VK_ERROR_DEVICE_LOST) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Device lost during queue submit");
        framebufferResized = true;
        return false;
    } else if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to submit draw command buffer: %d", result);
        return false;
    }

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        framebufferResized = true;
    } else if (result == VK_ERROR_SURFACE_LOST_KHR || result == VK_ERROR_DEVICE_LOST) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Surface/device lost during present, will recover");
        framebufferResized = true;
    } else if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to present swapchain image: %d", result);
    }

    // Advance grass double-buffer sets after frame submission
    // This swaps compute/render buffer sets so next frame can overlap:
    // - Next frame's compute writes to what was the render set
    // - Next frame's render reads from what was the compute set (now contains fresh data)
    systems_->grass().advanceBufferSet();
    systems_->weather().advanceBufferSet();
    systems_->leaf().advanceBufferSet();

    // Update water tile cull visibility tracking (uses absolute frame counter)
    systems_->waterTileCull().endFrame(currentFrame);

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    return true;
}

void Renderer::waitIdle() {
    vulkanContext.waitIdle();
}

void Renderer::waitForPreviousFrame() {
    // Wait for the previous frame's fence to ensure GPU is done with resources
    // we might be about to destroy/update.
    //
    // With double-buffering (MAX_FRAMES_IN_FLIGHT=2):
    // - Frame N uses fence[N % 2]
    // - Before updating meshes for frame N, we need frame N-1's GPU work complete
    // - Previous frame's fence is fence[(N-1) % 2] = fence[(currentFrame + 1) % 2]
    //
    // This prevents race conditions where we destroy mesh buffers while the GPU
    // is still reading them from the previous frame's commands.
    uint32_t previousFrame = (currentFrame + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;
    inFlightFences[previousFrame].wait();
}

void Renderer::destroyDepthImageAndView() {
    // RAII handles cleanup - just reset the managed objects
    depthImageView = ManagedImageView();
    depthImage = ManagedImage();
}

void Renderer::destroyFramebuffers() {
    // RAII handles cleanup - just clear the vector
    framebuffers.clear();
}

bool Renderer::recreateDepthResources(VkExtent2D newExtent) {
    destroyDepthImageAndView();

    VkImage rawImage = VK_NULL_HANDLE;
    VmaAllocation rawAllocation = VK_NULL_HANDLE;
    VkImageView rawView = VK_NULL_HANDLE;

    if (!VulkanResourceFactory::createDepthImageAndView(
        vulkanContext.getDevice(), vulkanContext.getAllocator(),
        newExtent, depthFormat,
        rawImage, rawAllocation, rawView)) {
        return false;
    }

    depthImage = ManagedImage::fromRaw(vulkanContext.getAllocator(), rawImage, rawAllocation);
    depthImageView = ManagedImageView::fromRaw(vulkanContext.getDevice(), rawView);
    return true;
}

bool Renderer::handleResize() {
    // Delegate all resize logic to the coordinator (pass {0,0} to trigger core handler)
    bool success = systems_->resizeCoordinator().performResize(
        vulkanContext.getDevice(),
        vulkanContext.getAllocator(),
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

    frame.frameIndex = currentFrame;
    frame.deltaTime = deltaTime;
    frame.time = time;
    frame.timeOfDay = systems_->time().getTimeOfDay();

    frame.cameraPosition = camera.getPosition();
    frame.view = camera.getViewMatrix();
    frame.projection = camera.getProjectionMatrix();
    frame.viewProj = frame.projection * frame.view;

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
    UniformBufferObject* ubo = static_cast<UniformBufferObject*>(systems_->globalBuffers().uniformBuffers.mappedPointers[currentFrame]);
    frame.sunDirection = glm::normalize(glm::vec3(ubo->sunDirection));
    frame.sunIntensity = ubo->sunDirection.w;

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
    resources.swapchainRenderPass = renderPass.get();
    resources.swapchainFramebuffer = framebuffers[swapchainImageIndex].get();
    resources.swapchainExtent = {vulkanContext.getWidth(), vulkanContext.getHeight()};

    // Main scene pipeline
    resources.graphicsPipeline = graphicsPipeline.get();
    resources.pipelineLayout = pipelineLayout.get();
    resources.descriptorSetLayout = descriptorSetLayout.get();

    return resources;
}

// Render pass recording helpers - pure command recording, no state mutation

void Renderer::recordShadowPass(VkCommandBuffer cmd, uint32_t frameIndex, float grassTime) {
    // Delegate to the shadow system with callbacks for terrain and grass
    auto terrainCallback = [this, frameIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (terrainEnabled && perfToggles.terrainShadows) {
            systems_->terrain().recordShadowDraw(cb, frameIndex, lightMatrix, static_cast<int>(cascade));
        }
    };

    auto grassCallback = [this, frameIndex, grassTime](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;  // Grass uses cascade index only
        if (perfToggles.grassShadows) {
            systems_->grass().recordShadowDraw(cb, frameIndex, grassTime, cascade);
        }
    };

    auto treeCallback = [this, frameIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;
        if (systems_->tree() && systems_->treeRenderer()) {
            systems_->treeRenderer()->renderShadows(cb, frameIndex, *systems_->tree(), static_cast<int>(cascade), systems_->treeLOD());
        }
        // Render impostor shadows
        if (systems_->treeLOD()) {
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

            const Renderable& playerObj = sceneObjs[playerIndex];
            AnimatedCharacter& character = sceneBuilder.getAnimatedCharacter();
            SkinnedMesh& skinnedMesh = character.getSkinnedMesh();

            // Bind skinned shadow pipeline with descriptor set that has bone matrices
            systems_->shadow().bindSkinnedShadowPipeline(cb, systems_->skinnedMesh().getDescriptorSet(frameIndex));

            // Record the skinned mesh shadow
            systems_->shadow().recordSkinnedMeshShadow(cb, cascade, playerObj.transform, skinnedMesh);
        };
    }

    // Use any MaterialRegistry descriptor set for shadow pass (only needs common bindings/UBO)
    // MaterialId 0 is the first registered material (crate)
    const auto& materialRegistry = systems_->scene().getSceneBuilder().getMaterialRegistry();
    VkDescriptorSet shadowDescriptorSet = materialRegistry.getDescriptorSet(0, frameIndex);
    systems_->shadow().recordShadowPass(cmd, frameIndex, shadowDescriptorSet,
                                   allObjects,
                                   terrainCallback, grassCallback, treeCallback, skinnedCallback);
}

void Renderer::recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex) {
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

        vkCmdPushConstants(cmd, pipelineLayout.get(),
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(PushConstants), &push);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout.get(), 0, 1, &descSet, 0, nullptr);

        VkBuffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, obj.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd, obj.mesh->getIndexCount(), 1, 0, 0, 0);
    };

    // Render scene manager objects using MaterialRegistry for descriptor set lookup
    const auto& sceneObjects = systems_->scene().getRenderables();
    size_t playerIndex = systems_->scene().getSceneBuilder().getPlayerObjectIndex();
    bool hasCharacter = systems_->scene().getSceneBuilder().hasCharacter();

    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        // Skip player character (rendered separately with GPU skinning)
        if (hasCharacter && i == playerIndex) {
            continue;
        }

        const auto& obj = sceneObjects[i];

        // Use MaterialRegistry to get descriptor set by materialId
        // This replaces the brittle texture pointer comparison
        VkDescriptorSet descSet = materialRegistry.getDescriptorSet(obj.materialId, frameIndex);
        if (descSet == VK_NULL_HANDLE) {
            // Fallback: skip objects with invalid materialId
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Skipping object with invalid materialId %u", obj.materialId);
            continue;
        }
        renderObject(obj, descSet);
    }

    // Render procedural rocks (RockSystem uses its own descriptor sets)
    VkDescriptorSet rockDescSet = rockDescriptorSets[frameIndex];
    for (const auto& rock : systems_->rock().getSceneObjects()) {
        renderObject(rock, rockDescSet);
    }

    // Render woodland detritus (fallen branches - uses its own descriptor sets)
    if (systems_->detritus() && !detritusDescriptorSets.empty()) {
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
        const auto& lodSettings = systems_->treeLOD()->getLODSettings();
        // Use GPU-culled path only in Auto mode (SimpleLODMode not forced)
        // In forced modes (FullDetail/Impostor), use CPU path which respects SimpleLODMode
        bool useGPUCulledPath = impostorCull && impostorCull->getTreeCount() > 0 &&
                                lodSettings.simpleLODMode == SimpleLODMode::Auto;
        if (useGPUCulledPath) {
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
            // CPU-culled rendering (also handles forced SimpleLODMode)
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
    VkRenderPassBeginInfo hdrPassInfo{};
    hdrPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    hdrPassInfo.renderPass = systems_->postProcess().getHDRRenderPass();
    hdrPassInfo.framebuffer = systems_->postProcess().getHDRFramebuffer();
    hdrPassInfo.renderArea.offset = {0, 0};
    hdrPassInfo.renderArea.extent = systems_->postProcess().getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    hdrPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    hdrPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &hdrPassInfo, VK_SUBPASS_CONTENTS_INLINE);

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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline.get());
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
    if (systems_->waterTileCull().wasWaterVisibleLastFrame(frameIndex)) {
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

    // Draw physics debug lines (if enabled and lines available)
#ifdef JPH_DEBUG_RENDERER
    if (physicsDebugEnabled && systems_->debugLine().hasLines()) {
        // Set up viewport and scissor for debug rendering
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(systems_->postProcess().getExtent().width);
        viewport.height = static_cast<float>(systems_->postProcess().getExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = systems_->postProcess().getExtent();
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Need to get viewProj from the current frame data
        // For now, use the last known values (could be improved by passing as parameter)
        systems_->debugLine().recordCommands(cmd, lastViewProj);
    }
#endif

    vkCmdEndRenderPass(cmd);
}

// ===== GPU Skinning Implementation =====

bool Renderer::initSkinnedMeshRenderer() {
    SkinnedMeshRenderer::InitInfo info{};
    info.device = vulkanContext.getDevice();
    info.allocator = vulkanContext.getAllocator();
    info.descriptorPool = &*descriptorManagerPool;
    info.renderPass = systems_->postProcess().getHDRRenderPass();
    info.extent = vulkanContext.getSwapchainExtent();
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
    const auto& whiteTexture = systems_->scene().getSceneBuilder().getWhiteTexture();
    const auto& emissiveMap = systems_->scene().getSceneBuilder().getDefaultEmissiveMap();
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
    VkImageView playerDiffuseView = whiteTexture.getImageView();
    VkSampler playerDiffuseSampler = whiteTexture.getSampler();
    VkImageView playerNormalView = whiteTexture.getImageView();
    VkSampler playerNormalSampler = whiteTexture.getSampler();

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
    resources.emissiveMapView = emissiveMap.getImageView();
    resources.emissiveMapSampler = emissiveMap.getSampler();
    resources.pointShadowViews = &pointShadowViews;
    resources.pointShadowSampler = systems_->shadow().getPointShadowSampler();
    resources.spotShadowViews = &spotShadowViews;
    resources.spotShadowSampler = systems_->shadow().getSpotShadowSampler();
    resources.snowMaskView = systems_->snowMask().getSnowMaskView();
    resources.snowMaskSampler = systems_->snowMask().getSnowMaskSampler();
    resources.whiteTextureView = whiteTexture.getImageView();
    resources.whiteTextureSampler = whiteTexture.getSampler();
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
// Inline method implementations (moved from header due to forward declarations)
// ============================================================================

// Time system
void Renderer::setTimeScale(float scale) { systems_->time().setTimeScale(scale); }
float Renderer::getTimeScale() const { return systems_->time().getTimeScale(); }
void Renderer::setTimeOfDay(float time) { systems_->time().setTimeOfDay(time); }
void Renderer::resumeAutoTime() { systems_->time().resumeAutoTime(); }
float Renderer::getTimeOfDay() const { return systems_->time().getTimeOfDay(); }
TimeSystem& Renderer::getTimeSystem() { return systems_->time(); }
const TimeSystem& Renderer::getTimeSystem() const { return systems_->time(); }

// Cloud coverage and density
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

// Sky exposure control
void Renderer::setSkyExposure(float exposure) {
    skyExposure = glm::clamp(exposure, 1.0f, 20.0f);
}

float Renderer::getSkyExposure() const {
    return skyExposure;
}

// Cloud shadow control
void Renderer::setCloudShadowEnabled(bool enabled) { systems_->cloudShadow().setEnabled(enabled); }
bool Renderer::isCloudShadowEnabled() const { return systems_->cloudShadow().isEnabled(); }
void Renderer::setCloudShadowIntensity(float intensity) { systems_->cloudShadow().setShadowIntensity(intensity); }
float Renderer::getCloudShadowIntensity() const { return systems_->cloudShadow().getShadowIntensity(); }

// God ray quality control
void Renderer::setGodRaysEnabled(bool enabled) { systems_->postProcess().setGodRaysEnabled(enabled); }
bool Renderer::isGodRaysEnabled() const { return systems_->postProcess().isGodRaysEnabled(); }
void Renderer::setGodRayQuality(int quality) {
    systems_->postProcess().setGodRayQuality(static_cast<PostProcessSystem::GodRayQuality>(quality));
}
int Renderer::getGodRayQuality() const {
    return static_cast<int>(systems_->postProcess().getGodRayQuality());
}

// Froxel volumetric fog quality control
void Renderer::setFroxelFilterQuality(bool highQuality) { systems_->postProcess().setFroxelFilterQuality(highQuality); }
bool Renderer::isFroxelFilterHighQuality() const { return systems_->postProcess().isFroxelFilterHighQuality(); }

// Local tone mapping (bilateral grid)
void Renderer::setLocalToneMapEnabled(bool enabled) { systems_->postProcess().setLocalToneMapEnabled(enabled); }
bool Renderer::isLocalToneMapEnabled() const { return systems_->postProcess().isLocalToneMapEnabled(); }
void Renderer::setLocalToneMapContrast(float c) { systems_->postProcess().setLocalToneMapContrast(c); }
float Renderer::getLocalToneMapContrast() const { return systems_->postProcess().getLocalToneMapContrast(); }
void Renderer::setLocalToneMapDetail(float d) { systems_->postProcess().setLocalToneMapDetail(d); }
float Renderer::getLocalToneMapDetail() const { return systems_->postProcess().getLocalToneMapDetail(); }
void Renderer::setBilateralBlend(float b) { systems_->postProcess().setBilateralBlend(b); }
float Renderer::getBilateralBlend() const { return systems_->postProcess().getBilateralBlend(); }

// Bloom control
void Renderer::setBloomEnabled(bool enabled) { systems_->postProcess().setBloomEnabled(enabled); }
bool Renderer::isBloomEnabled() const { return systems_->postProcess().isBloomEnabled(); }

// Auto-exposure control
void Renderer::setAutoExposureEnabled(bool enabled) { systems_->postProcess().setAutoExposure(enabled); }
bool Renderer::isAutoExposureEnabled() const { return systems_->postProcess().isAutoExposureEnabled(); }
void Renderer::setManualExposure(float ev) { systems_->postProcess().setExposure(ev); }
float Renderer::getManualExposure() const { return systems_->postProcess().getExposure(); }
float Renderer::getCurrentExposure() const { return systems_->postProcess().getCurrentExposure(); }

// Terrain control
void Renderer::toggleTerrainWireframe() { systems_->terrain().setWireframeMode(!systems_->terrain().isWireframeMode()); }
bool Renderer::isTerrainWireframeMode() const { return systems_->terrain().isWireframeMode(); }
float Renderer::getTerrainHeightAt(float x, float z) const { return systems_->terrain().getHeightAt(x, z); }
uint32_t Renderer::getTerrainNodeCount() const { return systems_->terrain().getNodeCount(); }
const TerrainSystem& Renderer::getTerrainSystem() const { return systems_->terrain(); }
TerrainSystem& Renderer::getTerrainSystem() { return systems_->terrain(); }

// Catmull-Clark subdivision control
void Renderer::toggleCatmullClarkWireframe() { systems_->catmullClark().setWireframeMode(!systems_->catmullClark().isWireframeMode()); }
bool Renderer::isCatmullClarkWireframeMode() const { return systems_->catmullClark().isWireframeMode(); }
CatmullClarkSystem& Renderer::getCatmullClarkSystem() { return systems_->catmullClark(); }

// Weather control
uint32_t Renderer::getWeatherType() const { return systems_->weather().getWeatherType(); }
float Renderer::getIntensity() const { return systems_->weather().getIntensity(); }

// Fog control - Froxel volumetric fog
void Renderer::setFogDensity(float density) { systems_->froxel().setFogDensity(density); }
float Renderer::getFogDensity() const { return systems_->froxel().getFogDensity(); }
void Renderer::setFogEnabled(bool enabled) { systems_->froxel().setEnabled(enabled); systems_->postProcess().setFroxelEnabled(enabled); }
bool Renderer::isFogEnabled() const { return systems_->froxel().isEnabled(); }

// Froxel fog extended parameters
void Renderer::setFogBaseHeight(float h) { systems_->froxel().setFogBaseHeight(h); }
float Renderer::getFogBaseHeight() const { return systems_->froxel().getFogBaseHeight(); }
void Renderer::setFogScaleHeight(float h) { systems_->froxel().setFogScaleHeight(h); }
float Renderer::getFogScaleHeight() const { return systems_->froxel().getFogScaleHeight(); }
void Renderer::setFogAbsorption(float a) { systems_->froxel().setFogAbsorption(a); }
float Renderer::getFogAbsorption() const { return systems_->froxel().getFogAbsorption(); }
float Renderer::getVolumetricFarPlane() const { return systems_->froxel().getVolumetricFarPlane(); }
void Renderer::setTemporalBlend(float b) { systems_->froxel().setTemporalBlend(b); }
float Renderer::getTemporalBlend() const { return systems_->froxel().getTemporalBlend(); }

// Height fog layer parameters
void Renderer::setLayerHeight(float h) { systems_->froxel().setLayerHeight(h); }
float Renderer::getLayerHeight() const { return systems_->froxel().getLayerHeight(); }
void Renderer::setLayerThickness(float t) { systems_->froxel().setLayerThickness(t); }
float Renderer::getLayerThickness() const { return systems_->froxel().getLayerThickness(); }
void Renderer::setLayerDensity(float d) { systems_->froxel().setLayerDensity(d); }
float Renderer::getLayerDensity() const { return systems_->froxel().getLayerDensity(); }

// Atmospheric scattering parameters
AtmosphereLUTSystem& Renderer::getAtmosphereLUTSystem() { return systems_->atmosphereLUT(); }

// Leaf control
void Renderer::setLeafIntensity(float intensity) { systems_->leaf().setIntensity(intensity); }
float Renderer::getLeafIntensity() const { return systems_->leaf().getIntensity(); }
void Renderer::spawnConfetti(const glm::vec3& position, float velocity, float count, float coneAngle) {
    systems_->leaf().spawnConfetti(position, velocity, count, coneAngle);
}

// Snow control
void Renderer::setSnowAmount(float amount) { systems_->environmentSettings().snowAmount = glm::clamp(amount, 0.0f, 1.0f); }
float Renderer::getSnowAmount() const { return systems_->environmentSettings().snowAmount; }
void Renderer::setSnowColor(const glm::vec3& color) { systems_->environmentSettings().snowColor = color; }
const glm::vec3& Renderer::getSnowColor() const { return systems_->environmentSettings().snowColor; }
void Renderer::addSnowInteraction(const glm::vec3& position, float radius, float strength) {
    systems_->snowMask().addInteraction(position, radius, strength);
}
EnvironmentSettings& Renderer::getEnvironmentSettings() { return systems_->environmentSettings(); }

// Scene access
SceneManager& Renderer::getSceneManager() { return systems_->scene(); }
const SceneManager& Renderer::getSceneManager() const { return systems_->scene(); }

// Rock system access
const RockSystem& Renderer::getRockSystem() const { return systems_->rock(); }

// Detritus system access
const DetritusSystem* Renderer::getDetritusSystem() const { return systems_->detritus(); }

// Tree system access
TreeSystem* Renderer::getTreeSystem() { return systems_->tree(); }
const TreeSystem* Renderer::getTreeSystem() const { return systems_->tree(); }

// Access to systems for simulation
WindSystem& Renderer::getWindSystem() { return systems_->wind(); }
const WindSystem& Renderer::getWindSystem() const { return systems_->wind(); }
WaterSystem& Renderer::getWaterSystem() { return systems_->water(); }
const WaterSystem& Renderer::getWaterSystem() const { return systems_->water(); }
WaterTileCull& Renderer::getWaterTileCull() { return systems_->waterTileCull(); }
const WaterTileCull& Renderer::getWaterTileCull() const { return systems_->waterTileCull(); }

// Celestial/astronomical settings
void Renderer::setDate(int year, int month, int day) { systems_->time().setDate(year, month, day); }
int Renderer::getCurrentYear() const { return systems_->time().getCurrentYear(); }
int Renderer::getCurrentMonth() const { return systems_->time().getCurrentMonth(); }
int Renderer::getCurrentDay() const { return systems_->time().getCurrentDay(); }
const CelestialCalculator& Renderer::getCelestialCalculator() const { return systems_->celestial(); }

// Moon phase override controls
void Renderer::setMoonPhaseOverride(bool enabled) { systems_->time().setMoonPhaseOverride(enabled); }
bool Renderer::isMoonPhaseOverrideEnabled() const { return systems_->time().isMoonPhaseOverrideEnabled(); }
void Renderer::setMoonPhase(float phase) { systems_->time().setMoonPhase(phase); }
float Renderer::getMoonPhase() const { return systems_->time().getMoonPhase(); }
float Renderer::getCurrentMoonPhase() const { return systems_->time().getCurrentMoonPhase(); }

// Moon brightness controls
void Renderer::setMoonBrightness(float brightness) { systems_->time().setMoonBrightness(brightness); }
float Renderer::getMoonBrightness() const { return systems_->time().getMoonBrightness(); }
void Renderer::setMoonDiscIntensity(float intensity) { systems_->time().setMoonDiscIntensity(intensity); }
float Renderer::getMoonDiscIntensity() const { return systems_->time().getMoonDiscIntensity(); }
void Renderer::setMoonEarthshine(float earthshine) { systems_->time().setMoonEarthshine(earthshine); }
float Renderer::getMoonEarthshine() const { return systems_->time().getMoonEarthshine(); }

// Eclipse controls
void Renderer::setEclipseEnabled(bool enabled) { systems_->time().setEclipseEnabled(enabled); }
bool Renderer::isEclipseEnabled() const { return systems_->time().isEclipseEnabled(); }
void Renderer::setEclipseAmount(float amount) { systems_->time().setEclipseAmount(amount); }
float Renderer::getEclipseAmount() const { return systems_->time().getEclipseAmount(); }

// Hi-Z occlusion culling control
void Renderer::setHiZCullingEnabled(bool enabled) { systems_->hiZ().setHiZEnabled(enabled); }
bool Renderer::isHiZCullingEnabled() const { return systems_->hiZ().isHiZEnabled(); }
IDebugControl::CullingStats Renderer::getHiZCullingStats() const {
    auto stats = systems_->hiZ().getStats();
    return IDebugControl::CullingStats{stats.totalObjects, stats.visibleObjects, stats.frustumCulled, stats.occlusionCulled};
}
uint32_t Renderer::getVisibleObjectCount() const { return systems_->hiZ().getVisibleCount(currentFrame); }

// Profiling access
Profiler& Renderer::getProfiler() { return systems_->profiler(); }
const Profiler& Renderer::getProfiler() const { return systems_->profiler(); }
void Renderer::setProfilingEnabled(bool enabled) { systems_->profiler().setEnabled(enabled); }
bool Renderer::isProfilingEnabled() const { return systems_->profiler().isEnabled(); }

// Resource access
DescriptorManager::Pool* Renderer::getDescriptorPool() { return &*descriptorManagerPool; }

// Physics debug visualization
DebugLineSystem& Renderer::getDebugLineSystem() { return systems_->debugLine(); }
const DebugLineSystem& Renderer::getDebugLineSystem() const { return systems_->debugLine(); }

#ifdef JPH_DEBUG_RENDERER
PhysicsDebugRenderer* Renderer::getPhysicsDebugRenderer() { return systems_->physicsDebugRenderer(); }
const PhysicsDebugRenderer* Renderer::getPhysicsDebugRenderer() const { return systems_->physicsDebugRenderer(); }
#endif

// Scene builder and mesh access
SceneBuilder& Renderer::getSceneBuilder() { return systems_->scene().getSceneBuilder(); }
const SceneBuilder& Renderer::getSceneBuilder() const { return systems_->scene().getSceneBuilder(); }
Mesh& Renderer::getFlagClothMesh() { return systems_->scene().getSceneBuilder().getFlagClothMesh(); }
Mesh& Renderer::getFlagPoleMesh() { return systems_->scene().getSceneBuilder().getFlagPoleMesh(); }
void Renderer::uploadFlagClothMesh() {
    systems_->scene().getSceneBuilder().uploadFlagClothMesh(
        vulkanContext.getAllocator(), vulkanContext.getDevice(),
        commandPool.get(), vulkanContext.getGraphicsQueue());
}

// Animated character
void Renderer::updateAnimatedCharacter(float deltaTime, float movementSpeed, bool isGrounded, bool isJumping) {
    systems_->scene().getSceneBuilder().updateAnimatedCharacter(
        deltaTime, vulkanContext.getAllocator(), vulkanContext.getDevice(),
        commandPool.get(), vulkanContext.getGraphicsQueue(),
        movementSpeed, isGrounded, isJumping);
}

void Renderer::startCharacterJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics) {
    systems_->scene().getSceneBuilder().startCharacterJump(startPos, velocity, gravity, physics);
}

// Location and atmosphere
void Renderer::setLocation(const GeographicLocation& location) { systems_->celestial().setLocation(location); }
const GeographicLocation& Renderer::getLocation() const { return systems_->celestial().getLocation(); }

void Renderer::setAtmosphereParams(const AtmosphereParams& params) { systems_->atmosphereLUT().setAtmosphereParams(params); }
const AtmosphereParams& Renderer::getAtmosphereParams() const { return systems_->atmosphereLUT().getAtmosphereParams(); }

void Renderer::setVolumetricFarPlane(float f) {
    systems_->froxel().setVolumetricFarPlane(f);
    systems_->postProcess().setFroxelParams(f, FroxelSystem::DEPTH_DISTRIBUTION);
}
