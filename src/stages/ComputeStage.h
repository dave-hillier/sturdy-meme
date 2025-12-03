#pragma once

#include <vulkan/vulkan.h>
#include <functional>
#include <vector>
#include <string>

#include "../RenderContext.h"

/**
 * ComputeStage - Orchestrates all compute passes before rendering
 *
 * Holds a collection of compute pass functions that are executed in order.
 * Each pass is a lambda that captures its system reference and records
 * compute commands using the RenderContext.
 *
 * Usage:
 *   ComputeStage stage;
 *   stage.addPass("terrain", [&](RenderContext& ctx) {
 *       terrainSystem.recordCompute(ctx.cmd, ctx.frameIndex);
 *   });
 *   stage.execute(ctx);
 */
struct ComputeStage {
    using PassFunction = std::function<void(RenderContext&)>;

    struct Pass {
        std::string name;
        PassFunction fn;
        bool enabled = true;
    };

    std::vector<Pass> passes;

    void addPass(const std::string& name, PassFunction fn) {
        passes.push_back({name, std::move(fn), true});
    }

    void setPassEnabled(const std::string& name, bool enabled) {
        for (auto& pass : passes) {
            if (pass.name == name) {
                pass.enabled = enabled;
                return;
            }
        }
    }

    void execute(RenderContext& ctx) {
        for (auto& pass : passes) {
            if (pass.enabled) {
                pass.fn(ctx);
            }
        }
    }

    void clear() {
        passes.clear();
    }
};
