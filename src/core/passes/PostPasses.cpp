#include "PostPasses.h"
#include "RendererSystems.h"
#include "RenderContext.h"
#include "RenderPipeline.h"
#include "Profiler.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"

namespace PostPasses {

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, RenderPipeline& pipeline, const Config& config) {
    PassIds ids;
    auto* guiCallback = config.guiRenderCallback;
    auto* framebuffers = config.framebuffers;

    // Hi-Z pass - hierarchical Z-buffer generation
    ids.hiZ = graph.addPass({
        .name = "HiZ",
        .execute = [&pipeline](FrameGraph::RenderContext& ctx) {
            if (pipeline.postStage.hiZRecordFn) {
                RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
                if (!renderCtx) return;
                pipeline.postStage.hiZRecordFn(*renderCtx);
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 15
    });

    // Bloom pass - multi-pass bloom effect
    ids.bloom = graph.addPass({
        .name = "Bloom",
        .execute = [&systems, &pipeline](FrameGraph::RenderContext& ctx) {
            if (systems.postProcess().isBloomEnabled() && pipeline.postStage.bloomRecordFn) {
                RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
                if (!renderCtx) return;
                pipeline.postStage.bloomRecordFn(*renderCtx);
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 10
    });

    // Bilateral grid pass - local tone mapping
    ids.bilateralGrid = graph.addPass({
        .name = "BilateralGrid",
        .execute = [&systems](FrameGraph::RenderContext& ctx) {
            if (systems.postProcess().isLocalToneMapEnabled()) {
                systems.profiler().beginGpuZone(ctx.commandBuffer, "BilateralGrid");
                systems.bilateralGrid().recordBilateralGrid(ctx.commandBuffer, ctx.frameIndex,
                                                           systems.postProcess().getHDRColorView());
                systems.profiler().endGpuZone(ctx.commandBuffer, "BilateralGrid");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 10
    });

    // Post-process pass - final composite with tone mapping and GUI
    ids.postProcess = graph.addPass({
        .name = "PostProcess",
        .execute = [&systems, framebuffers, guiCallback](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            systems.profiler().beginGpuZone(ctx.commandBuffer, "PostProcess");
            systems.postProcess().recordPostProcess(ctx.commandBuffer, ctx.frameIndex,
                *(*framebuffers)[ctx.imageIndex], renderCtx->frame.deltaTime,
                guiCallback ? *guiCallback : std::function<void(VkCommandBuffer)>{});
            systems.profiler().endGpuZone(ctx.commandBuffer, "PostProcess");
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 0  // Lowest priority - runs last
    });

    return ids;
}

} // namespace PostPasses
