#pragma once

#include <vulkan/vulkan.h>
#include <functional>
#include <vector>

#include "../RenderContext.h"
#include "../RenderableBuilder.h"

/**
 * ShadowStage - Conditional shadow pass rendering
 *
 * Manages shadow map rendering with conditional execution based on sun intensity.
 * Uses callbacks to delegate terrain and grass shadow rendering to avoid
 * tight coupling with specific system implementations.
 *
 * Usage:
 *   ShadowStage stage;
 *   stage.setShadowRenderFn([&](RenderContext& ctx, ...) { ... });
 *   stage.setTerrainCallback([&](VkCommandBuffer, uint32_t, const glm::mat4&) { ... });
 *   stage.setGrassCallback([&](VkCommandBuffer, uint32_t, const glm::mat4&) { ... });
 *
 *   if (stage.isEnabled(ctx)) {
 *       stage.execute(ctx);
 *   }
 */
struct ShadowStage {
    // Callback for terrain/grass shadow rendering
    // Signature: (VkCommandBuffer cmd, uint32_t cascadeIndex, const glm::mat4& lightMatrix)
    using DrawCallback = std::function<void(VkCommandBuffer, uint32_t, const glm::mat4&)>;

    // Main shadow render function (wraps ShadowSystem::recordShadowPass)
    using ShadowRenderFn = std::function<void(
        RenderContext& ctx,
        VkDescriptorSet descriptorSet,
        const std::vector<Renderable>& sceneObjects,
        const DrawCallback& terrainCallback,
        const DrawCallback& grassCallback
    )>;

    ShadowRenderFn shadowRenderFn;
    DrawCallback terrainCallback;
    DrawCallback grassCallback;

    // Accessor functions for scene data (set by Renderer)
    std::function<VkDescriptorSet(uint32_t)> getDescriptorSet;
    std::function<const std::vector<Renderable>&()> getSceneObjects;

    // Minimum sun intensity to render shadows
    float sunIntensityThreshold = 0.001f;

    void setShadowRenderFn(ShadowRenderFn fn) {
        shadowRenderFn = std::move(fn);
    }

    void setTerrainCallback(DrawCallback fn) {
        terrainCallback = std::move(fn);
    }

    void setGrassCallback(DrawCallback fn) {
        grassCallback = std::move(fn);
    }

    bool isEnabled(const RenderContext& ctx) const {
        return ctx.frame.sunIntensity > sunIntensityThreshold;
    }

    void execute(RenderContext& ctx) {
        if (!shadowRenderFn || !getDescriptorSet || !getSceneObjects) {
            return;
        }

        shadowRenderFn(
            ctx,
            getDescriptorSet(ctx.frameIndex),
            getSceneObjects(),
            terrainCallback,
            grassCallback
        );
    }
};
