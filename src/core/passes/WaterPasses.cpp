#include "WaterPasses.h"
#include "WaterPassResources.h"
#include "RenderContext.h"
#include "PerformanceToggles.h"
#include "Profiler.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "SSRSystem.h"
#include "PostProcessSystem.h"

namespace WaterPasses {

PassIds addPasses(FrameGraph& graph, const WaterPassResources& resources, const Config& config) {
    PassIds ids;
    bool* hdrPassEnabled = config.hdrPassEnabled;
    PerformanceToggles* perfToggles = config.perfToggles;

    // Capture resources by value (struct of pointers)
    auto res = resources;

    // Water G-buffer pass - renders water to mini G-buffer
    ids.waterGBuffer = graph.addPass({
        .name = "WaterGBuffer",
        .execute = [res, perfToggles](FrameGraph::RenderContext& ctx) {
            vk::CommandBuffer vkCmd(ctx.commandBuffer);

            if (perfToggles->waterGBuffer &&
                res.waterGBuffer->getPipeline() != VK_NULL_HANDLE &&
                res.hasWaterTileCull() &&
                res.waterTileCull->wasWaterVisibleLastFrame(ctx.frameIndex)) {
                res.profiler->beginGpuZone(ctx.commandBuffer, "WaterGBuffer");
                res.waterGBuffer->beginRenderPass(ctx.commandBuffer);

                vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                   res.waterGBuffer->getPipeline());
                vk::DescriptorSet gbufferDescSet =
                    res.waterGBuffer->getDescriptorSet(ctx.frameIndex);
                vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                        res.waterGBuffer->getPipelineLayout(),
                                        0, gbufferDescSet, {});

                res.water->recordMeshDraw(ctx.commandBuffer);
                res.waterGBuffer->endRenderPass(ctx.commandBuffer);
                res.profiler->endGpuZone(ctx.commandBuffer, "WaterGBuffer");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 40
    });

    // SSR pass - screen-space reflections
    ids.ssr = graph.addPass({
        .name = "SSR",
        .execute = [res, hdrPassEnabled, perfToggles](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*hdrPassEnabled && perfToggles->ssr && res.ssr->isEnabled()) {
                res.profiler->beginGpuZone(ctx.commandBuffer, "SSR");
                res.ssr->recordCompute(ctx.commandBuffer, ctx.frameIndex,
                                        res.postProcess->getHDRColorView(),
                                        res.postProcess->getHDRDepthView(),
                                        renderCtx->frame.view, renderCtx->frame.projection,
                                        renderCtx->frame.cameraPosition);
                res.profiler->endGpuZone(ctx.commandBuffer, "SSR");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 20
    });

    // Water tile culling pass
    ids.waterTileCull = graph.addPass({
        .name = "WaterTileCull",
        .execute = [res, hdrPassEnabled, perfToggles](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*hdrPassEnabled && perfToggles->waterTileCull && res.waterTileCull->isEnabled()) {
                res.profiler->beginGpuZone(ctx.commandBuffer, "WaterTileCull");
                glm::mat4 viewProj = renderCtx->frame.projection * renderCtx->frame.view;
                res.waterTileCull->recordTileCull(ctx.commandBuffer, ctx.frameIndex,
                                              viewProj, renderCtx->frame.cameraPosition,
                                              res.water->getWaterLevel(),
                                              res.postProcess->getHDRDepthView());
                res.profiler->endGpuZone(ctx.commandBuffer, "WaterTileCull");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 20
    });

    return ids;
}

} // namespace WaterPasses
