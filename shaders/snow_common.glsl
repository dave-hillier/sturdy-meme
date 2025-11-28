// Snow layer common functions
// Shared across terrain, vegetation, and mesh shaders
#ifndef SNOW_COMMON_GLSL
#define SNOW_COMMON_GLSL

// GLSL doesn't have saturate like HLSL - define it
#ifndef saturate
#define saturate(x) clamp(x, 0.0, 1.0)
#endif

// Snow material properties
const vec3 SNOW_UP_DIR = vec3(0.0, 1.0, 0.0);
const float SNOW_ROUGHNESS_DEFAULT = 0.7;
const float SNOW_METALLIC = 0.0;

// SSS parameters for snow glow
const float SNOW_SSS_WRAP = 0.4;
const float SNOW_BACKSCATTER_STRENGTH = 0.3;

// Volumetric snow cascade parameters
const uint NUM_SNOW_CASCADES = 3;
const float MAX_SNOW_HEIGHT_M = 0.2;  // Maximum snow height in meters

// Cascade coverage distances (should match VolumetricSnowSystem.h)
const float SNOW_CASCADE_COVERAGE_0 = 256.0;   // Near cascade
const float SNOW_CASCADE_COVERAGE_1 = 1024.0;  // Mid cascade
const float SNOW_CASCADE_COVERAGE_2 = 4096.0;  // Far cascade

// POM parameters for close-up snow detail
const float POM_HEIGHT_SCALE = 0.1;       // Visual height exaggeration for POM
const int POM_MIN_SAMPLES = 8;
const int POM_MAX_SAMPLES = 32;
const float POM_MAX_DISTANCE = 20.0;      // Distance beyond which POM is disabled

// ============================================================================
// Snow Coverage Calculation
// ============================================================================

// Calculate base snow coverage based on slope and world mask
// snowAmount: global snow intensity (0-1)
// snowMaskCoverage: sampled from world-space snow mask texture (0-1)
// normalWS: world-space surface normal
// Returns: coverage factor (0-1)
float calculateSnowCoverage(float snowAmount, float snowMaskCoverage, vec3 normalWS) {
    // Slope factor - more snow on flat surfaces, less on steep
    float upDot = max(dot(normalWS, SNOW_UP_DIR), 0.0);
    // Steeper falloff for vertical surfaces
    float slopeFactor = smoothstep(0.2, 0.7, upDot);

    // Combine global amount, mask, and slope
    float coverage = snowAmount * snowMaskCoverage * slopeFactor;

    return saturate(coverage);
}

// Calculate snow coverage with world-space noise variation
// Adds natural variation to avoid flat, uniform snow blankets
float calculateSnowCoverageWithNoise(float snowAmount, float snowMaskCoverage, vec3 normalWS,
                                      vec3 worldPos, sampler2D noiseTex, float noiseScale) {
    float baseCoverage = calculateSnowCoverage(snowAmount, snowMaskCoverage, normalWS);

    // Sample world-space noise for variation
    float noise = texture(noiseTex, worldPos.xz * noiseScale).r;
    // Map noise from 0-1 to 0.8-1.2 range for subtle variation
    float noiseFactor = mix(0.8, 1.2, noise);

    return saturate(baseCoverage * noiseFactor);
}

// ============================================================================
// Snow Texture Sampling (World-Space)
// ============================================================================

// Sample snow albedo in world space
vec3 sampleSnowAlbedo(sampler2D snowAlbedoTex, vec3 worldPos, float texScale, vec3 snowTint) {
    vec2 snowUV = worldPos.xz * texScale;
    vec3 albedo = texture(snowAlbedoTex, snowUV).rgb;
    return albedo * snowTint;
}

// Sample snow normal in world space (returns world-space normal)
vec3 sampleSnowNormalWS(sampler2D snowNormalTex, vec3 worldPos, float texScale) {
    vec2 snowUV = worldPos.xz * texScale;
    vec3 normalTS = texture(snowNormalTex, snowUV).rgb * 2.0 - 1.0;

    // For top-down snow, tangent space aligns with world XZ
    // T = (1,0,0), B = (0,0,1), N = (0,1,0)
    vec3 normalWS = normalize(vec3(normalTS.x, normalTS.z, normalTS.y));
    return normalWS;
}

// ============================================================================
// Snow Layer Blending
// ============================================================================

// Blend base material with snow layer
// Uses coverage as blend factor
struct SnowBlendResult {
    vec3 albedo;
    vec3 normal;
    float roughness;
    float metallic;
};

