#include "ShadowPasses.h"
#include "RendererSystems.h"
#include "RenderContext.h"
#include "Profiler.h"
#include "PerformanceToggles.h"
#include "ScreenSpaceShadowSystem.h"

namespace ShadowPasses {

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, const Config& config) {
    PassIds ids;
    float* lastSunIntensity = config.lastSunIntensity;
    PerformanceToggles* perfToggles = config.perfToggles;
    auto recordFn = config.recordShadowPass;

    // Shadow map rendering pass
    ids.shadow = graph.addPass({
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

    // Screen-space shadow resolve pass (compute)
    if (systems.hasScreenSpaceShadow()) {
        ids.shadowResolve = graph.addPass({
            .name = "ShadowResolve",
            .execute = [&systems, lastSunIntensity, perfToggles](FrameGraph::RenderContext& ctx) {
                if (!systems.hasScreenSpaceShadow()) return;
                if (*lastSunIntensity <= 0.001f || !perfToggles->shadowPass) return;
                systems.profiler().beginGpuZone(ctx.commandBuffer, "ShadowResolve");
                systems.screenSpaceShadow()->record(ctx.commandBuffer, ctx.frameIndex);
                systems.profiler().endGpuZone(ctx.commandBuffer, "ShadowResolve");
            },
            .canUseSecondary = false,
            .mainThreadOnly = true,
            .priority = 45  // Between shadow (50) and HDR (30)
        });
    }

    return ids;
}

} // namespace ShadowPasses
