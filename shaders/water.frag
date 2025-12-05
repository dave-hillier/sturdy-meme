#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * water.frag - Water surface fragment shader
 * Implements Fresnel reflections, specular highlights, foam, and flow-based animation.
 * Flow map system based on Far Cry 5's water rendering (GDC 2018).
 */

#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"
#include "terrain_height_common.glsl"
#include "flow_common.glsl"
#include "fbm_common.glsl"
#include "foam.glsl"
#include "bindings.glsl"

// Water-specific uniforms
layout(std140, binding = BINDING_WATER_UBO) uniform WaterUniforms {
    // Primary material properties
    vec4 waterColor;           // rgb = base water color, a = transparency
    vec4 waveParams;           // x = amplitude, y = wavelength, z = steepness, w = speed
    vec4 waveParams2;          // Second wave layer parameters
    vec4 waterExtent;          // xy = position offset, zw = size
    vec4 scatteringCoeffs;     // rgb = absorption coefficients, a = turbidity

    // Phase 12: Secondary material for blending
    vec4 waterColor2;          // Secondary water color
    vec4 scatteringCoeffs2;    // Secondary scattering coefficients
    vec4 blendCenter;          // xy = world position, z = blend direction angle, w = unused
    float absorptionScale2;    // Secondary absorption scale
    float scatteringScale2;    // Secondary scattering scale
    float specularRoughness2;  // Secondary specular roughness
    float sssIntensity2;       // Secondary SSS intensity
    float blendDistance;       // Distance over which materials blend (world units)
    int blendMode;             // 0 = distance from center, 1 = directional, 2 = radial

    float waterLevel;          // Y height of water plane
    float foamThreshold;       // Wave height threshold for foam
    float fresnelPower;        // Fresnel reflection power
    float terrainSize;         // Terrain size for UV calculation
    float terrainHeightScale;  // Terrain height scale
    float shoreBlendDistance;  // Distance over which shore fades (world units)
    float shoreFoamWidth;      // Width of shore foam band (world units)
    float flowStrength;        // How much flow affects UV offset (world units)
    float flowSpeed;           // Flow animation speed multiplier
    float flowFoamStrength;    // How much flow speed affects foam
    float fbmNearDistance;     // Distance for max FBM detail (9 octaves)
    float fbmFarDistance;      // Distance for min FBM detail (3 octaves)
    float specularRoughness;   // Base roughness for specular
    float absorptionScale;     // How quickly light is absorbed with depth
    float scatteringScale;     // Turbidity multiplier
    float displacementScale;   // Scale for interactive displacement
    float sssIntensity;        // Phase 17: Subsurface scattering intensity
    float causticsScale;       // Phase 9: Caustics pattern scale
    float causticsSpeed;       // Phase 9: Caustics animation speed
    float causticsIntensity;   // Phase 9: Caustics brightness
    float padding;             // Alignment padding
};

layout(binding = BINDING_WATER_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_WATER_TERRAIN_HEIGHT) uniform sampler2D terrainHeightMap;
layout(binding = BINDING_WATER_FLOW_MAP) uniform sampler2D flowMap;
layout(binding = BINDING_WATER_FOAM_NOISE) uniform sampler2D foamNoiseTexture;
layout(binding = BINDING_WATER_TEMPORAL_FOAM) uniform sampler2D temporalFoamMap;  // Phase 14: Persistent foam
layout(binding = BINDING_WATER_CAUSTICS) uniform sampler2D causticsTexture;  // Phase 9: Underwater light patterns
layout(binding = BINDING_WATER_SSR) uniform sampler2D ssrTexture;       // Phase 10: Screen-Space Reflections
layout(binding = BINDING_WATER_SCENE_DEPTH) uniform sampler2D sceneDepthTexture; // Phase 11: Scene depth for refraction

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragWaveHeight;
layout(location = 4) in float fragJacobian;  // Phase 13: Jacobian for foam detection
layout(location = 5) in float fragWaveSlope; // Phase 17: Wave slope for SSS

layout(location = 0) out vec4 outColor;

// Procedural noise for water detail
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

// =========================================================================
// PHASE 11: Dual Depth Buffer Functions
// Scene depth sampling for refraction and soft edges
// =========================================================================

// Saturate helper (clamp to 0-1)
float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec2 saturate(vec2 x) { return clamp(x, 0.0, 1.0); }
vec3 saturate(vec3 x) { return clamp(x, 0.0, 1.0); }

// Convert hardware depth to linear depth (view space)
float linearizeDepth(float depth, float near, float far) {
    // Vulkan uses [0,1] depth range with reverse-Z typically
    float z = depth;
    return near * far / (far - z * (far - near));
}

