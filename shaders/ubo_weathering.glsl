// Weathering UBO definition - include this in shaders that need weathering effects
// Complements ubo_snow.glsl with wetness, dirt, and moss effects
// Part of the composable material system

#ifndef UBO_WEATHERING_GLSL
#define UBO_WEATHERING_GLSL

#include "bindings.glsl"

// Default to main rendering set binding; terrain shader overrides to BINDING_TERRAIN_WEATHERING_UBO
#ifndef WEATHERING_UBO_BINDING
#define WEATHERING_UBO_BINDING BINDING_WEATHERING_UBO
#endif

layout(std140, binding = WEATHERING_UBO_BINDING) uniform WeatheringUBO {
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
    vec4 dirtColor;             // RGB = dirt color, A = unused

    float dirtAmount;           // Global dirt amount (0-1)
    float dirtCreviceBias;      // How much dirt accumulates in crevices
    float dirtGravityBias;      // How much dirt accumulates on horizontal surfaces
    float padding3;

    // Moss parameters
    vec4 mossColor;             // RGB = moss color, A = unused

    float mossAmount;           // Global moss amount (0-1)
    float mossMoistureScale;    // How much wetness affects moss growth
    float mossOrientationBias;  // Preference for north-facing/horizontal
    float padding4;

    // Puddle rendering
    float puddleReflectivity;   // Base reflectivity for puddles
    float puddleRippleSpeed;    // Rain ripple animation speed
    float puddleRippleScale;    // Rain ripple pattern scale
    float enablePuddles;        // 1.0 = puddles enabled, 0.0 = disabled
} weathering;

#endif // UBO_WEATHERING_GLSL
