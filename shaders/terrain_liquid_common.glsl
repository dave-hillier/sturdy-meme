// Terrain Liquid Effects - puddles, wet areas, and surface water
// Part of the composable material system
// Enables water effects on terrain without separate geometry
#ifndef TERRAIN_LIQUID_COMMON_GLSL
#define TERRAIN_LIQUID_COMMON_GLSL

#include "weathering_common.glsl"

// ============================================================================
// Puddle Detection and Rendering
// ============================================================================

// Puddle parameters
struct PuddleParams {
    float depth;            // Puddle water depth
    float roughness;        // Water surface roughness
    vec3 waterColor;        // Tint color
    float reflectivity;     // Base reflectivity
    float rippleStrength;   // Rain ripple intensity
    float edgeSoftness;     // Edge blend distance
};

// Calculate puddle presence based on terrain conditions
// Returns puddle depth (0 = no puddle, >0 = puddle)
float calculatePuddlePresence(
    vec3 worldPos,
    vec3 normalWS,
    float wetness,          // Global wetness from weather
    float heightVariation,  // Local height noise (from texture)
    float puddleThreshold,  // Wetness level for puddles to form
    float maxPuddleDepth    // Maximum puddle depth
) {
    // Puddles only form on near-flat surfaces
    float upDot = dot(normalWS, vec3(0.0, 1.0, 0.0));
    if (upDot < 0.85) {
        return 0.0;  // Too steep for puddles
    }

    // Puddles form in depressions (low height variation)
    float depressionFactor = 1.0 - smoothstep(0.0, 0.3, heightVariation);

    // Wetness must exceed threshold
    float wetnessExcess = max(0.0, wetness - puddleThreshold);
    float wetnessFactor = smoothstep(0.0, 1.0 - puddleThreshold, wetnessExcess);

    // Combined puddle depth
    float depth = wetnessFactor * depressionFactor * maxPuddleDepth;

    // Flatness bonus
    float flatBonus = smoothstep(0.85, 0.98, upDot);
    depth *= (0.5 + 0.5 * flatBonus);

    return depth;
}

// Apply puddle appearance to terrain material
// Modifies albedo, roughness, and adds reflection
struct PuddleResult {
    vec3 albedo;
    float roughness;
    float metallic;
    vec3 reflection;
    float reflectionStrength;
};