SnowBlendResult blendSnowLayer(vec3 baseAlbedo, vec3 baseNormal, float baseRoughness, float baseMetallic,
                                vec3 snowAlbedo, vec3 snowNormal, float snowRoughness,
                                float coverage) {
    SnowBlendResult result;

    // Linear blend for albedo
    result.albedo = mix(baseAlbedo, snowAlbedo, coverage);

    // Slerp-like blend for normals (simplified)
    result.normal = normalize(mix(baseNormal, snowNormal, coverage));

    // Linear blend for roughness (snow is usually mid-rough)
    result.roughness = mix(baseRoughness, snowRoughness, coverage);

    // Snow is non-metallic
    result.metallic = mix(baseMetallic, SNOW_METALLIC, coverage);

    return result;
}

// Simple albedo-only snow blend (for lightweight integration)
vec3 blendSnowAlbedo(vec3 baseAlbedo, vec3 snowColor, float coverage) {
    return mix(baseAlbedo, snowColor, coverage);
}

// ============================================================================
// Snow SSS Lighting (Optional "Snow Glow")
// ============================================================================

// Wrapped diffuse for snow (light wraps around terminator)
float snowWrappedDiffuse(vec3 N, vec3 L) {
    float NdotL = dot(N, L);
    float wrap = SNOW_SSS_WRAP;
    return saturate((NdotL + wrap) / (1.0 + wrap));
}

// Backscatter term (glow when light is behind snow surface)
float snowBackscatter(vec3 N, vec3 L, vec3 V) {
    float back = saturate(dot(-L, N));
    float viewGrazing = saturate(1.0 - dot(N, V));
    return back * viewGrazing * SNOW_BACKSCATTER_STRENGTH;
}

// Calculate snow-specific lighting with SSS effects
// coverage acts as thickness proxy for SSS intensity
vec3 calculateSnowLighting(vec3 snowAlbedo, vec3 N, vec3 L, vec3 V, vec3 lightColor,
                           float lightIntensity, float coverage) {
    // Wrapped diffuse
    float NdotL_wrap = snowWrappedDiffuse(N, L);
    vec3 diffuse = snowAlbedo * NdotL_wrap;

    // Backscatter glow
    float backIntensity = snowBackscatter(N, L, V);
    vec3 backGlow = snowAlbedo * backIntensity;

    // Combine with coverage-based SSS intensity
    vec3 snowLighting = diffuse + backGlow * coverage;

    return snowLighting * lightColor * lightIntensity;
}

// ============================================================================
// Snow Mask UV Calculation
// ============================================================================

// Convert world position to snow mask UV coordinates
vec2 worldToSnowMaskUV(vec3 worldPos, vec2 snowMaskOrigin, float snowMaskSize) {
    return (worldPos.xz - snowMaskOrigin) / snowMaskSize;
}

// Sample snow mask coverage at world position
float sampleSnowMask(sampler2D snowMaskTex, vec3 worldPos, vec2 snowMaskOrigin, float snowMaskSize) {
    vec2 uv = worldToSnowMaskUV(worldPos, snowMaskOrigin, snowMaskSize);

    // Check bounds - no snow outside mask coverage area
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 0.0;
    }

    return texture(snowMaskTex, uv).r;
}

// ============================================================================
// Vegetation-Specific Snow Functions
// ============================================================================

// Calculate snow coverage for vegetation (grass, leaves, etc.)
// Uses vertex color or height to modulate snow affinity
// snowAffinity: per-vertex value (0 at base, 1 at tips/leaves)
float calculateVegetationSnowCoverage(float snowAmount, float snowMaskCoverage, vec3 normalWS,
                                       float snowAffinity) {
    float baseCoverage = calculateSnowCoverage(snowAmount, snowMaskCoverage, normalWS);

    // Vegetation tips/leaves catch more snow
    return saturate(baseCoverage * snowAffinity);
}

// Desaturate and brighten vegetation color for snow coverage
vec3 snowyVegetationColor(vec3 baseColor, vec3 snowColor, float coverage) {
    // Partially desaturate vegetation under snow
    float lum = dot(baseColor, vec3(0.299, 0.587, 0.114));
    vec3 desaturated = mix(baseColor, vec3(lum), coverage * 0.5);

    // Blend with snow color
    return mix(desaturated, snowColor, coverage);
}

// ============================================================================
// Volumetric Snow Cascade Functions
// ============================================================================

// Convert world position to cascade UV coordinates
vec2 worldToSnowCascadeUV(vec3 worldPos, vec4 cascadeParams) {
    // cascadeParams: xy = origin, z = size, w = texel size
    return (worldPos.xz - cascadeParams.xy) / cascadeParams.z;
}

// Check if UV is valid (inside cascade bounds)
bool isValidCascadeUV(vec2 uv) {
    return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
}

