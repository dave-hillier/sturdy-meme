#include "ComputePasses.h"
#include "RendererSystems.h"
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

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, const Config& config) {
    PassIds ids;
    PerformanceToggles* perfToggles = config.perfToggles;
    bool* terrainEnabled = config.terrainEnabled;

    // Compute pass - runs all GPU compute dispatches
    ids.compute = graph.addPass({
        .name = "Compute",
        .execute = [&systems, perfToggles, terrainEnabled](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            VkCommandBuffer cmd = renderCtx->cmd;
            uint32_t frameIndex = renderCtx->frameIndex;

            systems.profiler().beginCpuZone("ComputeDispatch");

            // Terrain compute pass (adaptive subdivision)
            if (terrainEnabled && *terrainEnabled && perfToggles->terrainCompute) {
                systems.profiler().beginGpuZone(cmd, "TerrainCompute");
                systems.terrain().recordCompute(cmd, frameIndex, &systems.profiler().getGpuProfiler());
                systems.profiler().endGpuZone(cmd, "TerrainCompute");
            }

            // Catmull-Clark subdivision compute pass
            if (perfToggles->subdivisionCompute) {
                systems.profiler().beginGpuZone(cmd, "SubdivisionCompute");
                systems.catmullClark().recordCompute(cmd, frameIndex);
                systems.profiler().endGpuZone(cmd, "SubdivisionCompute");
            }

            // Grass compute pass (displacement + simulation)
            if (perfToggles->grassCompute) {
                systems.profiler().beginGpuZone(cmd, "GrassCompute");
                systems.displacement().recordUpdate(cmd, frameIndex);
                systems.grass().recordResetAndCompute(cmd, frameIndex, renderCtx->frame.time);
                systems.profiler().endGpuZone(cmd, "GrassCompute");
            }

            // Weather particle compute pass
            if (perfToggles->weatherCompute) {
                systems.profiler().beginGpuZone(cmd, "WeatherCompute");
                systems.weather().recordResetAndCompute(cmd, frameIndex, renderCtx->frame.time, renderCtx->frame.deltaTime);
                systems.profiler().endGpuZone(cmd, "WeatherCompute");
            }

            // Snow compute passes (mask + volumetric)
            if (perfToggles->snowCompute) {
                systems.profiler().beginGpuZone(cmd, "SnowCompute");
                systems.snowMask().recordCompute(cmd, frameIndex);
                systems.volumetricSnow().recordCompute(cmd, frameIndex);
                systems.profiler().endGpuZone(cmd, "SnowCompute");
            }

            // Leaf particle compute pass
            if (perfToggles->leafCompute) {
                systems.profiler().beginGpuZone(cmd, "LeafCompute");
                systems.leaf().recordResetAndCompute(cmd, frameIndex, renderCtx->frame.time, renderCtx->frame.deltaTime);
                systems.profiler().endGpuZone(cmd, "LeafCompute");
            }

            // Tree leaf culling compute pass
            if (systems.tree() && systems.treeRenderer() && systems.treeRenderer()->isLeafCullingEnabled()) {
                systems.profiler().beginGpuZone(cmd, "TreeLeafCull");
                systems.treeRenderer()->recordLeafCulling(
                    cmd, frameIndex, *systems.tree(),
                    systems.treeLOD(),
                    renderCtx->frame.cameraPosition, renderCtx->frame.frustumPlanes);
                systems.profiler().endGpuZone(cmd, "TreeLeafCull");
            }

            // Tree impostor Hi-Z occlusion culling compute pass
            auto* impostorCull = systems.impostorCull();
            if (impostorCull && systems.tree()) {
                systems.profiler().beginGpuZone(cmd, "ImpostorCull");

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
                lodParams.tanHalfFOV = 1.0f / std::abs(renderCtx->frame.projection[1][1]);

                impostorCull->recordCulling(
                    cmd, frameIndex,
                    renderCtx->frame.cameraPosition,
                    renderCtx->frame.frustumPlanes,
                    renderCtx->frame.viewProj,
                    hiZView, hiZSampler,
                    lodParams
                );

                systems.profiler().endGpuZone(cmd, "ImpostorCull");
            }

            // Water foam persistence compute pass
            if (perfToggles->foamCompute) {
                systems.profiler().beginGpuZone(cmd, "FoamCompute");
                systems.foam().recordCompute(cmd, frameIndex, renderCtx->frame.deltaTime,
                                             systems.flowMap().getFlowMapView(), systems.flowMap().getFlowMapSampler());
                systems.profiler().endGpuZone(cmd, "FoamCompute");
            }

            // Cloud shadow map compute pass
            if (perfToggles->cloudShadowCompute && systems.cloudShadow().isEnabled()) {
                systems.profiler().beginGpuZone(cmd, "CloudShadow");

                glm::vec2 windDir = systems.wind().getWindDirection();
                float windSpeed = systems.wind().getWindSpeed();
                float windTime = systems.wind().getTime();
                float cloudTimeScale = 0.02f;
                glm::vec3 windOffset = glm::vec3(windDir.x * windSpeed * windTime * cloudTimeScale,
                                                  windTime * 0.002f,
                                                  windDir.y * windSpeed * windTime * cloudTimeScale);

                systems.cloudShadow().recordUpdate(cmd, frameIndex, renderCtx->frame.sunDirection, renderCtx->frame.sunIntensity,
                                                   windOffset, windTime * cloudTimeScale, renderCtx->frame.cameraPosition);
                systems.profiler().endGpuZone(cmd, "CloudShadow");
            }

            systems.profiler().endCpuZone("ComputeDispatch");
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 100  // Highest priority - runs first
    });

    // Froxel/Atmosphere pass - volumetric fog and atmosphere LUTs
    ids.froxel = graph.addPass({
        .name = "Froxel",
        .execute = [&systems, perfToggles](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            VkCommandBuffer cmd = renderCtx->cmd;
            uint32_t frameIndex = renderCtx->frameIndex;

            systems.postProcess().setCameraPlanes(
                renderCtx->frame.nearPlane, renderCtx->frame.farPlane);

            if (!perfToggles->froxelFog && !perfToggles->atmosphereLUT) {
                return;
            }

            systems.profiler().beginGpuZone(cmd, "Atmosphere");

            UniformBufferObject* ubo = static_cast<UniformBufferObject*>(
                systems.globalBuffers().uniformBuffers.mappedPointers[frameIndex]);
            glm::vec3 sunColor = glm::vec3(ubo->sunColor);

            // Froxel volumetric fog update
            systems.profiler().beginGpuZone(cmd, "Atmosphere:Froxel");
            systems.froxel().recordFroxelUpdate(cmd, frameIndex,
                                                renderCtx->frame.view, renderCtx->frame.projection,
                                                renderCtx->frame.cameraPosition,
                                                renderCtx->frame.sunDirection, renderCtx->frame.sunIntensity, sunColor,
                                                systems.shadow().getCascadeMatrices().data(),
                                                ubo->cascadeSplits);
            systems.profiler().endGpuZone(cmd, "Atmosphere:Froxel");

            // Static LUT recomputation (if needed)
            if (systems.atmosphereLUT().needsRecompute()) {
                systems.profiler().beginGpuZone(cmd, "Atmosphere:StaticLUT");
                systems.atmosphereLUT().recomputeStaticLUTs(cmd);
                systems.profiler().endGpuZone(cmd, "Atmosphere:StaticLUT");
            }

            // Sky view LUT update
            systems.profiler().beginGpuZone(cmd, "Atmosphere:SkyView");
            systems.atmosphereLUT().updateSkyViewLUT(cmd, frameIndex, renderCtx->frame.sunDirection, renderCtx->frame.cameraPosition, 0.0f);
            systems.profiler().endGpuZone(cmd, "Atmosphere:SkyView");

            // Cloud map LUT update
            systems.profiler().beginGpuZone(cmd, "Atmosphere:CloudMap");
            glm::vec2 windDir = systems.wind().getWindDirection();
            float windSpeed = systems.wind().getWindSpeed();
            float windTime = systems.wind().getTime();
            float cloudTimeScale = 0.02f;
            glm::vec3 windOffset = glm::vec3(windDir.x * windSpeed * windTime * cloudTimeScale,
                                              windTime * 0.002f,
                                              windDir.y * windSpeed * windTime * cloudTimeScale);
            systems.atmosphereLUT().updateCloudMapLUT(cmd, frameIndex, windOffset, windTime * cloudTimeScale);
            systems.profiler().endGpuZone(cmd, "Atmosphere:CloudMap");

            systems.profiler().endGpuZone(cmd, "Atmosphere");
        },
        .canUseSecondary = false,
        .mainThreadOnly = false,  // Can run parallel with Shadow
        .priority = 50
    });

    return ids;
}

} // namespace ComputePasses
