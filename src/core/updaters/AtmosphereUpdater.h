#pragma once

#include "FrameData.h"

class RendererSystems;
struct EnvironmentSettings;

/**
 * AtmosphereUpdater - Per-frame updates for atmosphere/weather systems
 *
 * Handles: wind, weather, snow mask, volumetric snow
 */
class AtmosphereUpdater {
public:
    struct SnowConfig {
        float maxSnowHeight = 0.3f;
        bool useVolumetricSnow = true;
    };

    static void update(RendererSystems& systems, const FrameData& frame, const SnowConfig& snowConfig);

private:
    static void updateWind(RendererSystems& systems, const FrameData& frame);
    static void updateWeather(RendererSystems& systems, const FrameData& frame);
    static void updateSnow(RendererSystems& systems, const FrameData& frame);
};
