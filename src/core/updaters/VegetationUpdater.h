#pragma once

#include "FrameData.h"
#include <vulkan/vulkan.h>

class RendererSystems;
struct VegetationRenderContext;

/**
 * VegetationUpdater - Per-frame updates for vegetation systems
 *
 * Handles: grass, trees, tree LOD, leaves, and detritus
 *
 * Builds VegetationRenderContext once and passes it to methods
 * that need shared per-frame state.
 *
 * Also handles deferred terrain object generation (trees, detritus)
 * which is triggered on the first frame after terrain is ready.
 */
class VegetationUpdater {
public:
    static void update(RendererSystems& systems, const FrameData& frame, VkExtent2D extent);

private:
    // Try to generate deferred terrain objects if not yet done
    static void tryGenerateDeferredObjects(RendererSystems& systems);

    static void updateGrass(RendererSystems& systems, const FrameData& frame,
                            const VegetationRenderContext& ctx);
    static void updateTreeDescriptors(RendererSystems& systems, const FrameData& frame,
                                      const VegetationRenderContext& ctx);
    static void updateTreeLOD(RendererSystems& systems, const FrameData& frame, VkExtent2D extent);
    static void updateLeaf(RendererSystems& systems, const FrameData& frame);
};
