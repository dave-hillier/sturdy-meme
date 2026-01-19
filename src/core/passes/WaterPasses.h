#pragma once

#include "FrameGraph.h"
#include "WaterPassResources.h"

struct PerformanceToggles;

/**
 * WaterPasses - Water rendering pass definitions
 *
 * Includes: WaterGBuffer, SSR, WaterTileCull
 */
namespace WaterPasses {

struct Config {
    bool* hdrPassEnabled = nullptr;
    PerformanceToggles* perfToggles = nullptr;
};

struct PassIds {
    FrameGraph::PassId waterGBuffer = FrameGraph::INVALID_PASS;
    FrameGraph::PassId ssr = FrameGraph::INVALID_PASS;
    FrameGraph::PassId waterTileCull = FrameGraph::INVALID_PASS;
};

PassIds addPasses(FrameGraph& graph, const WaterPassResources& resources, const Config& config);

} // namespace WaterPasses
