#include "UBOBuilder.h"

#include "Camera.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "ShadowSystem.h"
#include "WindSystem.h"
#include "AtmosphereLUTSystem.h"
#include "FroxelSystem.h"
#include "SceneManager.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "CloudShadowSystem.h"
#include "EnvironmentSettings.h"

#include <glm/gtc/constants.hpp>
#include <cmath>

UBOBuilder::UBOBuilder(const Systems& systems)
    : systems_(systems)
{
}

void UBOBuilder::setSystems(const Systems& systems) {
    systems_ = systems;
}

UBOBuilder::LightingParams UBOBuilder::calculateLightingParams(float timeOfDay) const {
    LightingParams params{};

    DateTime dateTime = DateTime::fromTimeOfDay(timeOfDay, systems_.timeSystem->getCurrentYear(),
                                                 systems_.timeSystem->getCurrentMonth(),
                                                 systems_.timeSystem->getCurrentDay());
    CelestialPosition sunPos = systems_.celestialCalculator->calculateSunPosition(dateTime);
    MoonPosition moonPos = systems_.celestialCalculator->calculateMoonPosition(dateTime);

    params.sunDir = sunPos.direction;
    params.moonDir = moonPos.direction;
    params.sunIntensity = sunPos.intensity;
    params.moonIntensity = moonPos.intensity;

    // Smooth transition for moon as light source during twilight
    if (moonPos.altitude > -5.0f) {
        float twilightFactor = glm::smoothstep(10.0f, -6.0f, sunPos.altitude);
        params.moonIntensity *= (1.0f + twilightFactor * 1.0f);
    }

    // Apply user-controlled moon brightness multiplier
    params.moonIntensity *= systems_.timeSystem->getMoonBrightness();

    params.sunColor = systems_.celestialCalculator->getSunColor(sunPos.altitude);
    params.moonColor = systems_.celestialCalculator->getMoonColor(moonPos.altitude, moonPos.illumination);
    params.ambientColor = systems_.celestialCalculator->getAmbientColor(sunPos.altitude);

    // Apply moon phase override if enabled, otherwise use astronomical calculation
    if (systems_.timeSystem->isMoonPhaseOverrideEnabled()) {
        params.moonPhase = systems_.timeSystem->getMoonPhase();
        // Recalculate illumination based on manual phase
        float phaseAngle = params.moonPhase * 2.0f * glm::pi<float>();
        float illumination = (1.0f - std::cos(phaseAngle)) * 0.5f;
        // Adjust moon color based on manual phase illumination
        params.moonColor = systems_.celestialCalculator->getMoonColor(moonPos.altitude, illumination);
    } else {
        params.moonPhase = moonPos.phase;
    }

    // Eclipse simulation - affects sun intensity
    params.eclipseAmount = systems_.timeSystem->isEclipseEnabled()
                           ? systems_.timeSystem->getEclipseAmount()
                           : 0.0f;

    params.julianDay = dateTime.toJulianDay();

    return params;
}

