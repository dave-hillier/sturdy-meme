#include "ShadowPasses.h"
#include "RendererSystems.h"
#include "RenderContext.h"
#include "Profiler.h"
#include "PerformanceToggles.h"

namespace ShadowPasses {

FrameGraph::PassId addShadowPass(FrameGraph& graph, RendererSystems& systems, const Config& config) {
    float* lastSunIntensity = config.lastSunIntensity;
    PerformanceToggles* perfToggles = config.perfToggles;
    auto recordFn = config.recordShadowPass;

    return graph.addPass({
        .name = "Shadow",
        .execute = [&systems, lastSunIntensity, perfToggles, recordFn](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*lastSunIntensity > 0.001f && perfToggles->shadowPass) {
                systems.profiler().beginCpuZone("ShadowRecord");
                systems.profiler().beginGpuZone(ctx.commandBuffer, "ShadowPass");
                recordFn(ctx.commandBuffer, ctx.frameIndex,
                         renderCtx->frame.time, renderCtx->frame.cameraPosition);
                systems.profiler().endGpuZone(ctx.commandBuffer, "ShadowPass");
                systems.profiler().endCpuZone("ShadowRecord");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 50
    });
}

} // namespace ShadowPasses
