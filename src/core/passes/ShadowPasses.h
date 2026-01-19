#pragma once

#include "FrameGraph.h"
#include <glm/glm.hpp>

class Profiler;
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

FrameGraph::PassId addShadowPass(FrameGraph& graph, Profiler& profiler, const Config& config);

} // namespace ShadowPasses
