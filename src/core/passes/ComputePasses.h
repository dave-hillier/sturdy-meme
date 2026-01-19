#pragma once

#include "FrameGraph.h"

struct ComputePassResources;
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
};

PassIds addPasses(FrameGraph& graph, const ComputePassResources& resources, const Config& config);

} // namespace ComputePasses
