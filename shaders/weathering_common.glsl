// Weathering effects common functions
// Provides wetness, dirt, and moss effects that complement snow_common.glsl
// Part of the composable material system
#ifndef WEATHERING_COMMON_GLSL
#define WEATHERING_COMMON_GLSL

// ============================================================================
// Wetness Effects
// ============================================================================

// Surface wetness modifies material properties:
// - Darkens albedo (water absorption)
// - Reduces roughness (water film is smooth)
// - Adds subtle reflectivity

// Wetness material modification
struct WetnessResult {
    vec3 albedo;
    float roughness;
    float metallic;
};

// Apply wetness to surface material
// wetness: 0.0 = dry, 1.0 = fully wet
WetnessResult applyWetness(vec3 baseAlbedo, float baseRoughness, float baseMetallic, float wetness) {
    WetnessResult result;

    // Wet surfaces are darker (water absorption)
    // Typical darkening factor is 0.6-0.8 for fully wet surfaces
    float darkeningFactor = mix(1.0, 0.6, wetness);
    result.albedo = baseAlbedo * darkeningFactor;

    // Wet surfaces are smoother due to water film
    // Water film roughness is approximately 0.1
    float wetRoughness = 0.1;
    result.roughness = mix(baseRoughness, wetRoughness, wetness * 0.7);

    // Wet non-metallic surfaces gain slight specular (water film)
    // But don't increase metallic - use Fresnel increase instead
    result.metallic = baseMetallic;

    return result;
}

// Simple wetness albedo modification
vec3 applyWetnessAlbedo(vec3 baseAlbedo, float wetness) {
    float darkeningFactor = mix(1.0, 0.6, wetness);
    return baseAlbedo * darkeningFactor;
}

// Wetness roughness modification
float applyWetnessRoughness(float baseRoughness, float wetness) {
    return mix(baseRoughness, 0.1, wetness * 0.7);
}

// Calculate wetness from world position and mask
// Can be used with procedural patterns or texture masks
float calculateWetness(
    float baseWetness,          // Global wetness amount (0-1)
    vec3 worldPos,              // World position for procedural variation
    vec3 normalWS,              // Surface normal for puddle accumulation
    float waterLevel,           // Water surface level for proximity wetness
    float waterProximityRange   // Range to check for water proximity
) {
    // Base wetness (from rain, etc.)
    float wetness = baseWetness;

    // Slope-based accumulation - puddles form on flat surfaces
    float upDot = max(dot(normalWS, vec3(0.0, 1.0, 0.0)), 0.0);
    float puddleFactor = smoothstep(0.7, 0.95, upDot);  // Only on near-flat surfaces
    wetness = mix(wetness, min(wetness * 2.0, 1.0), puddleFactor * baseWetness);

    // Water proximity wetness - surfaces near water are wetter
    if (waterProximityRange > 0.0) {
        float heightAboveWater = worldPos.y - waterLevel;
        if (heightAboveWater >= 0.0 && heightAboveWater < waterProximityRange) {
            float proximityWetness = 1.0 - (heightAboveWater / waterProximityRange);
            proximityWetness = proximityWetness * proximityWetness;  // Quadratic falloff
            wetness = max(wetness, proximityWetness);
        }
    }

    return clamp(wetness, 0.0, 1.0);
}

// ============================================================================
// Dirt/Grime Accumulation
// ============================================================================

// Apply dirt overlay to surface
// Dirt typically accumulates in crevices and on horizontal surfaces
vec3 applyDirt(
    vec3 baseAlbedo,
    vec3 dirtColor,
    float dirtAmount,
    vec3 normalWS,
    float aoValue          // Ambient occlusion - dirt accumulates in crevices
) {
    // Dirt accumulates more in crevices (low AO areas)
    float creviceFactor = 1.0 - aoValue;

    // And on upward-facing surfaces (gravity)
    float upDot = max(dot(normalWS, vec3(0.0, 1.0, 0.0)), 0.0);
    float gravityFactor = smoothstep(0.0, 0.5, upDot);

    // Combined dirt factor
    float actualDirt = dirtAmount * mix(gravityFactor, creviceFactor, 0.5);

    return mix(baseAlbedo, dirtColor * baseAlbedo, actualDirt);
}

// ============================================================================
// Moss/Vegetation Growth
// ============================================================================

