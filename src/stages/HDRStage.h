#pragma once

#include <vulkan/vulkan.h>
#include <functional>
#include <vector>
#include <string>
#include <array>

#include "RenderContext.h"

/**
 * HDRStage - Main scene rendering into HDR target
 *
 * Manages the HDR render pass that contains all scene rendering:
 * sky, terrain, scene objects, grass, leaves, weather particles, etc.
 *
 * The stage handles render pass begin/end and executes draw calls in order.
 *
 * Usage:
 *   HDRStage stage;
 *   stage.addDrawCall("sky", [&](RenderContext& ctx) { skySystem.recordDraw(...); });
 *   stage.addDrawCall("terrain", [&](RenderContext& ctx) { terrainSystem.recordDraw(...); });
 *   stage.execute(ctx);
 */
struct HDRStage {
    using DrawFunction = std::function<void(RenderContext&)>;

    struct DrawCall {
        std::string name;
        DrawFunction fn;
        bool enabled = true;
    };

    std::vector<DrawCall> drawCalls;

    // Clear color for HDR target
    std::array<float, 4> clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    float clearDepth = 1.0f;

    void addDrawCall(const std::string& name, DrawFunction fn) {
        drawCalls.push_back({name, std::move(fn), true});
    }

    void setDrawCallEnabled(const std::string& name, bool enabled) {
        for (auto& call : drawCalls) {
            if (call.name == name) {
                call.enabled = enabled;
                return;
            }
        }
    }

    void execute(RenderContext& ctx) {
        const auto& res = ctx.resources;

        // Begin HDR render pass
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = res.hdrRenderPass;
        renderPassInfo.framebuffer = res.hdrFramebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = res.hdrExtent;

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{clearColor[0], clearColor[1], clearColor[2], clearColor[3]}};
        clearValues[1].depthStencil = {clearDepth, 0};

        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(ctx.cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Set viewport and scissor
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(res.hdrExtent.width);
        viewport.height = static_cast<float>(res.hdrExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(ctx.cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = res.hdrExtent;
        vkCmdSetScissor(ctx.cmd, 0, 1, &scissor);

        // Execute all draw calls in order
        for (auto& call : drawCalls) {
            if (call.enabled) {
                call.fn(ctx);
            }
        }

        vkCmdEndRenderPass(ctx.cmd);
    }

    void clear() {
        drawCalls.clear();
    }
};