// Sample snow height from a single cascade
float sampleSnowCascadeHeight(sampler2D cascadeTex, vec3 worldPos, vec4 cascadeParams) {
    vec2 uv = worldToSnowCascadeUV(worldPos, cascadeParams);
    if (!isValidCascadeUV(uv)) {
        return 0.0;
    }
    return texture(cascadeTex, uv).r;
}

// Select appropriate cascade based on distance from camera
// Returns cascade index (0, 1, or 2) and blend factor to next cascade
void selectSnowCascade(float distToCamera, out uint cascadeIdx, out float blendFactor) {
    // Cascade transition zones (overlap for smooth blending)
    const float TRANS_0_1_START = SNOW_CASCADE_COVERAGE_0 * 0.4;   // 102m
    const float TRANS_0_1_END = SNOW_CASCADE_COVERAGE_0 * 0.5;     // 128m
    const float TRANS_1_2_START = SNOW_CASCADE_COVERAGE_1 * 0.4;   // 410m
    const float TRANS_1_2_END = SNOW_CASCADE_COVERAGE_1 * 0.5;     // 512m

    if (distToCamera < TRANS_0_1_START) {
        cascadeIdx = 0;
        blendFactor = 0.0;
    } else if (distToCamera < TRANS_0_1_END) {
        cascadeIdx = 0;
        blendFactor = (distToCamera - TRANS_0_1_START) / (TRANS_0_1_END - TRANS_0_1_START);
    } else if (distToCamera < TRANS_1_2_START) {
        cascadeIdx = 1;
        blendFactor = 0.0;
    } else if (distToCamera < TRANS_1_2_END) {
        cascadeIdx = 1;
        blendFactor = (distToCamera - TRANS_1_2_START) / (TRANS_1_2_END - TRANS_1_2_START);
    } else {
        cascadeIdx = 2;
        blendFactor = 0.0;
    }
}

// Sample volumetric snow height with cascade blending
// Returns snow height in meters
float sampleVolumetricSnowHeight(
    sampler2D cascade0, sampler2D cascade1, sampler2D cascade2,
    vec3 worldPos, vec3 cameraPos,
    vec4 cascade0Params, vec4 cascade1Params, vec4 cascade2Params
) {
    float distToCamera = length(worldPos.xz - cameraPos.xz);

    uint cascadeIdx;
    float blendFactor;
    selectSnowCascade(distToCamera, cascadeIdx, blendFactor);

    float height = 0.0;

    // Sample primary cascade
    if (cascadeIdx == 0) {
        height = sampleSnowCascadeHeight(cascade0, worldPos, cascade0Params);
        if (blendFactor > 0.0) {
            float nextHeight = sampleSnowCascadeHeight(cascade1, worldPos, cascade1Params);
            height = mix(height, nextHeight, blendFactor);
        }
    } else if (cascadeIdx == 1) {
        height = sampleSnowCascadeHeight(cascade1, worldPos, cascade1Params);
        if (blendFactor > 0.0) {
            float nextHeight = sampleSnowCascadeHeight(cascade2, worldPos, cascade2Params);
            height = mix(height, nextHeight, blendFactor);
        }
    } else {
        height = sampleSnowCascadeHeight(cascade2, worldPos, cascade2Params);
    }

    return height;
}

// Convert snow height to coverage factor for shader blending
// Uses a smooth ramp to transition from bare to fully covered
float snowHeightToCoverage(float heightMeters, float snowAmount, vec3 normalWS) {
    // Slope factor - more snow on flat surfaces, less on steep
    float upDot = max(dot(normalWS, SNOW_UP_DIR), 0.0);
    float slopeFactor = smoothstep(0.2, 0.7, upDot);

    // Height-based coverage (0.1m = start of visible snow, 0.5m = full coverage)
    float heightCoverage = smoothstep(0.0, 0.5, heightMeters);

    // Combine with global amount and slope
    return saturate(heightCoverage * snowAmount * slopeFactor);
}

// ============================================================================
// Vertex Displacement for Volumetric Snow
// ============================================================================

// Calculate vertex displacement from snow height
// Returns Y-axis displacement in world units
float calculateSnowDisplacement(float snowHeight, vec3 normalWS) {
    // Only displace upward-facing surfaces
    float upDot = max(dot(normalWS, SNOW_UP_DIR), 0.0);
    float displaceFactor = smoothstep(0.3, 0.8, upDot);

    // Scale displacement by height and slope
    return snowHeight * displaceFactor;
}

// Get displaced world position for terrain vertex
vec3 displaceVertexBySnow(vec3 worldPos, float snowHeight, vec3 normalWS) {
    float displacement = calculateSnowDisplacement(snowHeight, normalWS);
    return worldPos + vec3(0.0, displacement, 0.0);
}

// ============================================================================
// Parallax Occlusion Mapping for Snow Detail
// ============================================================================

