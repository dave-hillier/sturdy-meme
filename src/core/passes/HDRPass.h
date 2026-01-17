#pragma once

#include "FrameGraph.h"

class RendererSystems;

/**
 * HDRPass - Main scene HDR rendering pass
 *
 * Renders sky, terrain, scene objects, grass, water, weather, leaves
 * with parallel secondary command buffer support.
 */
namespace HDRPass {

using HDRRecordFn = std::function<void(VkCommandBuffer, uint32_t, float)>;
using HDRSecondaryRecordFn = std::function<void(VkCommandBuffer, uint32_t, float, const std::vector<vk::CommandBuffer>&)>;
using HDRSlotRecordFn = std::function<void(VkCommandBuffer, uint32_t, float, uint32_t)>;

struct Config {
    bool* hdrPassEnabled = nullptr;
    HDRRecordFn recordHDRPass;
    HDRSecondaryRecordFn recordHDRPassWithSecondaries;
    HDRSlotRecordFn recordHDRPassSecondarySlot;
};

FrameGraph::PassId addPass(FrameGraph& graph, RendererSystems& systems, const Config& config);

} // namespace HDRPass
