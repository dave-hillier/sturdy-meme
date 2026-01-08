#pragma once

#include <vulkan/vulkan.h>
#include <functional>
#include <vector>
#include <string>

#include "RenderContext.h"

/**
 * ComputeStage - Orchestrates all compute passes before rendering
 *
 * Holds a collection of compute pass functions that are executed in order.
 * Each pass is a lambda that captures its system reference and records
 * compute commands using the RenderContext.
 *
 * GPU Parallelization:
 *   Independent compute passes (those writing to different resources) can execute
 *   in parallel on the GPU. Each pass manages its own barriers for correctness.
 *   The GPU driver automatically overlaps execution of independent dispatches
 *   where barriers permit.
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
    bool stageEnabled = true;  // Master enable for entire stage

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

    // Enable/disable all passes at once (useful for debugging)
    void setAllPassesEnabled(bool enabled) {
        for (auto& pass : passes) {
            pass.enabled = enabled;
        }
    }

    // Enable/disable the entire stage
    void setStageEnabled(bool enabled) {
        stageEnabled = enabled;
    }

    bool isStageEnabled() const {
        return stageEnabled;
    }

    // Get count of enabled passes
    size_t getEnabledPassCount() const {
        size_t count = 0;
        for (const auto& pass : passes) {
            if (pass.enabled) ++count;
        }
        return count;
    }

    void execute(RenderContext& ctx) {
        if (!stageEnabled) return;

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
