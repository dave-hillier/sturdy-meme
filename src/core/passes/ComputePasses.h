#pragma once

#include "FrameGraph.h"

class RendererSystems;
class RenderPipeline;
struct PerformanceToggles;

/**
 * ComputePasses - GPU compute dispatch pass definitions
 *
 * Includes: Compute stage, Froxel/Atmosphere
 */
namespace ComputePasses {

struct Config {
    PerformanceToggles* perfToggles = nullptr;
};

struct PassIds {
    FrameGraph::PassId compute = FrameGraph::INVALID_PASS;
    FrameGraph::PassId froxel = FrameGraph::INVALID_PASS;
};

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, RenderPipeline& pipeline, const Config& config);

} // namespace ComputePasses
