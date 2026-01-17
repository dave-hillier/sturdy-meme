#pragma once

#include "FrameGraph.h"
#include <vulkan/vulkan_raii.hpp>

class RendererSystems;
struct PerformanceToggles;

/**
 * PostPasses - Post-processing pass definitions
 *
 * Includes: HiZ, Bloom, BilateralGrid, PostProcess (final composite)
 */
namespace PostPasses {

struct Config {
    std::function<void(VkCommandBuffer)>* guiRenderCallback = nullptr;
    std::vector<vk::raii::Framebuffer>* framebuffers = nullptr;
    PerformanceToggles* perfToggles = nullptr;
};

struct PassIds {
    FrameGraph::PassId hiZ = FrameGraph::INVALID_PASS;
    FrameGraph::PassId bloom = FrameGraph::INVALID_PASS;
    FrameGraph::PassId bilateralGrid = FrameGraph::INVALID_PASS;
    FrameGraph::PassId postProcess = FrameGraph::INVALID_PASS;
};

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, const Config& config);

} // namespace PostPasses
