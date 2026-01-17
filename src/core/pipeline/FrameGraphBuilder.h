#pragma once

#include "FrameGraph.h"
#include "RenderPipeline.h"
#include "PerformanceToggles.h"
#include <vulkan/vulkan_raii.hpp>
#include <functional>

class RendererSystems;

/**
 * FrameGraphBuilder - Wires together domain-specific render passes
 *
 * Delegates pass creation to domain modules in src/core/passes/:
 * - ComputePasses: GPU compute dispatches, froxel/atmosphere
 * - ShadowPasses: Shadow map rendering
 * - WaterPasses: Water GBuffer, SSR, tile culling
 * - HDRPass: Main scene rendering
 * - PostPasses: HiZ, bloom, bilateral grid, final composite
 *
 * This class only wires dependencies between passes:
 *   ComputeStage ──┬──> ShadowPass ──┐
 *                  ├──> Froxel ──────┼──> HDR ──┬──> SSR ─────────┐
 *                  └──> WaterGBuffer ┘          ├──> WaterTileCull┼──> PostProcess
 *                                               ├──> HiZ ──> Bloom┤
 *                                               └──> BilateralGrid┘
 */
class FrameGraphBuilder {
public:
    struct Callbacks {
        std::function<void(VkCommandBuffer, uint32_t, float, const glm::vec3&)> recordShadowPass;
        std::function<void(VkCommandBuffer, uint32_t, float)> recordHDRPass;
        std::function<void(VkCommandBuffer, uint32_t, float, const std::vector<vk::CommandBuffer>&)> recordHDRPassWithSecondaries;
        std::function<void(VkCommandBuffer, uint32_t, float, uint32_t)> recordHDRPassSecondarySlot;
        std::function<void(VkCommandBuffer)>* guiRenderCallback = nullptr;
    };

    struct State {
        float* lastSunIntensity = nullptr;
        bool* hdrPassEnabled = nullptr;
        PerformanceToggles* perfToggles = nullptr;
        std::vector<vk::raii::Framebuffer>* framebuffers = nullptr;
    };

    static bool build(
        FrameGraph& frameGraph,
        RendererSystems& systems,
        RenderPipeline& renderPipeline,
        const Callbacks& callbacks,
        const State& state
    );
};
