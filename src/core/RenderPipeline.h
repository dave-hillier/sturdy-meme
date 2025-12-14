#pragma once

#include "RenderContext.h"
#include "stages/ComputeStage.h"
#include "stages/ShadowStage.h"
#include "stages/HDRStage.h"
#include "stages/PostStage.h"

/**
 * RenderPipeline - Stage coordinator for the render loop
 *
 * Owns all render stages and executes them in order:
 * 1. ComputeStage - All compute passes (terrain, grass, weather, etc.)
 * 2. ShadowStage - Shadow map rendering (conditional on sun intensity)
 * 3. HDRStage - Main scene rendering into HDR target
 * 4. PostStage - HiZ, Bloom, final composite
 *
 * The pipeline doesn't own any systems - it holds function references
 * that are populated by Renderer during initialization.
 *
 * Usage:
 *   RenderPipeline pipeline;
 *   // Populate stages with lambdas capturing system references
 *   pipeline.computeStage.addPass("terrain", ...);
 *   pipeline.hdrStage.addDrawCall("sky", ...);
 *   // ...
 *   pipeline.execute(ctx);
 */
struct RenderPipeline {
    ComputeStage computeStage;
    ShadowStage shadowStage;
    HDRStage hdrStage;
    PostStage postStage;

    // Additional stages that run between main passes
    using StageFunction = std::function<void(RenderContext&)>;

    // Volumetric fog / atmosphere (runs after shadow, before HDR)
    StageFunction froxelStageFn;
    StageFunction atmosphereStageFn;

    void setFroxelStageFn(StageFunction fn) {
        froxelStageFn = std::move(fn);
    }

    void setAtmosphereStageFn(StageFunction fn) {
        atmosphereStageFn = std::move(fn);
    }

    void execute(RenderContext& ctx) {
        // 1. Compute passes (terrain LOD, grass simulation, weather particles, etc.)
        computeStage.execute(ctx);

        // 2. Shadow pass (conditional on sun intensity)
        if (shadowStage.isEnabled(ctx)) {
            shadowStage.execute(ctx);
        }

        // 3. Volumetric fog / atmosphere updates (after shadows, before HDR)
        if (froxelStageFn) {
            froxelStageFn(ctx);
        }
        if (atmosphereStageFn) {
            atmosphereStageFn(ctx);
        }

        // 4. HDR scene rendering
        hdrStage.execute(ctx);

        // 5. Post-processing (HiZ, Bloom, final composite)
        postStage.execute(ctx);
    }

    void clear() {
        computeStage.clear();
        hdrStage.clear();
        froxelStageFn = nullptr;
        atmosphereStageFn = nullptr;
    }
};