// Get scene depth at screen UV
float getSceneDepth(vec2 screenUV, float near, float far) {
    float rawDepth = texture(sceneDepthTexture, screenUV).r;
    return linearizeDepth(rawDepth, near, far);
}

// Calculate soft edge factor for water-geometry intersection
// Returns 0.0 at geometry intersection, 1.0 away from geometry
float calculateSoftEdge(vec2 screenUV, float waterDepth, float softEdgeDistance, float near, float far) {
    float sceneDepth = getSceneDepth(screenUV, near, far);
    float depthDiff = sceneDepth - waterDepth;
    return smoothstep(0.0, softEdgeDistance, depthDiff);
}

// Calculate refraction UV offset based on scene depth
// Deeper water = more refraction distortion
vec2 calculateRefractionOffset(vec2 screenUV, vec3 normal, float waterDepth, float near, float far) {
    float sceneDepth = getSceneDepth(screenUV, near, far);
    float depthDiff = max(0.0, sceneDepth - waterDepth);

    // Scale refraction by depth difference (more distortion through deeper water)
    float refractionStrength = saturate(depthDiff * 0.1) * 0.05;

    // Offset UV based on normal XZ (horizontal distortion)
    return normal.xz * refractionStrength;
}

// Schlick's Fresnel approximation
float fresnelSchlick(float cosTheta, float F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), fresnelPower);
}

// Sample environment reflection (simplified sky reflection)
vec3 sampleEnvironmentReflection(vec3 reflectDir, vec3 sunDir, vec3 sunColor) {
    // Simplified sky color based on reflection direction
    float skyGradient = smoothstep(-0.1, 0.5, reflectDir.y);

    // Day/night sky colors
    float dayAmount = smoothstep(-0.05, 0.2, sunDir.y);
    vec3 daySkyLow = vec3(0.6, 0.7, 0.9);
    vec3 daySkyHigh = vec3(0.3, 0.5, 0.85);
    vec3 nightSky = vec3(0.02, 0.03, 0.08);

    vec3 skyLow = mix(nightSky, daySkyLow, dayAmount);
    vec3 skyHigh = mix(nightSky * 0.5, daySkyHigh, dayAmount);
    vec3 skyColor = mix(skyLow, skyHigh, skyGradient);

    // Add sun reflection (specular highlight from sky)
    float sunDot = max(dot(reflectDir, sunDir), 0.0);
    vec3 sunReflect = sunColor * pow(sunDot, 256.0) * 2.0;  // Tight specular

    return skyColor + sunReflect;
}

// =========================================================================
// PHASE 10: Screen-Space Reflections
// Sample SSR texture and blend with environment fallback
// =========================================================================
vec3 sampleReflection(vec3 reflectDir, vec3 sunDir, vec3 sunColor, vec2 screenUV) {
    // Get environment reflection as fallback
    vec3 envReflection = sampleEnvironmentReflection(reflectDir, sunDir, sunColor);

    // Sample SSR texture
    // SSR texture stores: rgb = reflection color, a = confidence (0 = no hit)
    vec4 ssrSample = texture(ssrTexture, screenUV);
    float ssrConfidence = ssrSample.a;

    // Fade SSR based on reflection angle (SSR works best for grazing angles)
    float angleFade = 1.0 - abs(reflectDir.y);  // Less confident for vertical reflections
    ssrConfidence *= angleFade;

    // Blend SSR with environment based on confidence
    vec3 finalReflection = mix(envReflection, ssrSample.rgb, ssrConfidence);

    return finalReflection;
}

// =========================================================================
// PBR LIGHT TRANSPORT (Phase 8)
// Based on Far Cry 5's scattering coefficient approach
// =========================================================================

// Beer-Lambert law: light absorption through a medium
// absorption: per-channel absorption coefficients (higher = faster absorption)
// depth: path length through the medium
vec3 beerLambertAbsorption(vec3 absorption, float depth) {
    return exp(-absorption * depth);
}

// Calculate water color based on physical light transport
// Using scattering coefficients instead of artist-picked colors
vec3 calculateWaterTransmission(float depth, vec3 absorption, float turbidity, float scatterScale) {
    // Apply Beer-Lambert absorption
    vec3 transmitted = beerLambertAbsorption(absorption, depth);

    // Turbidity causes scattering which adds a milky/hazy appearance
    // Higher turbidity = more light scattered back toward viewer
    float scatter = turbidity * scatterScale * (1.0 - exp(-depth * 0.5));

    // Scattered light tends toward white/gray (all wavelengths equally)
    vec3 scatteredColor = vec3(0.7, 0.75, 0.8) * scatter;

    return transmitted + scatteredColor;
}

