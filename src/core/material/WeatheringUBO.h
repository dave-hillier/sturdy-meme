#pragma once

#include <glm/glm.hpp>
#include "MaterialComponents.h"

namespace material {

/**
 * WeatheringUBO - GPU-compatible uniform buffer for weathering effects
 *
 * This struct is std140 aligned for direct upload to GPU uniform buffers.
 * It complements the existing SnowUBO with additional weathering effects.
 *
 * Shader usage: #include "ubo_weathering.glsl"
 */
struct WeatheringUBO {
    // Wetness parameters
    float wetness;              // Global wetness amount (0-1), e.g. from rain
    float wetnessRoughnessScale; // How much wetness reduces roughness
    float puddleThreshold;      // Wetness level above which puddles form
    float waterProximityRange;  // Range for water proximity wetness

    // Water level for proximity wetness
    float waterLevel;           // Y height of nearest water surface
    float padding0;
    float padding1;
    float padding2;

    // Dirt parameters
    glm::vec4 dirtColor;        // RGB = dirt color, A = unused

    float dirtAmount;           // Global dirt amount (0-1)
    float dirtCreviceBias;      // How much dirt accumulates in crevices
    float dirtGravityBias;      // How much dirt accumulates on horizontal surfaces
    float padding3;

    // Moss parameters
    glm::vec4 mossColor;        // RGB = moss color, A = unused

    float mossAmount;           // Global moss amount (0-1)
    float mossMoistureScale;    // How much wetness affects moss growth
    float mossOrientationBias;  // Preference for north-facing/horizontal
    float padding4;

    // Puddle rendering
    float puddleReflectivity;   // Base reflectivity for puddles
    float puddleRippleSpeed;    // Rain ripple animation speed
    float puddleRippleScale;    // Rain ripple pattern scale
    float enablePuddles;        // 1.0 = puddles enabled, 0.0 = disabled

    // Default constructor with reasonable values
    WeatheringUBO() :
        wetness(0.0f),
        wetnessRoughnessScale(0.7f),
        puddleThreshold(0.7f),
        waterProximityRange(5.0f),
        waterLevel(0.0f),
        padding0(0.0f),
        padding1(0.0f),
        padding2(0.0f),
        dirtColor(0.3f, 0.25f, 0.2f, 1.0f),
        dirtAmount(0.0f),
        dirtCreviceBias(0.5f),
        dirtGravityBias(0.5f),
        padding3(0.0f),
        mossColor(0.2f, 0.35f, 0.15f, 1.0f),
        mossAmount(0.0f),
        mossMoistureScale(0.5f),
        mossOrientationBias(0.5f),
        padding4(0.0f),
        puddleReflectivity(0.8f),
        puddleRippleSpeed(1.0f),
        puddleRippleScale(0.5f),
        enablePuddles(1.0f)
    {}

    // Construct from WeatheringComponent
    static WeatheringUBO fromComponent(const WeatheringComponent& comp) {
        WeatheringUBO ubo;

        // Map component fields to UBO
        // Note: Snow is handled by the existing SnowUBO, so we skip snowCoverage
        ubo.wetness = comp.wetness;
        ubo.wetnessRoughnessScale = comp.wetnessRoughnessScale;

        ubo.dirtAmount = comp.dirtAccumulation;
        ubo.dirtColor = glm::vec4(comp.dirtColor, 1.0f);

        ubo.mossAmount = comp.moss;
        ubo.mossColor = glm::vec4(comp.mossColor, 1.0f);

        return ubo;
    }

    // Convert back to WeatheringComponent
    WeatheringComponent toComponent() const {
        WeatheringComponent comp;

        comp.wetness = wetness;
        comp.wetnessRoughnessScale = wetnessRoughnessScale;

        comp.dirtAccumulation = dirtAmount;
        comp.dirtColor = glm::vec3(dirtColor);

        comp.moss = mossAmount;
        comp.mossColor = glm::vec3(mossColor);

        return comp;
    }
};

// Verify std140 alignment (should be multiple of 16 bytes)
static_assert(sizeof(WeatheringUBO) % 16 == 0, "WeatheringUBO must be 16-byte aligned for std140");

} // namespace material