// Apply moss overlay to surface
// Moss grows in shaded, moist areas on north-facing surfaces (in northern hemisphere)
vec3 applyMoss(
    vec3 baseAlbedo,
    vec3 mossColor,
    float mossAmount,
    vec3 normalWS,
    float aoValue,
    float wetness
) {
    // Moss prefers shaded areas (low AO)
    float shadeFactor = 1.0 - aoValue;

    // Moss grows better on moist surfaces
    float moistureFactor = 0.5 + wetness * 0.5;

    // Moss grows on north-facing surfaces (negative Z in most engines)
    // Also grows on horizontal surfaces
    float upDot = dot(normalWS, vec3(0.0, 1.0, 0.0));
    float northDot = -normalWS.z;  // Negative Z = north
    float orientationFactor = max(upDot, northDot * 0.5);
    orientationFactor = smoothstep(0.0, 0.5, orientationFactor);

    // Combined moss factor
    float actualMoss = mossAmount * shadeFactor * moistureFactor * orientationFactor;

    // Moss color blends with base (organic overlay)
    return mix(baseAlbedo, mossColor, actualMoss);
}

// ============================================================================
// Combined Weathering Application
// ============================================================================

// Apply all weathering effects in correct order
// Order: Base -> Dirt -> Moss -> Wetness -> Snow (snow handled separately)
struct WeatheringParams {
    float wetness;
    float wetnessRoughnessScale;
    float dirtAmount;
    vec3 dirtColor;
    float mossAmount;
    vec3 mossColor;
};

struct WeatheredMaterial {
    vec3 albedo;
    float roughness;
    float metallic;
};

WeatheredMaterial applyWeathering(
    vec3 baseAlbedo,
    float baseRoughness,
    float baseMetallic,
    vec3 normalWS,
    float aoValue,
    WeatheringParams params
) {
    WeatheredMaterial result;
    vec3 albedo = baseAlbedo;
    float roughness = baseRoughness;

    // 1. Apply dirt (affects albedo only)
    if (params.dirtAmount > 0.01) {
        albedo = applyDirt(albedo, params.dirtColor, params.dirtAmount, normalWS, aoValue);
    }

    // 2. Apply moss (affects albedo only)
    if (params.mossAmount > 0.01) {
        albedo = applyMoss(albedo, params.mossColor, params.mossAmount, normalWS, aoValue, params.wetness);
    }

    // 3. Apply wetness (affects albedo and roughness)
    if (params.wetness > 0.01) {
        WetnessResult wet = applyWetness(albedo, roughness, baseMetallic, params.wetness);
        albedo = wet.albedo;
        roughness = wet.roughness;
    }

    result.albedo = albedo;
    result.roughness = roughness;
    result.metallic = baseMetallic;  // Weathering doesn't change metallic

    return result;
}

// ============================================================================
// Puddle Detection and Rendering
// ============================================================================

// Check if a point should render as a puddle
// Returns puddle depth (0 = no puddle, >0 = puddle with given depth)
float calculatePuddleDepth(
    vec3 worldPos,
    vec3 normalWS,
    float wetness,
    float puddleThreshold  // Wetness level above which puddles form
) {
    // Puddles only form on near-flat surfaces
    float upDot = dot(normalWS, vec3(0.0, 1.0, 0.0));
    if (upDot < 0.9) {
        return 0.0;
    }

    // Puddles form when wetness exceeds threshold
    if (wetness < puddleThreshold) {
        return 0.0;
    }

    // Depth based on how much wetness exceeds threshold
    float depth = (wetness - puddleThreshold) / (1.0 - puddleThreshold);
    return depth * 0.05;  // Max 5cm puddle depth
}

// Puddle reflection contribution
// Uses screen-space reflection or cubemap fallback
vec3 calculatePuddleReflection(
    vec3 viewDir,
    vec3 normalWS,
    float puddleDepth,
    vec3 skyColor  // Fallback when no SSR available
) {
    if (puddleDepth <= 0.0) {
        return vec3(0.0);
    }

    // Reflect view direction
    vec3 reflectDir = reflect(-viewDir, normalWS);

    // Simple sky reflection (replace with SSR sample in full implementation)
    vec3 reflection = skyColor;

    // Fresnel for puddle surface
    float NdotV = max(dot(normalWS, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 5.0) * 0.9 + 0.1;

    // Blend reflection based on depth and fresnel
    float reflectStrength = puddleDepth * 10.0 * fresnel;  // Scale up for visibility

    return reflection * clamp(reflectStrength, 0.0, 1.0);
}

#endif // WEATHERING_COMMON_GLSL