// GGX/Trowbridge-Reitz normal distribution function for specular
float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265 * denom * denom;

    return num / max(denom, 0.0001);
}

// Note: geometrySchlickGGX and geometrySmith are provided by lighting_common.glsl

// =========================================================================
// VARIANCE-BASED SPECULAR FILTERING (Phase 6)
// Reduces specular aliasing from high-frequency normal detail
// =========================================================================

// Calculate roughness adjustment based on normal variance
// Higher variance = more roughness to reduce aliasing
float calculateVarianceRoughness(vec3 normal, vec3 meshNormal, float baseRoughness) {
    // Compute variance as deviation from mesh normal
    float normalVariance = 1.0 - max(dot(normal, meshNormal), 0.0);

    // Convert variance to roughness increase
    // This follows the approach from "Filtering Distributions of Normals for Shading Antialiasing"
    float varianceRoughness = sqrt(normalVariance * 0.5);

    // Combine with base roughness (roughness adds in quadrature for Gaussian distributions)
    float combinedRoughness = sqrt(baseRoughness * baseRoughness + varianceRoughness * varianceRoughness);

    return clamp(combinedRoughness, 0.02, 1.0);
}

// =========================================================================
// PHASE 12: Material Blending
// Smooth transitions between different water types
// =========================================================================

// Calculate blend factor based on world position and blend mode
// Returns 0.0 = primary material, 1.0 = secondary material
float calculateMaterialBlendFactor(vec3 worldPos) {
    vec2 pos2D = worldPos.xz;
    vec2 center = blendCenter.xy;
    float blendAngle = blendCenter.z;

    float blendFactor = 0.0;

    if (blendMode == 0) {
        // Distance mode: blend based on distance from center point
        float dist = length(pos2D - center);
        blendFactor = smoothstep(0.0, blendDistance, dist);
    }
    else if (blendMode == 1) {
        // Directional mode: blend along a direction (e.g., river flowing to ocean)
        // The blend angle defines the direction of transition
        vec2 dir = vec2(cos(blendAngle), sin(blendAngle));
        float projDist = dot(pos2D - center, dir);
        blendFactor = smoothstep(-blendDistance * 0.5, blendDistance * 0.5, projDist);
    }
    else if (blendMode == 2) {
        // Radial mode: blend radially outward from center (inverse of distance)
        // 0 = secondary at center, 1 = primary at edge
        float dist = length(pos2D - center);
        blendFactor = 1.0 - smoothstep(0.0, blendDistance, dist);
    }

    return clamp(blendFactor, 0.0, 1.0);
}

// Blended material properties structure
struct BlendedMaterial {
    vec4 color;
    vec3 absorption;
    float turbidity;
    float absorptionScale;
    float scatteringScale;
    float roughness;
    float sss;
};

