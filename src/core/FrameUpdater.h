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
};
