#pragma once

#include "FrameGraph.h"
#include <glm/glm.hpp>

class RendererSystems;
struct PerformanceToggles;

/**
 * ShadowPasses - Shadow map rendering pass definitions
 */
namespace ShadowPasses {

using ShadowRecordFn = std::function<void(VkCommandBuffer, uint32_t, float, const glm::vec3&)>;

struct Config {
    float* lastSunIntensity = nullptr;
    PerformanceToggles* perfToggles = nullptr;
    ShadowRecordFn recordShadowPass;
};

struct PassIds {
    FrameGraph::PassId shadow = FrameGraph::INVALID_PASS;
    FrameGraph::PassId shadowResolve = FrameGraph::INVALID_PASS;
};

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, const Config& config);

} // namespace ShadowPasses
