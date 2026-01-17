#include "ComputePasses.h"
#include "RendererSystems.h"
#include "RenderContext.h"
#include "RenderPipeline.h"
#include "PerformanceToggles.h"
#include "Profiler.h"
#include "PostProcessSystem.h"

namespace ComputePasses {

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, RenderPipeline& pipeline, const Config& config) {
    PassIds ids;
    PerformanceToggles* perfToggles = config.perfToggles;

    // Compute pass - runs all GPU compute dispatches
    ids.compute = graph.addPass({
        .name = "Compute",
        .execute = [&systems, &pipeline](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            systems.profiler().beginCpuZone("ComputeDispatch");
            pipeline.computeStage.execute(*renderCtx);
            systems.profiler().endCpuZone("ComputeDispatch");
        },
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 100  // Highest priority - runs first
    });

    // Froxel/Atmosphere pass - volumetric fog and atmosphere LUTs
    ids.froxel = graph.addPass({
        .name = "Froxel",
        .execute = [&systems, &pipeline, perfToggles](FrameGraph::RenderContext& ctx) {
            RenderContext* renderCtx = static_cast<RenderContext*>(ctx.userData);
            if (!renderCtx) return;
            systems.postProcess().setCameraPlanes(
                renderCtx->frame.nearPlane, renderCtx->frame.farPlane);
            if (pipeline.froxelStageFn && (perfToggles->froxelFog || perfToggles->atmosphereLUT)) {
                pipeline.froxelStageFn(*renderCtx);
            }
        },
        .canUseSecondary = false,
        .mainThreadOnly = false,  // Can run parallel with Shadow
        .priority = 50
    });

    return ids;
}

} // namespace ComputePasses
