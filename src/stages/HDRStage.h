#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
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
    bool stageEnabled = true;  // Master enable for entire stage

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

    // Enable/disable all draw calls at once (useful for debugging)
    void setAllDrawCallsEnabled(bool enabled) {
        for (auto& call : drawCalls) {
            call.enabled = enabled;
        }
    }

    // Enable/disable the entire stage
    void setStageEnabled(bool enabled) {
        stageEnabled = enabled;
    }

    bool isStageEnabled() const {
        return stageEnabled;
    }

    // Get count of enabled draw calls
    size_t getEnabledDrawCallCount() const {
        size_t count = 0;
        for (const auto& call : drawCalls) {
            if (call.enabled) ++count;
        }
        return count;
    }

    void execute(RenderContext& ctx) {
        if (!stageEnabled) return;
        const auto& res = ctx.resources;
        vk::CommandBuffer vkCmd(ctx.cmd);

        // Begin HDR render pass
        std::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{
            clearColor[0], clearColor[1], clearColor[2], clearColor[3]});
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{clearDepth, 0};

        auto renderPassInfo = vk::RenderPassBeginInfo{}
            .setRenderPass(res.hdrRenderPass)
            .setFramebuffer(res.hdrFramebuffer)
            .setRenderArea(vk::Rect2D{{0, 0}, vk::Extent2D{res.hdrExtent.width, res.hdrExtent.height}})
            .setClearValues(clearValues);

        vkCmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // Set viewport and scissor
        auto viewport = vk::Viewport{}
            .setX(0.0f)
            .setY(0.0f)
            .setWidth(static_cast<float>(res.hdrExtent.width))
            .setHeight(static_cast<float>(res.hdrExtent.height))
            .setMinDepth(0.0f)
            .setMaxDepth(1.0f);
        vkCmd.setViewport(0, viewport);

        auto scissor = vk::Rect2D{}
            .setOffset({0, 0})
            .setExtent(vk::Extent2D{res.hdrExtent.width, res.hdrExtent.height});
        vkCmd.setScissor(0, scissor);

        // Execute all draw calls in order
        for (auto& call : drawCalls) {
            if (call.enabled) {
                call.fn(ctx);
            }
        }

        vkCmd.endRenderPass();
    }

    void clear() {
        drawCalls.clear();
    }
};
