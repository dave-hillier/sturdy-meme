#include "WaterPasses.h"
#include "RendererSystems.h"
#include "RenderContext.h"
#include "PerformanceToggles.h"
#include "Profiler.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "SSRSystem.h"
#include "PostProcessSystem.h"

namespace WaterPasses {

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, const Config& config) {
    PassIds ids;
    bool* hdrPassEnabled = config.hdrPassEnabled;
    PerformanceToggles* perfToggles = config.perfToggles;

    // Water G-buffer pass - renders water to mini G-buffer
    ids.waterGBuffer = graph.addPass({
        .name = "WaterGBuffer",
        .execute = [&systems, perfToggles](FrameGraph::RenderContext& ctx) {
            vk::CommandBuffer vkCmd(ctx.commandBuffer);

            if (perfToggles->waterGBuffer &&
                systems.waterGBuffer().getPipeline() != VK_NULL_HANDLE &&
                systems.hasWaterTileCull() &&
                systems.waterTileCull().wasWaterVisibleLastFrame(ctx.frameIndex)) {
                systems.profiler().beginGpuZone(ctx.commandBuffer, "WaterGBuffer");
                systems.waterGBuffer().beginRenderPass(ctx.commandBuffer);

                vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   systems.waterGBuffer().getPipeline());
                vk::DescriptorSet gbufferDescSet =
                    systems.waterGBuffer().getDescriptorSet(ctx.frameIndex);
                vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                        systems.waterGBuffer().getPipelineLayout(),
                                        0, gbufferDescSet, {});

                systems.water().recordMeshDraw(ctx.commandBuffer);
                systems.waterGBuffer().endRenderPass(ctx.commandBuffer);
                systems.profiler().endGpuZone(ctx.commandBuffer, "WaterGBuffer");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 40
    });

    // SSR pass - screen-space reflections
    ids.ssr = graph.addPass({
        .name = "SSR",
        .execute = [&systems, hdrPassEnabled, perfToggles](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*hdrPassEnabled && perfToggles->ssr && systems.ssr().isEnabled()) {
                systems.profiler().beginGpuZone(ctx.commandBuffer, "SSR");
                systems.ssr().recordCompute(ctx.commandBuffer, ctx.frameIndex,
                                        systems.postProcess().getHDRColorView(),
                                        systems.postProcess().getHDRDepthView(),
                                        renderCtx->frame.view, renderCtx->frame.projection,
                                        renderCtx->frame.cameraPosition);
                systems.profiler().endGpuZone(ctx.commandBuffer, "SSR");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 20
    });

    // Water tile culling pass
    ids.waterTileCull = graph.addPass({
        .name = "WaterTileCull",
        .execute = [&systems, hdrPassEnabled, perfToggles](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*hdrPassEnabled && perfToggles->waterTileCull && systems.waterTileCull().isEnabled()) {
                systems.profiler().beginGpuZone(ctx.commandBuffer, "WaterTileCull");
                glm::mat4 viewProj = renderCtx->frame.projection * renderCtx->frame.view;
                systems.waterTileCull().recordTileCull(ctx.commandBuffer, ctx.frameIndex,
                                              viewProj, renderCtx->frame.cameraPosition,
                                              systems.water().getWaterLevel(),
                                              systems.postProcess().getHDRDepthView());
                systems.profiler().endGpuZone(ctx.commandBuffer, "WaterTileCull");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 20
    });

    return ids;
}

} // namespace WaterPasses
