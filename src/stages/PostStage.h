#pragma once

#include <vulkan/vulkan.hpp>
#include <functional>

#include "RenderContext.h"

/**
 * PostStage - Post-processing pipeline (HiZ, Bloom, Final composite)
 *
 * Orchestrates the post-render passes:
 * 1. HiZ pyramid generation (for occlusion culling)
 * 2. Bloom multi-pass
 * 3. Final composite with tone mapping and GUI overlay
 *
 * Each system function is set via callbacks to avoid tight coupling.
 *
 * Usage:
 *   PostStage stage;
 *   stage.setHiZRecordFn([&](RenderContext& ctx) { ... });
 *   stage.setBloomRecordFn([&](RenderContext& ctx) { ... });
 *   stage.setPostProcessRecordFn([&](RenderContext& ctx) { ... });
 *   stage.execute(ctx);
 */
struct PostStage {
    using RecordFunction = std::function<void(RenderContext&)>;
    using GuiRenderCallback = std::function<void(VkCommandBuffer)>;

    // HiZ pyramid generation (optional - for occlusion culling)
    RecordFunction hiZRecordFn;
    bool hiZEnabled = true;

    // Bloom multi-pass
    RecordFunction bloomRecordFn;
    bool bloomEnabled = true;

    // Final post-process composite
    RecordFunction postProcessRecordFn;

    // GUI overlay callback (called during post-process render pass)
    GuiRenderCallback guiCallback;

    void setHiZRecordFn(RecordFunction fn) {
        hiZRecordFn = std::move(fn);
    }

    void setBloomRecordFn(RecordFunction fn) {
        bloomRecordFn = std::move(fn);
    }

    void setPostProcessRecordFn(RecordFunction fn) {
        postProcessRecordFn = std::move(fn);
    }

    void setGuiCallback(GuiRenderCallback callback) {
        guiCallback = std::move(callback);
    }

    void setHiZEnabled(bool enabled) { hiZEnabled = enabled; }
    void setBloomEnabled(bool enabled) { bloomEnabled = enabled; }

    void execute(RenderContext& ctx) {
        // 1. HiZ pyramid generation
        if (hiZEnabled && hiZRecordFn) {
            hiZRecordFn(ctx);
        }

        // 2. Bloom passes
        if (bloomEnabled && bloomRecordFn) {
            bloomRecordFn(ctx);
        }

        // 3. Final composite with GUI
        if (postProcessRecordFn) {
            postProcessRecordFn(ctx);
        }
    }
};
