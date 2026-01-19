#include "HDRPass.h"
#include "RenderContext.h"
#include "Profiler.h"

namespace HDRPass {

FrameGraph::PassId addPass(FrameGraph& graph, Profiler& profiler, const Config& config) {
    bool* hdrPassEnabled = config.hdrPassEnabled;
    auto recordHDR = config.recordHDRPass;
    auto recordHDRSecondaries = config.recordHDRPassWithSecondaries;
    auto recordHDRSlot = config.recordHDRPassSecondarySlot;

    // Capture profiler pointer for lambda
    Profiler* prof = &profiler;

    return graph.addPass({
        .name = "HDR",
        .execute = [prof, hdrPassEnabled, recordHDR, recordHDRSecondaries](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            if (*hdrPassEnabled) {
                prof->beginCpuZone("RenderPassRecord");
                if (ctx.secondaryBuffers && !ctx.secondaryBuffers->empty()) {
                    // Execute with pre-recorded secondary buffers (parallel path)
                    recordHDRSecondaries(ctx.commandBuffer, ctx.frameIndex,
                                         renderCtx->frame.time, *ctx.secondaryBuffers);
                } else {
                    // Fallback to sequential recording
                    recordHDR(ctx.commandBuffer, ctx.frameIndex, renderCtx->frame.time);
                }
                prof->endCpuZone("RenderPassRecord");
            }
        },
        .canUseSecondary = true,
        .mainThreadOnly = true,  // Main thread begins render pass, but secondaries record in parallel
        .priority = 30,
        .secondarySlots = 3,  // 3 parallel recording slots
        .secondaryRecord = [recordHDRSlot](FrameGraph::RenderContext& ctx, uint32_t slot) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            recordHDRSlot(ctx.commandBuffer, ctx.frameIndex, renderCtx->frame.time, slot);
        }
    });
}

} // namespace HDRPass
