#pragma once

#include "FrameGraph.h"

class RendererSystems;

/**
 * VisBufferPasses - Visibility buffer material resolve pass
 *
 * Dispatches the compute resolve shader that evaluates materials
 * per-pixel from the visibility buffer and writes to the HDR target.
 *
 * Requires:
 * - VisibilityBuffer (rasterization already completed)
 * - GPUSceneBuffer (instance data)
 * - GPUMaterialBuffer (material properties)
 * - PostProcessSystem (HDR target to write into)
 */
namespace VisBufferPasses {

struct PassIds {
    FrameGraph::PassId resolve = FrameGraph::INVALID_PASS;
};

PassIds addPasses(FrameGraph& graph, RendererSystems& systems);

} // namespace VisBufferPasses
