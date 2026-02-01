#pragma once

#include "FrameGraph.h"

class RendererSystems;
struct PerformanceToggles;

/**
 * ComputePasses - GPU compute dispatch pass definitions
 *
 * Includes: Compute stage, Froxel/Atmosphere
 */
namespace ComputePasses {

struct Config {
    PerformanceToggles* perfToggles = nullptr;
    bool* terrainEnabled = nullptr;
};

struct PassIds {
    FrameGraph::PassId compute = FrameGraph::INVALID_PASS;
    FrameGraph::PassId froxel = FrameGraph::INVALID_PASS;
    FrameGraph::PassId gpuCull = FrameGraph::INVALID_PASS;  // GPU-driven culling pass
};

PassIds addPasses(FrameGraph& graph, RendererSystems& systems, const Config& config);

} // namespace ComputePasses