PuddleResult applyPuddleEffect(
    vec3 baseAlbedo,
    float baseRoughness,
    float baseMetallic,
    float puddleDepth,
    PuddleParams params,
    vec3 viewDir,
    vec3 normalWS,
    vec3 skyColor        // For reflection fallback
) {
    PuddleResult result;

    if (puddleDepth <= 0.0) {
        result.albedo = baseAlbedo;
        result.roughness = baseRoughness;
        result.metallic = baseMetallic;
        result.reflection = vec3(0.0);
        result.reflectionStrength = 0.0;
        return result;
    }

    // Blend factor based on depth
    float blend = smoothstep(0.0, params.edgeSoftness, puddleDepth);

    // Darken albedo under water (absorption)
    vec3 waterTintedAlbedo = baseAlbedo * params.waterColor;
    float absorption = 1.0 - exp(-puddleDepth * 5.0);
    result.albedo = mix(baseAlbedo, waterTintedAlbedo, absorption * blend);

    // Smooth water surface
    result.roughness = mix(baseRoughness, params.roughness, blend);

    // Water isn't metallic
    result.metallic = mix(baseMetallic, 0.0, blend);

    // Fresnel reflection
    float NdotV = max(dot(normalWS, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 5.0) * 0.9 + 0.1;
    result.reflectionStrength = fresnel * params.reflectivity * blend;

    // Simple sky reflection (replace with SSR in full implementation)
    vec3 reflectDir = reflect(-viewDir, normalWS);
    result.reflection = skyColor * (0.5 + 0.5 * reflectDir.y);

    return result;
}

// ============================================================================
// Rain Ripples for Puddles
// ============================================================================

// Generate animated rain ripple normal perturbation
vec3 calculateRainRipples(
    vec2 worldXZ,
    float time,
    float rippleScale,
    float rippleSpeed,
    int numRipples
) {
    vec3 rippleNormal = vec3(0.0, 1.0, 0.0);

    for (int i = 0; i < numRipples; i++) {
        // Pseudo-random ripple position
        float fi = float(i);
        vec2 rippleCenter = vec2(
            fract(sin(fi * 127.1) * 43758.5453),
            fract(sin(fi * 269.5) * 43758.5453)
        ) * rippleScale;

        // Ripple timing offset
        float timeOffset = fract(sin(fi * 311.7) * 43758.5453);
        float t = fract(time * rippleSpeed + timeOffset);

        // Ripple radius expands over time
        float maxRadius = rippleScale * 0.3;
        float radius = t * maxRadius;

        // Distance from ripple center
        vec2 toPixel = fract(worldXZ / rippleScale) * rippleScale - rippleCenter;
        float dist = length(toPixel);

        // Ring wave
        float wave = sin((dist - radius) * 30.0) * exp(-dist * 5.0) * (1.0 - t);

        // Accumulate normal perturbation
        if (dist > 0.001) {
            vec2 dir = toPixel / dist;
            rippleNormal.xz += dir * wave * 0.1;
        }
    }

    return normalize(rippleNormal);
}

// ============================================================================
// Wet Surface Effect (not puddles, just damp)
// ============================================================================

// Apply wet surface appearance without standing water
void applyWetSurface(
    inout vec3 albedo,
    inout float roughness,
    float wetness
) {
    if (wetness <= 0.0) return;

    // Darken albedo
    float darken = mix(1.0, 0.6, wetness);
    albedo *= darken;

    // Reduce roughness (wet surfaces are shinier)
    roughness = mix(roughness, roughness * 0.3, wetness);
}

// ============================================================================
// Stream/River on Terrain
// ============================================================================

// Parameters for flowing water on terrain
struct TerrainStreamParams {
    vec2 flowDirection;     // Normalized flow direction
    float flowSpeed;        // Animation speed
    float flowWidth;        // Stream width
    float depth;            // Water depth
    vec3 waterColor;        // Stream color
    float foamIntensity;    // White water intensity
    float turbulence;       // Surface roughness from turbulence
};

// Calculate stream presence from distance field or mask
float calculateStreamPresence(
    vec3 worldPos,
    float streamMask,       // 0 = no stream, 1 = center of stream
    float streamWidth
) {
    // The mask value indicates proximity to stream center
    // Convert to depth-like value with soft edges
    float presence = smoothstep(0.0, 0.5, streamMask);
    return presence;
}

// Apply flowing stream effect
void applyStreamEffect(
    inout vec3 albedo,
    inout float roughness,
    inout vec3 normal,
    float streamPresence,
    TerrainStreamParams params,
    vec3 worldPos,
    float time
) {
    if (streamPresence <= 0.0) return;

    float blend = streamPresence;

    // Flow animation - offset UVs
    vec2 flowOffset = params.flowDirection * time * params.flowSpeed;

    // Animated normal perturbation (simplified)
    float wavePhase = dot(worldPos.xz + flowOffset, params.flowDirection) * 10.0;
    float wave = sin(wavePhase) * 0.5 + 0.5;

    // Darken and tint for water
    float absorption = 1.0 - exp(-params.depth * 3.0);
    albedo = mix(albedo, albedo * params.waterColor, absorption * blend);

    // Water surface roughness (affected by turbulence)
    float waterRoughness = 0.05 + params.turbulence * 0.1;
    roughness = mix(roughness, waterRoughness, blend);

    // Perturb normal for waves
    vec3 waveNormal = normalize(vec3(
        sin(wavePhase) * 0.1 * params.turbulence,
        1.0,
        cos(wavePhase * 1.3) * 0.1 * params.turbulence
    ));
    normal = normalize(mix(normal, waveNormal, blend * 0.5));
}

// ============================================================================
// Shore Wetness Gradient
// ============================================================================

// Apply graduated wetness near water bodies
void applyShoreWetness(
    inout vec3 albedo,
    inout float roughness,
    float distanceToWater,  // Horizontal distance to water edge
    float heightAboveWater, // Vertical distance above water level
    float wetnessRange,     // How far wetness extends
    float waveHeight        // For splash zone variation
) {
    // Combine horizontal and vertical distance
    float effectiveDistance = length(vec2(distanceToWater, heightAboveWater));

    // Wetness decreases with distance
    float wetness = 1.0 - smoothstep(0.0, wetnessRange, effectiveDistance);

    // Splash zone near water edge (more wet, more variable)
    float splashZone = 1.0 - smoothstep(0.0, waveHeight * 2.0, effectiveDistance);
    wetness = max(wetness, splashZone * 0.8);

    // Apply wet surface effect
    applyWetSurface(albedo, roughness, wetness);
}

// ============================================================================
// Combined Terrain Liquid Application
// ============================================================================

// Apply all liquid effects to terrain fragment
struct TerrainLiquidResult {
    vec3 albedo;
    vec3 normal;
    float roughness;
    float metallic;
    vec3 reflection;
    float reflectionStrength;
    float waterDepth;       // For caustics, etc.
};

TerrainLiquidResult applyTerrainLiquidEffects(
    vec3 baseAlbedo,
    vec3 baseNormal,
    float baseRoughness,
    float baseMetallic,
    vec3 worldPos,
    float wetness,
    float puddleMask,       // From height variation or explicit mask
    float streamMask,       // 0-1 stream presence
    TerrainStreamParams streamParams,
    PuddleParams puddleParams,
    vec3 viewDir,
    vec3 skyColor,
    float time
) {
    TerrainLiquidResult result;
    result.albedo = baseAlbedo;
    result.normal = baseNormal;
    result.roughness = baseRoughness;
    result.metallic = baseMetallic;
    result.reflection = vec3(0.0);
    result.reflectionStrength = 0.0;
    result.waterDepth = 0.0;

    // 1. Apply base wetness (affects all wet surfaces)
    applyWetSurface(result.albedo, result.roughness, wetness * 0.5);

    // 2. Apply stream effect if present
    if (streamMask > 0.01) {
        float streamPresence = calculateStreamPresence(worldPos, streamMask, streamParams.flowWidth);
        applyStreamEffect(
            result.albedo, result.roughness, result.normal,
            streamPresence, streamParams, worldPos, time
        );
        result.waterDepth = max(result.waterDepth, streamPresence * streamParams.depth);
    }

    // 3. Apply puddle effect (highest priority for reflection)
    float puddleDepth = calculatePuddlePresence(
        worldPos, baseNormal, wetness, puddleMask,
        puddleParams.depth > 0.0 ? 0.5 : 1.0,  // threshold
        puddleParams.depth
    );

    if (puddleDepth > 0.0) {
        PuddleResult puddle = applyPuddleEffect(
            result.albedo, result.roughness, result.metallic,
            puddleDepth, puddleParams, viewDir, baseNormal, skyColor
        );
        result.albedo = puddle.albedo;
        result.roughness = puddle.roughness;
        result.metallic = puddle.metallic;
        result.reflection = puddle.reflection;
        result.reflectionStrength = puddle.reflectionStrength;
        result.waterDepth = max(result.waterDepth, puddleDepth);

        // Add rain ripples to normal if raining
        if (wetness > 0.3 && puddleParams.rippleStrength > 0.0) {
            vec3 rippleNormal = calculateRainRipples(
                worldPos.xz, time, 2.0, 0.5, 8
            );
            result.normal = normalize(mix(
                result.normal, rippleNormal,
                puddleDepth * puddleParams.rippleStrength
            ));
        }
    }

    return result;
}

#endif // TERRAIN_LIQUID_COMMON_GLSL
