// RenderPipelineFactory.cpp - Decouples render pipeline setup from Renderer
//
// This file contains all the subsystem includes needed for pipeline lambda
// captures. By centralizing them here, Renderer.cpp needs fewer includes.

#include "RenderPipelineFactory.h"
#include "RendererSystems.h"

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
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "SceneBuilder.h"

void RenderPipelineFactory::setupPipeline(
    RenderPipeline& pipeline,
    RendererSystems& systems,
    const PipelineState& state,
    std::function<void(VkCommandBuffer, uint32_t)> recordSceneObjectsFn
) {
    // Clear any existing passes
    pipeline.clear();

    // Capture state pointers for use in lambdas
    bool* terrainEnabled = state.terrainEnabled;
    bool* physicsDebugEnabled = state.physicsDebugEnabled;
    const uint32_t* currentFrame = state.currentFrame;
    glm::mat4* lastViewProj = state.lastViewProj;
    VkPipeline graphicsPipeline = state.graphicsPipeline;

    // ===== COMPUTE STAGE =====

    // Terrain compute pass (adaptive subdivision)
    pipeline.computeStage.addPass("terrain", [&systems, terrainEnabled](RenderContext& ctx) {
        if (!*terrainEnabled) return;
        systems.profiler().beginGpuZone(ctx.cmd, "TerrainCompute");
        systems.terrain().recordCompute(ctx.cmd, ctx.frameIndex, &systems.profiler().getGpuProfiler());
        systems.profiler().endGpuZone(ctx.cmd, "TerrainCompute");
    });

    // Catmull-Clark subdivision compute pass
    pipeline.computeStage.addPass("subdivision", [&systems](RenderContext& ctx) {
        systems.profiler().beginGpuZone(ctx.cmd, "SubdivisionCompute");
        systems.catmullClark().recordCompute(ctx.cmd, ctx.frameIndex);
        systems.profiler().endGpuZone(ctx.cmd, "SubdivisionCompute");
    });

    // Grass compute pass (displacement + simulation)
    pipeline.computeStage.addPass("grass", [&systems](RenderContext& ctx) {
        systems.profiler().beginGpuZone(ctx.cmd, "GrassCompute");
        systems.grass().recordDisplacementUpdate(ctx.cmd, ctx.frameIndex);
        systems.grass().recordResetAndCompute(ctx.cmd, ctx.frameIndex, ctx.frame.time);
        systems.profiler().endGpuZone(ctx.cmd, "GrassCompute");
    });

    // Weather particle compute pass
    pipeline.computeStage.addPass("weather", [&systems](RenderContext& ctx) {
        systems.profiler().beginGpuZone(ctx.cmd, "WeatherCompute");
        systems.weather().recordResetAndCompute(ctx.cmd, ctx.frameIndex, ctx.frame.time, ctx.frame.deltaTime);
        systems.profiler().endGpuZone(ctx.cmd, "WeatherCompute");
    });

    // Snow compute passes (mask + volumetric)
    pipeline.computeStage.addPass("snow", [&systems](RenderContext& ctx) {
        systems.profiler().beginGpuZone(ctx.cmd, "SnowCompute");
        systems.snowMask().recordCompute(ctx.cmd, ctx.frameIndex);
        systems.volumetricSnow().recordCompute(ctx.cmd, ctx.frameIndex);
        systems.profiler().endGpuZone(ctx.cmd, "SnowCompute");
    });

    // Leaf particle compute pass
    pipeline.computeStage.addPass("leaf", [&systems](RenderContext& ctx) {
        systems.profiler().beginGpuZone(ctx.cmd, "LeafCompute");
        systems.leaf().recordResetAndCompute(ctx.cmd, ctx.frameIndex, ctx.frame.time, ctx.frame.deltaTime);
        systems.profiler().endGpuZone(ctx.cmd, "LeafCompute");
    });

    // Tree leaf culling compute pass
    pipeline.computeStage.addPass("treeLeafCull", [&systems](RenderContext& ctx) {
        if (!systems.tree() || !systems.treeRenderer()) return;
        if (!systems.treeRenderer()->isLeafCullingEnabled()) return;

        systems.profiler().beginGpuZone(ctx.cmd, "TreeLeafCull");
        systems.treeRenderer()->recordLeafCulling(
            ctx.cmd, ctx.frameIndex, *systems.tree(),
            systems.treeLOD(),
            ctx.frame.cameraPosition, ctx.frame.frustumPlanes);
        systems.profiler().endGpuZone(ctx.cmd, "TreeLeafCull");
    });

    // Tree impostor Hi-Z occlusion culling compute pass
    pipeline.computeStage.addPass("impostorCull", [&systems](RenderContext& ctx) {
        auto* impostorCull = systems.impostorCull();
        if (!impostorCull || !systems.tree()) return;

        systems.profiler().beginGpuZone(ctx.cmd, "ImpostorCull");

        VkImageView hiZView = systems.hiZ().getHiZPyramidView();
        VkSampler hiZSampler = systems.hiZ().getHiZSampler();

        ImpostorCullSystem::LODParams lodParams;
        if (systems.treeLOD()) {
            const auto& lodSettings = systems.treeLOD()->getLODSettings();
            lodParams.fullDetailDistance = lodSettings.fullDetailDistance;
            lodParams.impostorDistance = lodSettings.impostorDistance;
            lodParams.hysteresis = lodSettings.hysteresis;
            lodParams.blendRange = lodSettings.blendRange;
            lodParams.useScreenSpaceError = lodSettings.useScreenSpaceError;
            lodParams.errorThresholdFull = lodSettings.errorThresholdFull;
            lodParams.errorThresholdImpostor = lodSettings.errorThresholdImpostor;
            lodParams.errorThresholdCull = lodSettings.errorThresholdCull;
        }
        // Note: Vulkan Y-flip makes projection[1][1] negative, so use abs()
        lodParams.tanHalfFOV = 1.0f / std::abs(ctx.frame.projection[1][1]);

        impostorCull->recordCulling(
            ctx.cmd, ctx.frameIndex,
            ctx.frame.cameraPosition,
            ctx.frame.frustumPlanes,
            ctx.frame.viewProj,
            hiZView, hiZSampler,
            lodParams
        );

        systems.profiler().endGpuZone(ctx.cmd, "ImpostorCull");
    });

    // Water foam persistence compute pass
    pipeline.computeStage.addPass("foam", [&systems](RenderContext& ctx) {
        systems.profiler().beginGpuZone(ctx.cmd, "FoamCompute");
        systems.foam().recordCompute(ctx.cmd, ctx.frameIndex, ctx.frame.deltaTime,
                                     systems.flowMap().getFlowMapView(), systems.flowMap().getFlowMapSampler());
        systems.profiler().endGpuZone(ctx.cmd, "FoamCompute");
    });

    // Cloud shadow map compute pass
    pipeline.computeStage.addPass("cloudShadow", [&systems](RenderContext& ctx) {
        if (!systems.cloudShadow().isEnabled()) return;
        systems.profiler().beginGpuZone(ctx.cmd, "CloudShadow");

        glm::vec2 windDir = systems.wind().getWindDirection();
        float windSpeed = systems.wind().getWindSpeed();
        float windTime = systems.wind().getTime();
        float cloudTimeScale = 0.02f;
        glm::vec3 windOffset = glm::vec3(windDir.x * windSpeed * windTime * cloudTimeScale,
                                          windTime * 0.002f,
                                          windDir.y * windSpeed * windTime * cloudTimeScale);

        systems.cloudShadow().recordUpdate(ctx.cmd, ctx.frameIndex, ctx.frame.sunDirection, ctx.frame.sunIntensity,
                                           windOffset, windTime * cloudTimeScale, ctx.frame.cameraPosition);
        systems.profiler().endGpuZone(ctx.cmd, "CloudShadow");
    });

    // ===== SHADOW STAGE =====
    pipeline.shadowStage.setTerrainCallback([&systems, terrainEnabled, currentFrame](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (*terrainEnabled) {
            systems.terrain().recordShadowDraw(cb, *currentFrame, lightMatrix, static_cast<int>(cascade));
        }
    });

    pipeline.shadowStage.setGrassCallback([&systems, currentFrame](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& /*lightMatrix*/) {
        systems.grass().recordShadowDraw(cb, *currentFrame, systems.wind().getTime(), cascade);
    });

    pipeline.shadowStage.setTreeCallback([&systems, currentFrame](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& /*lightMatrix*/) {
        if (systems.tree() && systems.treeRenderer()) {
            systems.treeRenderer()->renderShadows(cb, *currentFrame, *systems.tree(), static_cast<int>(cascade), systems.treeLOD());
        }
    });

    pipeline.shadowStage.getDescriptorSet = [&systems](uint32_t frameIndex) -> VkDescriptorSet {
        const auto& materialRegistry = systems.scene().getSceneBuilder().getMaterialRegistry();
        return materialRegistry.getDescriptorSet(0, frameIndex);
    };

    pipeline.shadowStage.getSceneObjects = [&systems]() -> const std::vector<Renderable>& {
        return systems.scene().getRenderables();
    };

    // ===== ATMOSPHERE/FROXEL STAGES =====
    pipeline.setFroxelStageFn([&systems](RenderContext& ctx) {
        systems.profiler().beginGpuZone(ctx.cmd, "Atmosphere");

        UniformBufferObject* ubo = static_cast<UniformBufferObject*>(systems.globalBuffers().uniformBuffers.mappedPointers[ctx.frameIndex]);
        glm::vec3 sunColor = glm::vec3(ubo->sunColor);

        systems.froxel().recordFroxelUpdate(ctx.cmd, ctx.frameIndex,
                                            ctx.frame.view, ctx.frame.projection,
                                            ctx.frame.cameraPosition,
                                            ctx.frame.sunDirection, ctx.frame.sunIntensity, sunColor,
                                            systems.shadow().getCascadeMatrices().data(),
                                            ubo->cascadeSplits);

        if (systems.atmosphereLUT().needsRecompute()) {
            systems.atmosphereLUT().recomputeStaticLUTs(ctx.cmd);
        }

        systems.atmosphereLUT().updateSkyViewLUT(ctx.cmd, ctx.frameIndex, ctx.frame.sunDirection, ctx.frame.cameraPosition, 0.0f);

        glm::vec2 windDir = systems.wind().getWindDirection();
        float windSpeed = systems.wind().getWindSpeed();
        float windTime = systems.wind().getTime();
        float cloudTimeScale = 0.02f;
        glm::vec3 windOffset = glm::vec3(windDir.x * windSpeed * windTime * cloudTimeScale,
                                          windTime * 0.002f,
                                          windDir.y * windSpeed * windTime * cloudTimeScale);
        systems.atmosphereLUT().updateCloudMapLUT(ctx.cmd, ctx.frameIndex, windOffset, windTime * cloudTimeScale);

        systems.profiler().endGpuZone(ctx.cmd, "Atmosphere");
    });

    // ===== HDR STAGE =====

    // Sky rendering
    pipeline.hdrStage.addDrawCall("sky", [&systems](RenderContext& ctx) {
        systems.sky().recordDraw(ctx.cmd, ctx.frameIndex);
    });

    // Terrain rendering
    pipeline.hdrStage.addDrawCall("terrain", [&systems, terrainEnabled](RenderContext& ctx) {
        if (*terrainEnabled) {
            systems.terrain().recordDraw(ctx.cmd, ctx.frameIndex);
        }
    });

    // Catmull-Clark subdivision surfaces
    pipeline.hdrStage.addDrawCall("catmullClark", [&systems](RenderContext& ctx) {
        systems.catmullClark().recordDraw(ctx.cmd, ctx.frameIndex);
    });

    // Scene objects (static meshes)
    pipeline.hdrStage.addDrawCall("sceneObjects", [graphicsPipeline, recordSceneObjectsFn](RenderContext& ctx) {
        vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
        recordSceneObjectsFn(ctx.cmd, ctx.frameIndex);
    });

    // Skinned character (GPU skinning)
    pipeline.hdrStage.addDrawCall("skinnedCharacter", [&systems](RenderContext& ctx) {
        SceneBuilder& sceneBuilder = systems.scene().getSceneBuilder();
        if (sceneBuilder.hasCharacter()) {
            const auto& sceneObjects = sceneBuilder.getRenderables();
            size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
            if (playerIndex < sceneObjects.size()) {
                const Renderable& playerObj = sceneObjects[playerIndex];
                systems.skinnedMesh().record(ctx.cmd, ctx.frameIndex, playerObj, sceneBuilder.getAnimatedCharacter());
            }
        }
    });

    // Grass
    pipeline.hdrStage.addDrawCall("grass", [&systems](RenderContext& ctx) {
        systems.grass().recordDraw(ctx.cmd, ctx.frameIndex, ctx.frame.time);
    });

    // Water surface
    pipeline.hdrStage.addDrawCall("water", [&systems](RenderContext& ctx) {
        if (systems.waterTileCull().wasWaterVisibleLastFrame(ctx.frameIndex)) {
            systems.water().recordDraw(ctx.cmd, ctx.frameIndex);
        }
    });

    // Leaves
    pipeline.hdrStage.addDrawCall("leaves", [&systems](RenderContext& ctx) {
        systems.leaf().recordDraw(ctx.cmd, ctx.frameIndex, ctx.frame.time);
    });

    // Weather particles
    pipeline.hdrStage.addDrawCall("weather", [&systems](RenderContext& ctx) {
        systems.weather().recordDraw(ctx.cmd, ctx.frameIndex, ctx.frame.time);
    });

    // Physics debug lines
    pipeline.hdrStage.addDrawCall("debugLines", [&systems, physicsDebugEnabled, lastViewProj](RenderContext& ctx) {
#ifdef JPH_DEBUG_RENDERER
        if (*physicsDebugEnabled && systems.debugLine().hasLines()) {
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(systems.postProcess().getExtent().width);
            viewport.height = static_cast<float>(systems.postProcess().getExtent().height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(ctx.cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = systems.postProcess().getExtent();
            vkCmdSetScissor(ctx.cmd, 0, 1, &scissor);

            systems.debugLine().recordCommands(ctx.cmd, *lastViewProj);
        }
#else
        (void)ctx;
        (void)physicsDebugEnabled;
        (void)lastViewProj;
#endif
    });

    // ===== POST STAGE =====
    pipeline.postStage.setHiZRecordFn([&systems](RenderContext& ctx) {
        systems.profiler().beginGpuZone(ctx.cmd, "HiZPyramid");
        systems.hiZ().recordPyramidGeneration(ctx.cmd, ctx.frameIndex);
        systems.profiler().endGpuZone(ctx.cmd, "HiZPyramid");
    });

    pipeline.postStage.setBloomRecordFn([&systems](RenderContext& ctx) {
        systems.profiler().beginGpuZone(ctx.cmd, "Bloom");
        systems.bloom().setThreshold(systems.postProcess().getBloomThreshold());
        systems.bloom().recordBloomPass(ctx.cmd, systems.postProcess().getHDRColorView());
        systems.profiler().endGpuZone(ctx.cmd, "Bloom");
    });
}

void RenderPipelineFactory::syncToggles(
    RenderPipeline& pipeline,
    const PerformanceToggles& toggles
) {
    // Sync compute stage passes
    pipeline.computeStage.setPassEnabled("terrain", toggles.terrainCompute);
    pipeline.computeStage.setPassEnabled("subdivision", toggles.subdivisionCompute);
    pipeline.computeStage.setPassEnabled("grass", toggles.grassCompute);
    pipeline.computeStage.setPassEnabled("weather", toggles.weatherCompute);
    pipeline.computeStage.setPassEnabled("snow", toggles.snowCompute);
    pipeline.computeStage.setPassEnabled("leaf", toggles.leafCompute);
    pipeline.computeStage.setPassEnabled("foam", toggles.foamCompute);
    pipeline.computeStage.setPassEnabled("cloudShadow", toggles.cloudShadowCompute);

    // Sync HDR stage draw calls
    pipeline.hdrStage.setDrawCallEnabled("sky", toggles.skyDraw);
    pipeline.hdrStage.setDrawCallEnabled("terrain", toggles.terrainDraw);
    pipeline.hdrStage.setDrawCallEnabled("catmullClark", toggles.catmullClarkDraw);
    pipeline.hdrStage.setDrawCallEnabled("sceneObjects", toggles.sceneObjectsDraw);
    pipeline.hdrStage.setDrawCallEnabled("skinnedCharacter", toggles.skinnedCharacterDraw);
    pipeline.hdrStage.setDrawCallEnabled("grass", toggles.grassDraw);
    pipeline.hdrStage.setDrawCallEnabled("water", toggles.waterDraw);
    pipeline.hdrStage.setDrawCallEnabled("leaves", toggles.leavesDraw);
    pipeline.hdrStage.setDrawCallEnabled("weather", toggles.weatherDraw);
    pipeline.hdrStage.setDrawCallEnabled("debugLines", toggles.debugLinesDraw);

    // Sync post stage
    pipeline.postStage.setHiZEnabled(toggles.hiZPyramid);
    pipeline.postStage.setBloomEnabled(toggles.bloom);
}