UniformBufferObject UBOBuilder::buildUniformBufferData(
    const Camera& camera,
    const LightingParams& lighting,
    float timeOfDay,
    const MainUBOConfig& config) const
{
    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = camera.getViewMatrix();
    ubo.proj = camera.getProjectionMatrix();

    // Copy cascade matrices from shadow system
    const auto& cascadeMatrices = systems_.shadowSystem->getCascadeMatrices();
    for (uint32_t i = 0; i < NUM_SHADOW_CASCADES; i++) {
        ubo.cascadeViewProj[i] = cascadeMatrices[i];
    }

    // Store view-space split depths from shadow system
    const auto& cascadeSplitDepths = systems_.shadowSystem->getCascadeSplitDepths();
    ubo.cascadeSplits = glm::vec4(
        cascadeSplitDepths[1],
        cascadeSplitDepths[2],
        cascadeSplitDepths[3],
        cascadeSplitDepths[4]
    );

    ubo.toSunDirection = glm::vec4(lighting.sunDir, lighting.sunIntensity);
    ubo.moonDirection = glm::vec4(lighting.moonDir, lighting.moonIntensity);
    ubo.sunColor = glm::vec4(lighting.sunColor, 1.0f);
    ubo.moonColor = glm::vec4(lighting.moonColor, lighting.moonPhase);  // Pass moon phase in alpha channel
    ubo.ambientColor = glm::vec4(lighting.ambientColor, 1.0f);
    ubo.cameraPosition = glm::vec4(camera.getPosition(), 1.0f);

    // Point light from the glowing sphere (position updated by physics)
    float pointLightIntensity = 5.0f;
    float pointLightRadius = 8.0f;
    ubo.pointLightPosition = glm::vec4(systems_.sceneManager->getOrbLightPosition(), pointLightIntensity);
    ubo.pointLightColor = glm::vec4(1.0f, 0.9f, 0.7f, pointLightRadius);

    // Wind parameters for cloud animation
    glm::vec2 windDir = systems_.windSystem->getWindDirection();
    float windSpeed = systems_.windSystem->getWindSpeed();
    float windTime = systems_.windSystem->getTime();
    ubo.windDirectionAndSpeed = glm::vec4(windDir.x, windDir.y, windSpeed, windTime);

    ubo.timeOfDay = timeOfDay;
    ubo.shadowMapSize = static_cast<float>(systems_.shadowSystem->getShadowMapSize());
    ubo.debugCascades = config.showCascadeDebug ? 1.0f : 0.0f;
    // Store Julian day as offset from J2000 epoch for better float precision
    // Full Julian day is ~2.4 million which loses precision in 32-bit float
    // Offset is typically < 10000 for modern dates, preserving sub-hour precision
    constexpr double J2000_EPOCH = 2451545.0;  // January 1, 2000, 12:00 TT
    ubo.julianDayOffset = static_cast<float>(lighting.julianDay - J2000_EPOCH);
    ubo.cloudStyle = config.useParaboloidClouds ? 1.0f : 0.0f;
    ubo.cameraNear = camera.getNearPlane();
    ubo.cameraFar = camera.getFarPlane();
    ubo.eclipseAmount = lighting.eclipseAmount;

    // Copy atmosphere parameters from AtmosphereLUTSystem for use in atmosphere_common.glsl
    const auto& atmosParams = systems_.atmosphereLUTSystem->getAtmosphereParams();
    ubo.atmosRayleighScattering = glm::vec4(atmosParams.rayleighScatteringBase, atmosParams.rayleighScaleHeight);
    ubo.atmosMieParams = glm::vec4(atmosParams.mieScatteringBase, atmosParams.mieAbsorptionBase,
                                   atmosParams.mieScaleHeight, atmosParams.mieAnisotropy);
    ubo.atmosOzoneAbsorption = glm::vec4(atmosParams.ozoneAbsorption, atmosParams.ozoneLayerCenter);
    ubo.atmosOzoneWidth = atmosParams.ozoneLayerWidth;

    // Copy height fog parameters from FroxelSystem for use in atmosphere_common.glsl
    ubo.heightFogParams = glm::vec4(systems_.froxelSystem->getFogBaseHeight(),
                                     systems_.froxelSystem->getFogScaleHeight(),
                                     systems_.froxelSystem->getFogDensity(),
                                     0.0f);
    ubo.heightFogLayerParams = glm::vec4(systems_.froxelSystem->getLayerThickness(),
                                          systems_.froxelSystem->getLayerDensity(),
                                          0.0f, 0.0f);

    // Cloud parameters for sky.frag and cloud systems
    ubo.cloudCoverage = config.cloudCoverage;
    ubo.cloudDensity = config.cloudDensity;

    // Moon rendering parameters for sky.frag
    ubo.moonBrightness = systems_.timeSystem->getMoonBrightness();
    ubo.moonDiscIntensity = systems_.timeSystem->getMoonDiscIntensity();
    ubo.moonEarthshine = systems_.timeSystem->getMoonEarthshine();
    ubo.moonPad = 0.0f;

    // Sky rendering parameters for sky.frag
    ubo.skyExposure = config.skyExposure;
    ubo.skyPad1 = 0.0f;
    ubo.skyPad2 = 0.0f;
    ubo.skyPad3 = 0.0f;

    return ubo;
}

SnowUBO UBOBuilder::buildSnowUBOData(const SnowConfig& config) const {
    SnowUBO snow{};

    snow.snowAmount = systems_.environmentSettings->snowAmount;
    snow.snowRoughness = systems_.environmentSettings->snowRoughness;
    snow.snowTexScale = systems_.environmentSettings->snowTexScale;
    snow.useVolumetricSnow = config.useVolumetricSnow ? 1.0f : 0.0f;
    snow.snowColor = glm::vec4(systems_.environmentSettings->snowColor, 1.0f);
    snow.snowMaskParams = glm::vec4(systems_.snowMaskSystem->getMaskOrigin(),
                                     systems_.snowMaskSystem->getMaskSize(), 0.0f);

    // Volumetric snow cascade parameters
    auto cascadeParams = systems_.volumetricSnowSystem->getCascadeParams();
    snow.snowCascade0Params = cascadeParams[0];
    snow.snowCascade1Params = cascadeParams[1];
    snow.snowCascade2Params = cascadeParams[2];
    snow.snowMaxHeight = config.maxSnowHeight;
    snow.debugSnowDepth = config.showSnowDepthDebug ? 1.0f : 0.0f;
    snow.rainWetness = 0.0f;  // Set by weather system in Renderer
    snow.snowPadding = 0.0f;

    return snow;
}

CloudShadowUBO UBOBuilder::buildCloudShadowUBOData() const {
    CloudShadowUBO cloudShadowUbo{};

    cloudShadowUbo.cloudShadowMatrix = systems_.cloudShadowSystem->getWorldToShadowUV();
    cloudShadowUbo.cloudShadowIntensity = systems_.cloudShadowSystem->getShadowIntensity();
    cloudShadowUbo.cloudShadowEnabled = systems_.cloudShadowSystem->isEnabled() ? 1.0f : 0.0f;
    cloudShadowUbo.cloudShadowPadding = glm::vec2(0.0f);

    return cloudShadowUbo;
}