// Get blended material properties for current fragment
BlendedMaterial getBlendedMaterial(vec3 worldPos) {
    float blend = calculateMaterialBlendFactor(worldPos);

    BlendedMaterial mat;
    mat.color = mix(waterColor, waterColor2, blend);
    mat.absorption = mix(scatteringCoeffs.rgb, scatteringCoeffs2.rgb, blend);
    mat.turbidity = mix(scatteringCoeffs.a, scatteringCoeffs2.a, blend);
    mat.absorptionScale = mix(absorptionScale, absorptionScale2, blend);
    mat.scatteringScale = mix(scatteringScale, scatteringScale2, blend);
    mat.roughness = mix(specularRoughness, specularRoughness2, blend);
    mat.sss = mix(sssIntensity, sssIntensity2, blend);

    return mat;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 moonDir = normalize(ubo.moonDirection.xyz);
    float time = ubo.windDirectionAndSpeed.w;

    // =========================================================================
    // TERRAIN-AWARE SHORE DETECTION
    // =========================================================================
    // Sample terrain height at this fragment's world position
    vec2 terrainUV = worldPosToTerrainUV(fragWorldPos.xz, terrainSize);
    float terrainHeight = 0.0;
    float waterDepth = 100.0;  // Default to deep water if outside terrain
    bool insideTerrain = (terrainUV.x >= 0.0 && terrainUV.x <= 1.0 &&
                          terrainUV.y >= 0.0 && terrainUV.y <= 1.0);

    if (insideTerrain) {
        terrainHeight = sampleTerrainHeight(terrainHeightMap, terrainUV, terrainHeightScale);
        // Water depth = distance from water surface to terrain
        // Positive = underwater terrain, Negative = terrain above water
        waterDepth = fragWorldPos.y - terrainHeight;
    }

    // Discard fragments where terrain is above water (water shouldn't render there)
    if (waterDepth < -0.1) {
        discard;
    }

    // =========================================================================
    // FLOW MAP SAMPLING
    // =========================================================================
    // Sample flow map using world-space UV (flow map covers entire terrain)
    vec2 flowUV = worldPosToTerrainUV(fragWorldPos.xz, terrainSize);
    vec4 flowMapSample = texture(flowMap, flowUV);

    // Calculate flow sample data with two-phase sampling to eliminate pulsing
    FlowSample flowSample = calculateFlowSample(flowMapSample, fragWorldPos.xz * 0.1,
                                                 time * flowSpeed, flowStrength);

    // =========================================================================
    // LOD CALCULATION FOR FBM
    // =========================================================================
    // Calculate view distance for LOD-based FBM octave selection
    // Per Far Cry 5: 9 octaves close, 3 octaves far, never 0 (reflections need detail)
    float viewDistance = length(ubo.cameraPosition.xyz - fragWorldPos);
    float fbmLodFactor = calculateFBMLODFactor(viewDistance, fbmNearDistance, fbmFarDistance);

    // =========================================================================
    // PROCEDURAL NORMAL DETAIL (Flow-Animated, LOD-Aware)
    // =========================================================================
    // Use flow-based UVs for normal detail to make waves follow water flow
    // LOD: 9 octaves close (high detail), 3 octaves far (preserve reflections)
    float detail1_phase0 = fbmLOD(flowSample.uv0 * 0.5 + time * 0.1, fbmLodFactor, 3, 9) * 2.0 - 1.0;
    float detail1_phase1 = fbmLOD(flowSample.uv1 * 0.5 + time * 0.1, fbmLodFactor, 3, 9) * 2.0 - 1.0;
    float detail1 = blendFlowSamples(detail1_phase0, detail1_phase1, flowSample.blend);

    float detail2_phase0 = fbmLOD(flowSample.uv0 * 0.85 - time * 0.08, fbmLodFactor, 3, 9) * 2.0 - 1.0;
    float detail2_phase1 = fbmLOD(flowSample.uv1 * 0.85 - time * 0.08, fbmLodFactor, 3, 9) * 2.0 - 1.0;
    float detail2 = blendFlowSamples(detail2_phase0, detail2_phase1, flowSample.blend);

    // Reduce wave detail in shallow water (calmer near shore)
    float shallowFactor = smoothstep(0.0, shoreFoamWidth * 2.0, waterDepth);
    float detailStrength = mix(0.05, 0.15, shallowFactor);

    // Modulate detail strength by flow speed (faster water = more turbulent)
    detailStrength *= mix(0.7, 1.3, flowSample.speed);

    // Fade detail strength at distance to reduce aliasing
    detailStrength *= mix(1.0, 0.5, fbmLodFactor);

    vec3 detailNormal = normalize(N + vec3(detail1, 0.0, detail2) * detailStrength);
    N = normalize(mix(N, detailNormal, 0.5));

    // =========================================================================
    // FRESNEL & REFLECTION
    // =========================================================================
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = fresnelSchlick(NdotV, 0.02);  // Water F0 ~0.02

    vec3 R = reflect(-V, N);

    // Shadow calculation
    float shadow = calculateCascadedShadow(
        fragWorldPos, N, sunDir,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // =========================================================================
    // PHASE 12: Get blended material properties
    // =========================================================================
    BlendedMaterial mat = getBlendedMaterial(fragWorldPos);

    // =========================================================================
    // PBR WATER COLOR - Beer-Lambert absorption (Phase 8)
    // =========================================================================
    // Get physical scattering properties from blended material
    vec3 absorption = mat.absorption * mat.absorptionScale;
    float turbidity = mat.turbidity;

    // Calculate transmission through water using Beer-Lambert law
    // This replaces artist-picked colors with physically-based light transport
    vec3 waterTransmission = calculateWaterTransmission(waterDepth, absorption, turbidity, mat.scatteringScale);

    // Base water color is the inverse of absorption (what's NOT absorbed)
    // Mix with white for scattered light in turbid water
    vec3 baseColor = mat.color.rgb * waterTransmission;

    // Sun color used for SSS and specular - calculate once
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;

    // =========================================================================
    // PHASE 17: Enhanced Subsurface Scattering (Sea of Thieves inspired)
    // Light transmission through thin wave peaks creates a glowing effect
    // =========================================================================

    // Basic depth-based SSS (existing behavior, kept for deep water contribution)
    float sssDepth = min(waterDepth, 10.0);  // Cap for very deep water
    vec3 depthSSS = vec3(0.0, 0.3, 0.4) * (1.0 - exp(-sssDepth * 0.2)) * (1.0 - turbidity * 0.5);
    baseColor += depthSSS * 0.2;

    // Wave geometry-based SSS - light shining through thin wave peaks
    // When the sun is behind the wave (relative to viewer), light passes through
    // the thin water at wave crests, creating a glowing translucent effect

    // Back-lighting factor: strongest when looking toward the sun
    // dot(sunDir, -V) is positive when sun is behind the water relative to viewer
    float backLighting = max(0.0, dot(sunDir, -V));

    // Wave slope indicates thin water (steep slope = thin wave peak)
    // fragWaveSlope is 0 for flat surface, ~1 for nearly vertical
    float waveSlope = fragWaveSlope;

    // Also use wave height to boost SSS at wave peaks
    float heightFactor = smoothstep(0.0, waveParams.x * 2.0, fragWaveHeight);

    // Combine slope and height for thin-water detection
    float thinWaterFactor = max(waveSlope, heightFactor * 0.5);

    // SSS is strongest where water is thin AND back-lit
    float sssStrength = thinWaterFactor * backLighting * backLighting; // Square for falloff

    // Shallow water enhances SSS visibility
    float shallowBoost = 1.0 - smoothstep(0.0, 5.0, waterDepth);
    sssStrength *= mix(0.5, 1.5, shallowBoost);

    // Turbidity reduces SSS (particles scatter the light before it reaches the eye)
    sssStrength *= (1.0 - turbidity * 0.7);

    // Calculate SSS color - sun-tinted with water's natural color
    // Use a warmer tint at thin peaks for more realistic appearance
    vec3 sssTint = mix(mat.color.rgb, vec3(0.1, 0.5, 0.4), 0.5); // Blue-green tint
    vec3 waveSSS = sssTint * sunColor * sssStrength * mat.sss;

    // Apply enhanced SSS to base color
    baseColor += waveSSS * shadow;

    // =========================================================================
    // PHASE 9: Caustics - Underwater Light Patterns
    // Animated light patterns that appear on underwater surfaces
    // =========================================================================

    // Calculate caustics contribution - strongest in shallow, sunlit water
    vec3 causticsContribution = vec3(0.0);

    if (waterDepth > 0.0 && waterDepth < 20.0) {
        float time = ubo.windDirectionAndSpeed.w;

        // Two-layer caustics animation for richer look
        // Layer 1: Main caustics pattern
        vec2 causticsUV1 = fragWorldPos.xz * causticsScale + vec2(time * causticsSpeed * 0.3, time * causticsSpeed * 0.2);
        float caustic1 = texture(causticsTexture, causticsUV1).r;

        // Layer 2: Secondary pattern at different scale and speed (creates shimmering)
        vec2 causticsUV2 = fragWorldPos.xz * causticsScale * 1.5 - vec2(time * causticsSpeed * 0.2, time * causticsSpeed * 0.35);
        float caustic2 = texture(causticsTexture, causticsUV2).r;

        // Combine layers - multiply for sharper caustic lines
        float causticPattern = caustic1 * caustic2 * 2.0 + (caustic1 + caustic2) * 0.25;
        causticPattern = clamp(causticPattern, 0.0, 1.0);

        // Depth falloff - caustics are most visible in shallow water
        float depthFalloff = 1.0 - smoothstep(0.0, 15.0, waterDepth);

        // Sun angle influence - caustics are stronger with overhead sun
        float sunAngle = max(0.0, sunDir.y);
        float sunInfluence = sunAngle * sunAngle;

        // Combine factors
        float causticStrength = causticPattern * depthFalloff * sunInfluence * causticsIntensity;

        // Caustics are sun-colored light focused by wave refraction
        causticsContribution = sunColor * causticStrength * shadow;

        // Reduce caustics in turbid water (particles scatter the light)
        causticsContribution *= (1.0 - turbidity * 0.8);
    }

    // Add caustics to base color (affects what we see through the water)
    baseColor += causticsContribution;

    // Calculate screen UV for SSR sampling (Phase 10)
    // SSR texture is at half resolution, so multiply texture size by 2 to get screen size
    vec2 ssrTextureSize = vec2(textureSize(ssrTexture, 0));
    vec2 screenSize = ssrTextureSize * 2.0;  // SSR is half resolution
    vec2 screenUV = gl_FragCoord.xy / screenSize;

    // Reflection color from environment + SSR (Phase 10)
    vec3 reflectionColor = sampleReflection(R, sunDir, sunColor, screenUV);

    // Refraction color based on transmitted light
    vec3 refractionColor = baseColor;

    // Blend reflection and refraction based on Fresnel
    vec3 waterSurfaceColor = mix(refractionColor, reflectionColor, fresnel);

    // =========================================================================
    // GGX SPECULAR WITH VARIANCE FILTERING (Phase 6 & 8)
    // =========================================================================
    // Store mesh normal before detail perturbation for variance calculation
    vec3 meshNormal = normalize(fragNormal);

    // Calculate variance-adjusted roughness to reduce specular aliasing
    // Higher normal variance = more roughness = less aliasing
    float adjustedRoughness = calculateVarianceRoughness(N, meshNormal, mat.roughness);

    // Further increase roughness at distance to reduce shimmer
    adjustedRoughness = mix(adjustedRoughness, adjustedRoughness + 0.1, fbmLodFactor);

    // Sun specular using GGX distribution
    vec3 H = normalize(V + sunDir);
    float NdotH = max(dot(N, H), 0.0);
    float NdotL = max(dot(N, sunDir), 0.0);

    // GGX specular with Fresnel and geometry terms
    float D = distributionGGX(NdotH, adjustedRoughness);
    float G = geometrySmith(N, V, sunDir, adjustedRoughness);
    float F = fresnelSchlick(max(dot(H, V), 0.0), 0.02);

    // Cook-Torrance BRDF
    float specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 sunSpecular = sunColor * specular * NdotL * shadow;

    // Moon specular (simpler, less intense)
    vec3 moonH = normalize(V + moonDir);
    float moonNdotH = max(dot(N, moonH), 0.0);
    float moonNdotL = max(dot(N, moonDir), 0.0);
    float moonD = distributionGGX(moonNdotH, adjustedRoughness + 0.1);  // Slightly rougher
    vec3 moonColor = ubo.moonColor.rgb * ubo.moonDirection.w;
    vec3 moonSpecular = moonColor * moonD * moonNdotL * 0.02;  // Much dimmer than sun

    // Diffuse sun lighting on water (very subtle for water)
    vec3 diffuse = baseColor * sunColor * NdotL * shadow * 0.2;

    // Ambient lighting with depth-based darkening
    float depthDarkening = exp(-waterDepth * 0.05);  // Darker in deeper water
    vec3 ambient = baseColor * ubo.ambientColor.rgb * 0.4 * depthDarkening;

    // =========================================================================
    // FOAM - Jacobian + texture-based foam system (Phase 13)
    // Uses Jacobian from vertex shader to detect wave folding (whitecaps)
    // Combined with tileable noise texture for organic appearance
    // Based on Sea of Thieves + Far Cry 5 approaches
    // =========================================================================
    vec3 foamColor = vec3(0.9, 0.95, 1.0);
    float totalFoamAmount = 0.0;

    // Multi-scale foam texture sampling with flow-based UV animation
    // Sample at 3 scales for detail: large clumps, medium bubbles, fine detail
    vec2 flowOffset = flowSample.flowDir * time * 0.05;

    // Large scale foam (clumps) - moves slowly with flow
    vec2 foamUV1 = fragWorldPos.xz * 0.02 + flowOffset * 0.5;
    float foam1 = texture(foamNoiseTexture, foamUV1).r;

    // Medium scale foam (bubbles) - moves with flow
    vec2 foamUV2 = fragWorldPos.xz * 0.08 - flowOffset;
    float foam2 = texture(foamNoiseTexture, foamUV2).r;

    // Fine scale foam (detail) - moves faster, opposite direction for turbulence
    vec2 foamUV3 = fragWorldPos.xz * 0.25 + flowOffset * 2.0;
    float foam3 = texture(foamNoiseTexture, foamUV3).r;

    // Combine scales: large modulates medium, both modulate fine
    float combinedFoamNoise = foam1 * 0.5 + foam2 * 0.35 + foam3 * 0.15;
    // Add some contrast
    combinedFoamNoise = smoothstep(0.2, 0.8, combinedFoamNoise);

    // --- Phase 13: Jacobian-based wave foam (whitecaps) ---
    // Jacobian < 0 means wave surface is folding over itself
    // Jacobian = 0 is the threshold where folding begins
    // We use foamThreshold as a bias to control foam amount (negative = more foam)
    float jacobianBias = -foamThreshold;  // Convert threshold to bias (0.1 threshold -> -0.1 bias)
    float jacobianFoam = smoothstep(jacobianBias, jacobianBias + 0.3, -fragJacobian);

    // Also add foam based on wave height for steep waves that haven't folded yet
    float heightFoam = smoothstep(foamThreshold * 2.0, foamThreshold * 4.0, fragWaveHeight);

    // --- Phase 14: Sample temporal foam buffer for persistent foam ---
    // The foam buffer accumulates foam over time and blurs it for natural dissipation
    vec2 temporalFoamUV = (fragWorldPos.xz - waterExtent.xy) / waterExtent.zw + 0.5;
    temporalFoamUV = clamp(temporalFoamUV, 0.0, 1.0);
    float temporalFoam = texture(temporalFoamMap, temporalFoamUV).r;

    // Combine Jacobian foam with temporal persistence
    // Fresh Jacobian foam is sharp, temporal foam is the accumulated/blurred version
    float waveFoamAmount = max(jacobianFoam, max(heightFoam * 0.5, temporalFoam));
    // Modulate by foam texture for organic wave caps
    waveFoamAmount *= smoothstep(0.3, 0.7, combinedFoamNoise);

    // --- Flow-based foam (fast water generates foam) ---
    float flowFoamAmount = 0.0;
    if (flowSample.speed > 0.2) {
        // Flow turbulence foam
        flowFoamAmount = smoothstep(0.3, 0.8, flowSample.speed) * flowFoamStrength;
        // Use medium + fine scale for turbulent look
        float turbulenceNoise = foam2 * 0.6 + foam3 * 0.4;
        flowFoamAmount *= smoothstep(0.25, 0.6, turbulenceNoise);

        // Extra foam where flow meets obstacles
        float obstacleProximity = 1.0 - smoothstep(0.0, 0.2, flowSample.shoreDist);
        float obstacleFoam = obstacleProximity * flowSample.speed * 0.8;
        obstacleFoam *= smoothstep(0.2, 0.5, foam3); // Fine detail for splashing
        flowFoamAmount = max(flowFoamAmount, obstacleFoam);
    }

    // --- Phase 15: Intersection Foam (shore + geometry) ---
    // Enhanced shore foam with better intersection detection and flow advection
    float shoreFoamAmount = 0.0;
    if (insideTerrain && waterDepth > 0.0 && waterDepth < shoreFoamWidth) {
        float fw = shoreFoamWidth;

        // Wave-modulated intersection detection
        // Waves push and pull the waterline, creating dynamic foam patterns
        float waveInfluence = sin(fragWaveHeight * 10.0 + time * 2.0) * 0.3 + 0.7;
        float effectiveDepth = waterDepth / waveInfluence;

        // Create foam bands at different depths - more bands for richer look
        float band1 = smoothstep(0.0, fw * 0.15, effectiveDepth) * smoothstep(fw * 0.35, fw * 0.1, effectiveDepth);
        float band2 = smoothstep(fw * 0.25, fw * 0.4, effectiveDepth) * smoothstep(fw * 0.6, fw * 0.4, effectiveDepth);
        float band3 = smoothstep(fw * 0.5, fw * 0.7, effectiveDepth) * smoothstep(fw, fw * 0.7, effectiveDepth);

        // Flow-advected foam UVs for intersection foam
        // Foam appears at intersection and flows away with water
        vec2 advectedUV = fragWorldPos.xz * 0.1 + flowSample.flowDir * time * 0.3;
        float advectedNoise = texture(foamNoiseTexture, advectedUV * 0.5).r;

        // Modulate bands with foam texture for organic cellular look
        shoreFoamAmount = band1 * smoothstep(0.3, 0.7, foam1);
        shoreFoamAmount += band2 * smoothstep(0.35, 0.65, foam2) * 0.7;
        shoreFoamAmount += band3 * smoothstep(0.4, 0.6, combinedFoamNoise) * 0.4;

        // Strong foam right at the waterline with wave dynamics
        float waterlineIntensity = smoothstep(1.0, 0.0, effectiveDepth);
        // Add advected noise for foam that appears to flow away from the shore
        float dynamicWaterline = waterlineIntensity * mix(foam2, advectedNoise, 0.5);
        shoreFoamAmount = max(shoreFoamAmount, dynamicWaterline);

        // Intersection foam boost - extra foam very close to geometry
        float intersectionStrength = smoothstep(0.5, 0.0, waterDepth);
        float intersectionFoam = intersectionStrength * advectedNoise * 1.5;
        shoreFoamAmount = max(shoreFoamAmount, intersectionFoam);

        // Boost shore foam in fast-flowing areas (rapids effect)
        shoreFoamAmount *= mix(1.0, 1.8, flowSample.speed);

        // Add splashing effect based on wave height at shoreline
        float splashFactor = smoothstep(0.0, 0.05, fragWaveHeight) * intersectionStrength;
        float splashFoam = splashFactor * foam3 * 0.8;
        shoreFoamAmount = max(shoreFoamAmount, splashFoam);
    }

    // =========================================================================
    // PHASE 11: Scene Depth-Based Intersection Foam
    // Detect intersections with ANY geometry using scene depth buffer
    // This catches objects like rocks, boats, docks that terrain check misses
    // =========================================================================
    {
        // Calculate linear depth of water surface
        vec4 clipPos = ubo.proj * ubo.view * vec4(fragWorldPos, 1.0);
        float waterLinearDepth = linearizeDepth(clipPos.z / clipPos.w * 0.5 + 0.5, 0.1, 1000.0);

        // Get scene depth and calculate soft edge
        float sceneLinearDepth = getSceneDepth(screenUV, 0.1, 1000.0);
        float depthDiff = sceneLinearDepth - waterLinearDepth;

        // Soft edge factor: 0 at intersection, 1 away from geometry
        float softEdgeDist = 0.5;  // World units for soft transition
        float softEdge = smoothstep(0.0, softEdgeDist, depthDiff);

        // Add foam at intersections with any geometry
        if (depthDiff < softEdgeDist && depthDiff > -0.1) {
            float intersectionFactor = 1.0 - softEdge;

            // Sample foam texture at intersection
            vec2 intersectionUV = fragWorldPos.xz * 0.15 + time * 0.05;
            float intersectionNoise = texture(foamNoiseTexture, intersectionUV).r;

            // Scale foam by intersection proximity
            float geometryFoam = intersectionFactor * intersectionNoise * 0.8;

            // Add to shore foam (if inside terrain) or directly to total
            if (insideTerrain) {
                shoreFoamAmount = max(shoreFoamAmount, geometryFoam);
            } else {
                // For non-terrain geometry intersections
                flowFoamAmount = max(flowFoamAmount, geometryFoam);
            }
        }

        // Use soft edge to fade water alpha at intersections (soft particles effect)
        // This creates a smoother blend where water meets geometry
        // Store for later use in alpha blending
    }

    // Combine all foam sources
    totalFoamAmount = max(max(waveFoamAmount, shoreFoamAmount), flowFoamAmount);
    totalFoamAmount = clamp(totalFoamAmount, 0.0, 1.0);

    // Foam color varies slightly with depth
    foamColor = calculateFoamColor(totalFoamAmount, waterDepth, mat.color.rgb);

    // =========================================================================
    // COMBINE LIGHTING
    // =========================================================================
    vec3 finalColor = waterSurfaceColor + sunSpecular + moonSpecular + diffuse + ambient;
    finalColor = mix(finalColor, foamColor, totalFoamAmount * 0.85);

    // Apply aerial perspective (atmospheric scattering)
    // Note: viewDistance was already calculated for FBM LOD
    vec3 viewDir = normalize(fragWorldPos - ubo.cameraPosition.xyz);
    finalColor = applyAerialPerspective(finalColor, ubo.cameraPosition.xyz,
                                         viewDir, viewDistance,
                                         sunDir, sunColor);

    // =========================================================================
    // ALPHA - soft shore edges + foam opacity
    // =========================================================================
    float baseAlpha = mat.color.a;

    // Soft edge near shore (fade out as water gets very shallow)
    float shoreAlpha = smoothstep(0.0, shoreBlendDistance, waterDepth);

    // Foam makes water more opaque
    float foamOpacity = mix(baseAlpha, 1.0, totalFoamAmount);

    // Final alpha combines shore fade and foam
    float alpha = shoreAlpha * foamOpacity;

    // DEBUG: Visualize water depth as color gradient
    // Uncomment to debug terrain sampling:
    // Red = shallow (0-5m), Green = medium (5-20m), Blue = deep (20m+)
    /*
    vec3 debugColor = vec3(0.0);
    debugColor.r = 1.0 - smoothstep(0.0, 5.0, waterDepth);
    debugColor.g = smoothstep(0.0, 5.0, waterDepth) * (1.0 - smoothstep(5.0, 20.0, waterDepth));
    debugColor.b = smoothstep(5.0, 20.0, waterDepth);
    outColor = vec4(debugColor, 1.0);
    return;
    */

    outColor = vec4(finalColor, alpha);
}
