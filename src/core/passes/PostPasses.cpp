#include "PostPasses.h"
#include "RendererSystems.h"
#include "RenderContext.h"
#include "PerformanceToggles.h"
#include "Profiler.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "HiZSystem.h"

namespace PostPasses {

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, const Config& config) {
    PassIds ids;
    auto* guiCallback = config.guiRenderCallback;
    auto* framebuffers = config.framebuffers;
    auto* perfToggles = config.perfToggles;

    // Hi-Z pass - hierarchical Z-buffer generation
    ids.hiZ = graph.addPass({
        .name = "HiZ",
        .execute = [&systems, perfToggles](FrameGraph::RenderContext& ctx) {
            if (!perfToggles->hiZPyramid) return;
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            systems.profiler().beginGpuZone(ctx.commandBuffer, "HiZPyramid");
            systems.hiZ().recordPyramidGeneration(ctx.commandBuffer, ctx.frameIndex);
            systems.profiler().endGpuZone(ctx.commandBuffer, "HiZPyramid");
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 15
    });

    // Bloom pass - multi-pass bloom effect
    ids.bloom = graph.addPass({
        .name = "Bloom",
        .execute = [&systems, perfToggles](FrameGraph::RenderContext& ctx) {
            if (!perfToggles->bloom || !systems.postProcess().isBloomEnabled()) return;
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            systems.profiler().beginGpuZone(ctx.commandBuffer, "Bloom");
            systems.bloom().setThreshold(systems.postProcess().getBloomThreshold());
            systems.bloom().recordBloomPass(ctx.commandBuffer, systems.postProcess().getHDRColorView());
            systems.profiler().endGpuZone(ctx.commandBuffer, "Bloom");
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
