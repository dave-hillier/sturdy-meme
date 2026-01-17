#pragma once

#include "FrameData.h"
#include <vulkan/vulkan.h>

class RendererSystems;

/**
 * VegetationUpdater - Per-frame updates for vegetation systems
 *
 * Handles: grass, trees, tree LOD, leaves, and detritus
 */
class VegetationUpdater {
public:
    static void update(RendererSystems& systems, const FrameData& frame, VkExtent2D extent);

private:
    static void updateGrass(RendererSystems& systems, const FrameData& frame);
    static void updateTreeDescriptors(RendererSystems& systems, const FrameData& frame);
    static void updateTreeLOD(RendererSystems& systems, const FrameData& frame, VkExtent2D extent);
    static void updateLeaf(RendererSystems& systems, const FrameData& frame);
};
