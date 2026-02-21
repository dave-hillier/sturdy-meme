#pragma once

#include "FrameData.h"
#include <vulkan/vulkan.h>

class RendererSystems;

/**
 * FrameUpdater - Orchestrates per-frame subsystem updates
 *
 * Delegates to specialized updaters:
 * - VegetationUpdater: grass, trees, leaves
 * - AtmosphereUpdater: wind, weather, snow
 * - EnvironmentUpdater: terrain, water
 */
class FrameUpdater {
public:
    struct SnowConfig {
        float maxSnowHeight = 0.3f;
        bool useVolumetricSnow = true;
    };

    /**
     * Update all subsystems for the current frame
     */
    static void updateAllSystems(
        RendererSystems& systems,
        const FrameData& frame,
        VkExtent2D extent,
        const SnowConfig& snowConfig
    );

    /**
     * Populate GPU scene buffer with renderable objects for GPU-driven rendering.
     * Skips player and NPC characters (they use GPU skinning).
     */
    static void populateGPUSceneBuffer(RendererSystems& systems, const FrameData& frame);

    /**
     * Advance triple-buffered systems after command buffer recording.
     * Safe to call before submit since the command buffer already has
     * the current frame's buffer references baked in.
     */
    static void advanceBufferSets(RendererSystems& systems, uint32_t frameIndex);

    /**
     * Update debug line system: begin frame if needed, upload lines.
     */
    static void updateDebugLines(RendererSystems& systems, uint32_t frameIndex);
};