// Simple POM for snow surface detail at close range
// viewDirTS: view direction in tangent space (normalized)
// Returns offset UV and final height
vec2 parallaxSnowMapping(
    sampler2D snowHeightTex,
    vec2 texCoords,
    vec3 viewDirTS,
    float distToCamera,
    vec4 cascadeParams
) {
    // Disable POM at distance
    if (distToCamera > POM_MAX_DISTANCE) {
        return texCoords;
    }

    // Reduce quality with distance
    float distFactor = saturate(distToCamera / POM_MAX_DISTANCE);
    int numSamples = int(mix(float(POM_MAX_SAMPLES), float(POM_MIN_SAMPLES), distFactor));

    // Height scale decreases with distance
    float heightScale = POM_HEIGHT_SCALE * (1.0 - distFactor * 0.5);

    // Steep parallax mapping
    float layerHeight = 1.0 / float(numSamples);
    float currentLayerHeight = 0.0;

    // Direction to shift texture coords per layer
    vec2 deltaUV = viewDirTS.xy / viewDirTS.z * heightScale / float(numSamples);

    vec2 currentUV = texCoords;
    float currentHeight = texture(snowHeightTex, currentUV).r / MAX_SNOW_HEIGHT_M;

    // Find intersection with heightfield
    while (currentLayerHeight < currentHeight) {
        currentUV -= deltaUV;
        currentHeight = texture(snowHeightTex, currentUV).r / MAX_SNOW_HEIGHT_M;
        currentLayerHeight += layerHeight;
    }

    // Interpolation for smoother result
    vec2 prevUV = currentUV + deltaUV;
    float afterHeight = currentHeight - currentLayerHeight;
    float beforeHeight = texture(snowHeightTex, prevUV).r / MAX_SNOW_HEIGHT_M - currentLayerHeight + layerHeight;
    float weight = afterHeight / (afterHeight - beforeHeight);

    return mix(currentUV, prevUV, weight);
}

// Full POM with self-shadowing for snow
// Returns: x,y = offset UV, z = shadow factor (0-1)
vec3 parallaxSnowMappingWithShadow(
    sampler2D snowHeightTex,
    vec2 texCoords,
    vec3 viewDirTS,
    vec3 lightDirTS,
    float distToCamera,
    vec4 cascadeParams
) {
    // Get parallax offset UV
    vec2 offsetUV = parallaxSnowMapping(snowHeightTex, texCoords, viewDirTS, distToCamera, cascadeParams);

    // Self-shadowing (simplified)
    float shadow = 1.0;

    if (distToCamera < POM_MAX_DISTANCE * 0.5) {
        // Only compute shadows very close
        float heightAtPoint = texture(snowHeightTex, offsetUV).r / MAX_SNOW_HEIGHT_M;

        // Trace toward light
        const int SHADOW_SAMPLES = 8;
        vec2 lightStep = lightDirTS.xy / lightDirTS.z * POM_HEIGHT_SCALE / float(SHADOW_SAMPLES);
        float shadowLayerHeight = heightAtPoint;

        vec2 shadowUV = offsetUV;
        for (int i = 0; i < SHADOW_SAMPLES; i++) {
            shadowUV += lightStep;
            shadowLayerHeight += 1.0 / float(SHADOW_SAMPLES);

            float sampleHeight = texture(snowHeightTex, shadowUV).r / MAX_SNOW_HEIGHT_M;
            if (sampleHeight > shadowLayerHeight) {
                shadow = 0.0;
                break;
            }
        }
    }

    return vec3(offsetUV, shadow);
}

// ============================================================================
// Snow Normal Calculation from Height
// ============================================================================

// Calculate normal from snow heightfield using central differences
vec3 calculateSnowNormalFromHeight(
    sampler2D snowHeightTex,
    vec2 uv,
    float texelSize,
    float heightScale
) {
    float hL = texture(snowHeightTex, uv + vec2(-texelSize, 0.0)).r * heightScale;
    float hR = texture(snowHeightTex, uv + vec2(texelSize, 0.0)).r * heightScale;
    float hD = texture(snowHeightTex, uv + vec2(0.0, -texelSize)).r * heightScale;
    float hU = texture(snowHeightTex, uv + vec2(0.0, texelSize)).r * heightScale;

    // Approximate gradient
    float dx = (hR - hL) / (2.0 * texelSize);
    float dz = (hU - hD) / (2.0 * texelSize);

    return normalize(vec3(-dx, 1.0, -dz));
}

// Blend base normal with snow normal based on coverage
vec3 blendSnowNormal(vec3 baseNormal, vec3 snowNormal, float coverage) {
    // Weight toward snow normal as coverage increases
    vec3 blended = normalize(mix(baseNormal, snowNormal, coverage));
    return blended;
}

#endif // SNOW_COMMON_GLSL
