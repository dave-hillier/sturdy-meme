#include "ShadowPasses.h"
#include "RenderContext.h"
#include "Profiler.h"
#include "PerformanceToggles.h"

namespace ShadowPasses {

FrameGraph::PassId addShadowPass(FrameGraph& graph, Profiler& profiler, const Config& config) {
    float* lastSunIntensity = config.lastSunIntensity;
    PerformanceToggles* perfToggles = config.perfToggles;
    auto recordFn = config.recordShadowPass;

    // Capture profiler pointer for lambda
    Profiler* prof = &profiler;

    return graph.addPass({
        .name = "Shadow",
        .execute = [prof, lastSunIntensity, perfToggles, recordFn](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*lastSunIntensity > 0.001f && perfToggles->shadowPass) {
                prof->beginCpuZone("ShadowRecord");
                prof->beginGpuZone(ctx.commandBuffer, "ShadowPass");
                recordFn(ctx.commandBuffer, ctx.frameIndex,
                         renderCtx->frame.time, renderCtx->frame.cameraPosition);
                prof->endGpuZone(ctx.commandBuffer, "ShadowPass");
                prof->endCpuZone("ShadowRecord");
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 50
    });
}

} // namespace ShadowPasses
