// Snow layer common functions
// Shared across terrain, vegetation, and mesh shaders
#ifndef SNOW_COMMON_GLSL
#define SNOW_COMMON_GLSL

// Snow material properties
const vec3 SNOW_UP_DIR = vec3(0.0, 1.0, 0.0);
const float SNOW_ROUGHNESS_DEFAULT = 0.7;
const float SNOW_METALLIC = 0.0;

// SSS parameters for snow glow
const float SNOW_SSS_WRAP = 0.4;
const float SNOW_BACKSCATTER_STRENGTH = 0.3;

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

#endif // SNOW_COMMON_GLSL
