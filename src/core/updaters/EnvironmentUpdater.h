#pragma once

#include "FrameData.h"

class RendererSystems;

/**
 * EnvironmentUpdater - Per-frame updates for environment systems
 *
 * Handles: terrain, water, and their interconnections
 */
class EnvironmentUpdater {
public:
    struct Config {
        float maxSnowHeight = 0.3f;
        bool useVolumetricSnow = true;
    };

    static void update(RendererSystems& systems, const FrameData& frame, const Config& config);

private:
    static void updateTerrain(RendererSystems& systems, const FrameData& frame, const Config& config);
    static void updateWater(RendererSystems& systems, const FrameData& frame);
    static void connectWeatherToTerrain(RendererSystems& systems);
};
