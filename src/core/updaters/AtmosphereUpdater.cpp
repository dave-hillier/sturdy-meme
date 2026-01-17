#include "AtmosphereUpdater.h"
#include "RendererSystems.h"
#include "Profiler.h"

#include "WindSystem.h"
#include "WeatherSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "EnvironmentSettings.h"

#include <glm/glm.hpp>

void AtmosphereUpdater::update(RendererSystems& systems, const FrameData& frame, const SnowConfig& snowConfig) {
    updateWind(systems, frame);
    updateWeather(systems, frame);
    updateSnow(systems, frame);
}

void AtmosphereUpdater::updateWind(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("Update:Wind");
    systems.wind().update(frame.deltaTime);
    systems.wind().updateUniforms(frame.frameIndex);
    systems.profiler().endCpuZone("Update:Wind");
}

void AtmosphereUpdater::updateWeather(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("Update:Weather");
    systems.weather().updateUniforms(frame.frameIndex, frame.cameraPosition, frame.viewProj,
                                     frame.deltaTime, frame.time, systems.wind());
    systems.profiler().endCpuZone("Update:Weather");
}

void AtmosphereUpdater::updateSnow(RendererSystems& systems, const FrameData& frame) {
    systems.profiler().beginCpuZone("Update:Snow");

    // Update snow mask system - accumulation/melting based on weather type
    bool isSnowing = (systems.weather().getWeatherType() == 1);  // 1 = snow
    float weatherIntensity = systems.weather().getIntensity();
    auto& envSettings = systems.environmentSettings();

    // Auto-adjust snow amount based on weather state
    if (isSnowing && weatherIntensity > 0.0f) {
        envSettings.snowAmount = glm::min(envSettings.snowAmount + envSettings.snowAccumulationRate * frame.deltaTime, 1.0f);
    } else if (envSettings.snowAmount > 0.0f) {
        envSettings.snowAmount = glm::max(envSettings.snowAmount - envSettings.snowMeltRate * frame.deltaTime, 0.0f);
    }

    systems.snowMask().setMaskCenter(frame.cameraPosition);
    systems.snowMask().updateUniforms(frame.frameIndex, frame.deltaTime, isSnowing, weatherIntensity, envSettings);

    // Update volumetric snow system
    systems.volumetricSnow().setCameraPosition(frame.cameraPosition);
    systems.volumetricSnow().setWindDirection(glm::vec2(systems.wind().getEnvironmentSettings().windDirection.x,
                                                        systems.wind().getEnvironmentSettings().windDirection.y));
    systems.volumetricSnow().setWindStrength(systems.wind().getEnvironmentSettings().windStrength);
    systems.volumetricSnow().updateUniforms(frame.frameIndex, frame.deltaTime, isSnowing, weatherIntensity, envSettings);

    // Add player footprint interaction with snow
    if (envSettings.snowAmount > 0.1f) {
        systems.snowMask().addInteraction(frame.playerPosition, frame.playerCapsuleRadius * 1.5f, 0.3f);
        systems.volumetricSnow().addInteraction(frame.playerPosition, frame.playerCapsuleRadius * 1.5f, 0.3f);
    }

    systems.profiler().endCpuZone("Update:Snow");
}
