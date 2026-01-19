#include "PostPasses.h"
#include "PostPassResources.h"
#include "RenderContext.h"
#include "PerformanceToggles.h"
#include "Profiler.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "HiZSystem.h"

namespace PostPasses {

PassIds addPasses(FrameGraph& graph, const PostPassResources& resources, const Config& config) {
    PassIds ids;
    auto* guiCallback = config.guiRenderCallback;
    auto* framebuffers = config.framebuffers;
    auto* perfToggles = config.perfToggles;

    // Capture resources by value (struct of pointers)
    auto res = resources;

    // Hi-Z pass - hierarchical Z-buffer generation
    ids.hiZ = graph.addPass({
        .name = "HiZ",
        .execute = [res, perfToggles](FrameGraph::RenderContext& ctx) {
            if (!perfToggles->hiZPyramid) return;
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            res.profiler->beginGpuZone(ctx.commandBuffer, "HiZPyramid");
            res.hiZ->recordPyramidGeneration(ctx.commandBuffer, ctx.frameIndex);
            res.profiler->endGpuZone(ctx.commandBuffer, "HiZPyramid");
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 15
    });

    // Bloom pass - multi-pass bloom effect
    ids.bloom = graph.addPass({
        .name = "Bloom",
        .execute = [res, perfToggles](FrameGraph::RenderContext& ctx) {
            if (!perfToggles->bloom || !res.postProcess->isBloomEnabled()) return;
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            res.profiler->beginGpuZone(ctx.commandBuffer, "Bloom");
            res.bloom->setThreshold(res.postProcess->getBloomThreshold());
            res.bloom->recordBloomPass(ctx.commandBuffer, res.postProcess->getHDRColorView());
            res.profiler->endGpuZone(ctx.commandBuffer, "Bloom");
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 10
    });

    // Bilateral grid pass - local tone mapping
    ids.bilateralGrid = graph.addPass({
        .name = "BilateralGrid",
        .execute = [res](FrameGraph::RenderContext& ctx) {
            if (res.postProcess->isLocalToneMapEnabled()) {
                res.profiler->beginGpuZone(ctx.commandBuffer, "BilateralGrid");
                res.bilateralGrid->recordBilateralGrid(ctx.commandBuffer, ctx.frameIndex,
                                                       res.postProcess->getHDRColorView());
                res.profiler->endGpuZone(ctx.commandBuffer, "BilateralGrid");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 10
    });

    // Post-process pass - final composite with tone mapping and GUI
    ids.postProcess = graph.addPass({
        .name = "PostProcess",
        .execute = [res, framebuffers, guiCallback](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            res.profiler->beginGpuZone(ctx.commandBuffer, "PostProcess");
            res.postProcess->recordPostProcess(ctx.commandBuffer, ctx.frameIndex,
                *(*framebuffers)[ctx.imageIndex], renderCtx->frame.deltaTime,
                guiCallback ? *guiCallback : std::function<void(VkCommandBuffer)>{});
            res.profiler->endGpuZone(ctx.commandBuffer, "PostProcess");
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 0  // Lowest priority - runs last
    });

    return ids;
}

} // namespace PostPasses
