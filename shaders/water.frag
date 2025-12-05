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

// Water-specific uniforms
layout(std140, binding = 1) uniform WaterUniforms {
    vec4 waterColor;           // rgb = base water color, a = transparency
    vec4 waveParams;           // x = amplitude, y = wavelength, z = steepness, w = speed
    vec4 waveParams2;          // Second wave layer parameters
    vec4 waterExtent;          // xy = position offset, zw = size
    vec4 scatteringCoeffs;     // rgb = absorption coefficients, a = turbidity
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
    float padding;
};

layout(binding = 2) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = 3) uniform sampler2D terrainHeightMap;
layout(binding = 4) uniform sampler2D flowMap;
layout(binding = 6) uniform sampler2D foamNoiseTexture;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragWaveHeight;

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

// Schlick's Fresnel approximation
float fresnelSchlick(float cosTheta, float F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), fresnelPower);
}

// Sample environment reflection (simplified sky reflection)
vec3 sampleReflection(vec3 reflectDir, vec3 sunDir, vec3 sunColor) {
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
    // PBR WATER COLOR - Beer-Lambert absorption (Phase 8)
    // =========================================================================
    // Get physical scattering properties from uniforms
    vec3 absorption = scatteringCoeffs.rgb * absorptionScale;
    float turbidity = scatteringCoeffs.a;

    // Calculate transmission through water using Beer-Lambert law
    // This replaces artist-picked colors with physically-based light transport
    vec3 waterTransmission = calculateWaterTransmission(waterDepth, absorption, turbidity, scatteringScale);

    // Base water color is the inverse of absorption (what's NOT absorbed)
    // Mix with white for scattered light in turbid water
    vec3 baseColor = waterColor.rgb * waterTransmission;

    // Add subsurface scattering contribution - light that bounces back
    float sssDepth = min(waterDepth, 10.0);  // Cap for very deep water
    vec3 sssColor = vec3(0.0, 0.3, 0.4) * (1.0 - exp(-sssDepth * 0.2)) * (1.0 - turbidity * 0.5);
    baseColor += sssColor * 0.3;

    // Reflection color from environment
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 reflectionColor = sampleReflection(R, sunDir, sunColor);

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
    float adjustedRoughness = calculateVarianceRoughness(N, meshNormal, specularRoughness);

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
    // FOAM - Texture-based foam system with flow animation
    // Uses tileable Worley noise texture sampled at multiple scales
    // Based on Far Cry 5 approach: "noise texture modulated by noise texture"
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

    // --- Wave peak foam ---
    float waveFoamAmount = smoothstep(foamThreshold * 0.7, foamThreshold, fragWaveHeight);
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

    // --- Shore foam (depth-based) ---
    float shoreFoamAmount = 0.0;
    if (insideTerrain && waterDepth > 0.0 && waterDepth < shoreFoamWidth) {
        // Create foam bands at different depths
        float fw = shoreFoamWidth;
        float band1 = smoothstep(0.0, fw * 0.15, waterDepth) * smoothstep(fw * 0.35, fw * 0.1, waterDepth);
        float band2 = smoothstep(fw * 0.25, fw * 0.4, waterDepth) * smoothstep(fw * 0.6, fw * 0.4, waterDepth);
        float band3 = smoothstep(fw * 0.5, fw * 0.7, waterDepth) * smoothstep(fw, fw * 0.7, waterDepth);

        // Modulate bands with foam texture for organic cellular look
        shoreFoamAmount = band1 * smoothstep(0.3, 0.7, foam1);
        shoreFoamAmount += band2 * smoothstep(0.35, 0.65, foam2) * 0.7;
        shoreFoamAmount += band3 * smoothstep(0.4, 0.6, combinedFoamNoise) * 0.4;

        // Strong foam right at the waterline
        float waterlineIntensity = smoothstep(1.0, 0.0, waterDepth);
        shoreFoamAmount = max(shoreFoamAmount, waterlineIntensity * foam2);

        // Boost shore foam in fast-flowing areas (rapids effect)
        shoreFoamAmount *= mix(1.0, 1.5, flowSample.speed);
    }

    // Combine all foam sources
    totalFoamAmount = max(max(waveFoamAmount, shoreFoamAmount), flowFoamAmount);
    totalFoamAmount = clamp(totalFoamAmount, 0.0, 1.0);

    // Foam color varies slightly with depth
    foamColor = calculateFoamColor(totalFoamAmount, waterDepth, waterColor.rgb);

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
    float baseAlpha = waterColor.a;

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
