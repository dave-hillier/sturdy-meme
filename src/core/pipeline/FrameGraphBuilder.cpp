#include "FrameGraphBuilder.h"
#include "RendererSystems.h"

// Domain-specific pass modules
#include "passes/ComputePasses.h"
#include "passes/ShadowPasses.h"
#include "passes/WaterPasses.h"
#include "passes/HDRPass.h"
#include "passes/PostPasses.h"

#include <SDL3/SDL.h>

bool FrameGraphBuilder::build(
    FrameGraph& frameGraph,
    RendererSystems& systems,
    const Callbacks& callbacks,
    const State& state)
{
    frameGraph.clear();

    // ===== ADD PASSES FROM DOMAIN MODULES =====

    // Compute passes (compute stage + froxel)
    ComputePasses::Config computeConfig;
    computeConfig.perfToggles = state.perfToggles;
    computeConfig.terrainEnabled = state.terrainEnabled;
    auto computeIds = ComputePasses::addPasses(frameGraph, systems, computeConfig);

    // Shadow passes (shadow map + screen-space resolve)
    ShadowPasses::Config shadowConfig;
    shadowConfig.lastSunIntensity = state.lastSunIntensity;
    shadowConfig.perfToggles = state.perfToggles;
    shadowConfig.recordShadowPass = callbacks.recordShadowPass;
    auto shadowIds = ShadowPasses::addPasses(frameGraph, systems, shadowConfig);

    // Water passes (GBuffer, SSR, tile cull)
    WaterPasses::Config waterConfig;
    waterConfig.hdrPassEnabled = state.hdrPassEnabled;
    waterConfig.perfToggles = state.perfToggles;
    auto waterIds = WaterPasses::addPasses(frameGraph, systems, waterConfig);

    // HDR pass (main scene rendering)
    HDRPass::Config hdrConfig;
    hdrConfig.hdrPassEnabled = state.hdrPassEnabled;
    hdrConfig.recordHDRPass = callbacks.recordHDRPass;
    hdrConfig.recordHDRPassWithSecondaries = callbacks.recordHDRPassWithSecondaries;
    hdrConfig.recordHDRPassSecondarySlot = callbacks.recordHDRPassSecondarySlot;
    auto hdr = HDRPass::addPass(frameGraph, systems, hdrConfig);

    // Post passes (HiZ, bloom, bilateral grid, final composite)
    PostPasses::Config postConfig;
    postConfig.guiRenderCallback = callbacks.guiRenderCallback;
    postConfig.framebuffers = state.framebuffers;
    postConfig.perfToggles = state.perfToggles;
    auto postIds = PostPasses::addPasses(frameGraph, systems, postConfig);

    // ===== WIRE DEPENDENCIES =====
    // Shadow and Froxel depend on Compute
    frameGraph.addDependency(computeIds.compute, shadowIds.shadow);
    frameGraph.addDependency(computeIds.compute, computeIds.froxel);
    frameGraph.addDependency(computeIds.compute, waterIds.waterGBuffer);

    // GPU cull depends on Compute (needs scene data uploaded)
    if (computeIds.gpuCull != FrameGraph::INVALID_PASS) {
        frameGraph.addDependency(computeIds.compute, computeIds.gpuCull);
    }

    // Shadow resolve depends on shadow map pass
    if (shadowIds.shadowResolve != FrameGraph::INVALID_PASS) {
        frameGraph.addDependency(shadowIds.shadow, shadowIds.shadowResolve);
    }

    // HDR depends on Shadow (or ShadowResolve if available), Froxel, Water GBuffer, and GPU Cull
    if (shadowIds.shadowResolve != FrameGraph::INVALID_PASS) {
        frameGraph.addDependency(shadowIds.shadowResolve, hdr);
    } else {
        frameGraph.addDependency(shadowIds.shadow, hdr);
    }
    frameGraph.addDependency(computeIds.froxel, hdr);
    frameGraph.addDependency(waterIds.waterGBuffer, hdr);
    if (computeIds.gpuCull != FrameGraph::INVALID_PASS) {
        frameGraph.addDependency(computeIds.gpuCull, hdr);
    }

    // Post-HDR passes depend on HDR
    frameGraph.addDependency(hdr, waterIds.ssr);
    frameGraph.addDependency(hdr, waterIds.waterTileCull);
    frameGraph.addDependency(hdr, postIds.hiZ);
    frameGraph.addDependency(hdr, postIds.bilateralGrid);
    frameGraph.addDependency(hdr, postIds.godRays);

    // Bloom depends on HiZ
    frameGraph.addDependency(postIds.hiZ, postIds.bloom);

    // Final composite depends on all post-HDR passes
    frameGraph.addDependency(waterIds.ssr, postIds.postProcess);
    frameGraph.addDependency(waterIds.waterTileCull, postIds.postProcess);
    frameGraph.addDependency(postIds.bloom, postIds.postProcess);
    frameGraph.addDependency(postIds.bilateralGrid, postIds.postProcess);
    frameGraph.addDependency(postIds.godRays, postIds.postProcess);

    // Compile the graph
    if (!frameGraph.compile()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to compile FrameGraph");
        return false;
    }

    SDL_Log("FrameGraph setup complete:\n%s", frameGraph.debugString().c_str());
    return true;
}
