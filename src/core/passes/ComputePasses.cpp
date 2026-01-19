#include "ComputePasses.h"
#include "ComputePassResources.h"
#include "RenderContext.h"
#include "PerformanceToggles.h"
#include "Profiler.h"
#include "PostProcessSystem.h"
#include "GlobalBufferManager.h"

// Subsystem headers for compute passes
#include "TerrainSystem.h"
#include "CatmullClarkSystem.h"
#include "DisplacementSystem.h"
#include "GrassSystem.h"
#include "WeatherSystem.h"
#include "LeafSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "HiZSystem.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "CloudShadowSystem.h"
#include "WindSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "ShadowSystem.h"

namespace ComputePasses {

PassIds addPasses(FrameGraph& graph, const ComputePassResources& resources, const Config& config) {
    PassIds ids;
    PerformanceToggles* perfToggles = config.perfToggles;
    bool* terrainEnabled = config.terrainEnabled;

    // Capture resources by value (struct of pointers)
    auto res = resources;

    // Compute pass - runs all GPU compute dispatches
    ids.compute = graph.addPass({
        .name = "Compute",
        .execute = [res, perfToggles, terrainEnabled](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            VkCommandBuffer cmd = renderCtx->cmd;
            uint32_t frameIndex = renderCtx->frameIndex;

            res.profiler->beginCpuZone("ComputeDispatch");

            // Terrain compute pass (adaptive subdivision)
            if (terrainEnabled && *terrainEnabled && perfToggles->terrainCompute) {
                res.profiler->beginGpuZone(cmd, "TerrainCompute");
                res.terrain->recordCompute(cmd, frameIndex, &res.profiler->getGpuProfiler());
                res.profiler->endGpuZone(cmd, "TerrainCompute");
            }

            // Catmull-Clark subdivision compute pass
            if (perfToggles->subdivisionCompute) {
                res.profiler->beginGpuZone(cmd, "SubdivisionCompute");
                res.catmullClark->recordCompute(cmd, frameIndex);
                res.profiler->endGpuZone(cmd, "SubdivisionCompute");
            }

            // Grass compute pass (displacement + simulation)
            if (perfToggles->grassCompute) {
                res.profiler->beginGpuZone(cmd, "GrassCompute");
                res.displacement->recordUpdate(cmd, frameIndex);
                res.grass->recordResetAndCompute(cmd, frameIndex, renderCtx->frame.time);
                res.profiler->endGpuZone(cmd, "GrassCompute");
            }

            // Weather particle compute pass
            if (perfToggles->weatherCompute) {
                res.profiler->beginGpuZone(cmd, "WeatherCompute");
                res.weather->recordResetAndCompute(cmd, frameIndex, renderCtx->frame.time, renderCtx->frame.deltaTime);
                res.profiler->endGpuZone(cmd, "WeatherCompute");
            }

            // Snow compute passes (mask + volumetric)
            if (perfToggles->snowCompute) {
                res.profiler->beginGpuZone(cmd, "SnowCompute");
                res.snowMask->recordCompute(cmd, frameIndex);
                res.volumetricSnow->recordCompute(cmd, frameIndex);
                res.profiler->endGpuZone(cmd, "SnowCompute");
            }

            // Leaf particle compute pass
            if (perfToggles->leafCompute) {
                res.profiler->beginGpuZone(cmd, "LeafCompute");
                res.leaf->recordResetAndCompute(cmd, frameIndex, renderCtx->frame.time, renderCtx->frame.deltaTime);
                res.profiler->endGpuZone(cmd, "LeafCompute");
            }

            // Tree leaf culling compute pass
            if (res.hasTree() && res.hasTreeRenderer() && res.treeRenderer->isLeafCullingEnabled()) {
                res.profiler->beginGpuZone(cmd, "TreeLeafCull");
                res.treeRenderer->recordLeafCulling(
                    cmd, frameIndex, *res.tree,
                    res.treeLOD,
                    renderCtx->frame.cameraPosition, renderCtx->frame.frustumPlanes);
                res.profiler->endGpuZone(cmd, "TreeLeafCull");
            }

            // Tree impostor Hi-Z occlusion culling compute pass
            if (res.hasImpostorCull() && res.hasTree()) {
                res.profiler->beginGpuZone(cmd, "ImpostorCull");

                VkImageView hiZView = res.hiZ->getHiZPyramidView();
                VkSampler hiZSampler = res.hiZ->getHiZSampler();

                ImpostorCullSystem::LODParams lodParams;
                if (res.hasTreeLOD()) {
                    const auto& lodSettings = res.treeLOD->getLODSettings();
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
                lodParams.tanHalfFOV = 1.0f / std::abs(renderCtx->frame.projection[1][1]);

                res.impostorCull->recordCulling(
                    cmd, frameIndex,
                    renderCtx->frame.cameraPosition,
                    renderCtx->frame.frustumPlanes,
                    renderCtx->frame.viewProj,
                    hiZView, hiZSampler,
                    lodParams
                );

                res.profiler->endGpuZone(cmd, "ImpostorCull");
            }

            // Water foam persistence compute pass
            if (perfToggles->foamCompute) {
                res.profiler->beginGpuZone(cmd, "FoamCompute");
                res.foam->recordCompute(cmd, frameIndex, renderCtx->frame.deltaTime,
                                        res.flowMap->getFlowMapView(), res.flowMap->getFlowMapSampler());
                res.profiler->endGpuZone(cmd, "FoamCompute");
            }

            // Cloud shadow map compute pass
            if (perfToggles->cloudShadowCompute && res.cloudShadow->isEnabled()) {
                res.profiler->beginGpuZone(cmd, "CloudShadow");

                glm::vec2 windDir = res.wind->getWindDirection();
                float windSpeed = res.wind->getWindSpeed();
                float windTime = res.wind->getTime();
                float cloudTimeScale = 0.02f;
                glm::vec3 windOffset = glm::vec3(windDir.x * windSpeed * windTime * cloudTimeScale,
                                                  windTime * 0.002f,
                                                  windDir.y * windSpeed * windTime * cloudTimeScale);

                res.cloudShadow->recordUpdate(cmd, frameIndex, renderCtx->frame.sunDirection, renderCtx->frame.sunIntensity,
                                              windOffset, windTime * cloudTimeScale, renderCtx->frame.cameraPosition);
                res.profiler->endGpuZone(cmd, "CloudShadow");
            }

            res.profiler->endCpuZone("ComputeDispatch");
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 100  // Highest priority - runs first
    });

    // Froxel/Atmosphere pass - volumetric fog and atmosphere LUTs
    ids.froxel = graph.addPass({
        .name = "Froxel",
        .execute = [res, perfToggles](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            VkCommandBuffer cmd = renderCtx->cmd;
            uint32_t frameIndex = renderCtx->frameIndex;

            res.postProcess->setCameraPlanes(
                renderCtx->frame.nearPlane, renderCtx->frame.farPlane);

            if (!perfToggles->froxelFog && !perfToggles->atmosphereLUT) {
                return;
            }

            res.profiler->beginGpuZone(cmd, "Atmosphere");

            UniformBufferObject* ubo = static_cast<UniformBufferObject*>(
                res.globalBuffers->uniformBuffers.mappedPointers[frameIndex]);
            glm::vec3 sunColor = glm::vec3(ubo->sunColor);

            // Froxel volumetric fog update
            res.profiler->beginGpuZone(cmd, "Atmosphere:Froxel");
            res.froxel->recordFroxelUpdate(cmd, frameIndex,
                                           renderCtx->frame.view, renderCtx->frame.projection,
                                           renderCtx->frame.cameraPosition,
                                           renderCtx->frame.sunDirection, renderCtx->frame.sunIntensity, sunColor,
                                           res.shadow->getCascadeMatrices().data(),
                                           ubo->cascadeSplits);
            res.profiler->endGpuZone(cmd, "Atmosphere:Froxel");

            // Static LUT recomputation (if needed)
            if (res.atmosphereLUT->needsRecompute()) {
                res.profiler->beginGpuZone(cmd, "Atmosphere:StaticLUT");
                res.atmosphereLUT->recomputeStaticLUTs(cmd);
                res.profiler->endGpuZone(cmd, "Atmosphere:StaticLUT");
            }

            // Sky view LUT update
            res.profiler->beginGpuZone(cmd, "Atmosphere:SkyView");
            res.atmosphereLUT->updateSkyViewLUT(cmd, frameIndex, renderCtx->frame.sunDirection, renderCtx->frame.cameraPosition, 0.0f);
            res.profiler->endGpuZone(cmd, "Atmosphere:SkyView");

            // Cloud map LUT update
            res.profiler->beginGpuZone(cmd, "Atmosphere:CloudMap");
            glm::vec2 windDir = res.wind->getWindDirection();
            float windSpeed = res.wind->getWindSpeed();
            float windTime = res.wind->getTime();
            float cloudTimeScale = 0.02f;
            glm::vec3 windOffset = glm::vec3(windDir.x * windSpeed * windTime * cloudTimeScale,
                                              windTime * 0.002f,
                                              windDir.y * windSpeed * windTime * cloudTimeScale);
            res.atmosphereLUT->updateCloudMapLUT(cmd, frameIndex, windOffset, windTime * cloudTimeScale);
            res.profiler->endGpuZone(cmd, "Atmosphere:CloudMap");

            res.profiler->endGpuZone(cmd, "Atmosphere");
        },
        .canUseSecondary = false,
        .mainThreadOnly = false,  // Can run parallel with Shadow
        .priority = 50
    });

    return ids;
}

} // namespace ComputePasses
