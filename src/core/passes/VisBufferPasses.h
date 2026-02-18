#pragma once

#include "FrameGraph.h"

class RendererSystems;

/**
 * VisBufferPasses - Visibility buffer rasterization and material resolve passes
 *
 * Two passes:
 * 1. Raster: Draws scene objects into the V-buffer (R32_UINT with packed instanceId+triangleId)
 * 2. Resolve: Compute shader evaluates materials per-pixel and writes to HDR target
 *
 * Requires:
 * - VisibilityBuffer (render targets, pipelines)
 * - GPUSceneBuffer (instance data)
 * - GPUMaterialBuffer (material properties)
 * - GlobalBufferManager (UBO for view/proj matrices)
 * - SceneManager (scene objects to render)
 * - PostProcessSystem (HDR target to write into)
 */
namespace VisBufferPasses {

struct PassIds {
    FrameGraph::PassId cull = FrameGraph::INVALID_PASS;
    FrameGraph::PassId raster = FrameGraph::INVALID_PASS;
    FrameGraph::PassId resolve = FrameGraph::INVALID_PASS;
};

PassIds addPasses(FrameGraph& graph, RendererSystems& systems);

} // namespace VisBufferPasses
