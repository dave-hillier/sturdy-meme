#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * water.frag - Water surface fragment shader
 * Implements Fresnel reflections, specular highlights, and foam for realistic water
 */

#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "atmosphere_common.glsl"
#include "terrain_height_common.glsl"

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];
    vec4 cascadeSplits;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;
    vec4 pointLightColor;
    vec4 windDirectionAndSpeed;
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;
} ubo;

// Water-specific uniforms
layout(std140, binding = 1) uniform WaterUniforms {
    vec4 waterColor;           // rgb = base water color, a = transparency
    vec4 waveParams;           // x = amplitude, y = wavelength, z = steepness, w = speed
    vec4 waveParams2;          // Second wave layer parameters
    vec4 waterExtent;          // xy = position offset, zw = size
    float waterLevel;          // Y height of water plane
    float foamThreshold;       // Wave height threshold for foam
    float fresnelPower;        // Fresnel reflection power
    float terrainSize;         // Terrain size for UV calculation
    float terrainHeightScale;  // Terrain height scale
    float shoreBlendDistance;  // Distance over which shore fades (world units)
    float shoreFoamWidth;      // Width of shore foam band (world units)
    float padding;
};

layout(binding = 2) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = 3) uniform sampler2D terrainHeightMap;

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
    // PROCEDURAL NORMAL DETAIL
    // =========================================================================
    vec2 detailUV = fragWorldPos.xz * 0.5;
    float detail1 = fbm(detailUV + time * 0.3, 3) * 2.0 - 1.0;
    float detail2 = fbm(detailUV * 1.7 - time * 0.2, 3) * 2.0 - 1.0;

    // Reduce wave detail in shallow water (calmer near shore)
    float shallowFactor = smoothstep(0.0, shoreFoamWidth * 2.0, waterDepth);
    float detailStrength = mix(0.05, 0.15, shallowFactor);

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
    // WATER COLOR - depth-based tinting
    // =========================================================================
    vec3 baseColor = waterColor.rgb;

    // Shallow water is lighter/greener, deep water is darker/bluer
    vec3 shallowColor = vec3(0.2, 0.5, 0.5);  // Stronger turquoise tint
    float depthColorFactor = smoothstep(0.0, 15.0, waterDepth);  // Wider transition
    baseColor = mix(shallowColor, baseColor, depthColorFactor);

    // Reflection color from environment
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 reflectionColor = sampleReflection(R, sunDir, sunColor);

    // Refraction color (simplified - just darkened water color)
    vec3 refractionColor = baseColor * 0.7;

    // Blend reflection and refraction based on Fresnel
    vec3 waterSurfaceColor = mix(refractionColor, reflectionColor, fresnel);

    // =========================================================================
    // SPECULAR HIGHLIGHTS
    // =========================================================================
    vec3 H = normalize(V + sunDir);
    float spec = pow(max(dot(N, H), 0.0), 256.0);
    vec3 sunSpecular = sunColor * spec * shadow;

    vec3 moonH = normalize(V + moonDir);
    float moonSpec = pow(max(dot(N, moonH), 0.0), 128.0);
    vec3 moonColor = ubo.moonColor.rgb * ubo.moonDirection.w;
    vec3 moonSpecular = moonColor * moonSpec * 0.5;

    // Diffuse sun lighting on water (very subtle)
    float NdotL = max(dot(N, sunDir), 0.0);
    vec3 diffuse = baseColor * sunColor * NdotL * shadow * 0.3;

    // Ambient lighting
    vec3 ambient = baseColor * ubo.ambientColor.rgb * 0.4;

    // =========================================================================
    // FOAM - wave peaks + shore foam
    // =========================================================================
    vec3 foamColor = vec3(0.9, 0.95, 1.0);

    // Wave peak foam (existing)
    float waveFoamAmount = smoothstep(foamThreshold * 0.7, foamThreshold, fragWaveHeight);
    float foamNoise = fbm(fragWorldPos.xz * 2.0 + time * 0.5, 4);
    waveFoamAmount *= smoothstep(0.3, 0.7, foamNoise);

    // Shore foam - where water is shallow
    float shoreFoamAmount = 0.0;
    if (insideTerrain && waterDepth > 0.0 && waterDepth < shoreFoamWidth) {
        // Animated foam line that follows the shore
        float shoreNoise = fbm(fragWorldPos.xz * 0.5 + time * 0.15, 3);
        float shoreNoise2 = fbm(fragWorldPos.xz * 1.5 - time * 0.3, 2);

        // Create foam bands at different depths (scaled by shoreFoamWidth)
        float fw = shoreFoamWidth;
        float band1 = smoothstep(0.0, fw * 0.15, waterDepth) * smoothstep(fw * 0.35, fw * 0.1, waterDepth);
        float band2 = smoothstep(fw * 0.25, fw * 0.4, waterDepth) * smoothstep(fw * 0.6, fw * 0.4, waterDepth);
        float band3 = smoothstep(fw * 0.5, fw * 0.7, waterDepth) * smoothstep(fw, fw * 0.7, waterDepth);

        // Modulate bands with noise for organic look (lower thresholds = more foam)
        shoreFoamAmount = band1 * smoothstep(0.25, 0.55, shoreNoise);
        shoreFoamAmount += band2 * smoothstep(0.3, 0.6, shoreNoise2) * 0.8;
        shoreFoamAmount += band3 * smoothstep(0.35, 0.6, shoreNoise) * 0.5;

        // Strong foam right at the waterline
        float waterlineIntensity = smoothstep(1.0, 0.0, waterDepth);
        shoreFoamAmount = max(shoreFoamAmount, waterlineIntensity);
    }

    float totalFoamAmount = max(waveFoamAmount, shoreFoamAmount);

    // =========================================================================
    // COMBINE LIGHTING
    // =========================================================================
    vec3 finalColor = waterSurfaceColor + sunSpecular + moonSpecular + diffuse + ambient;
    finalColor = mix(finalColor, foamColor, totalFoamAmount * 0.85);

    // Apply aerial perspective (atmospheric scattering)
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDistance = length(cameraToFrag);
    finalColor = applyAerialPerspective(finalColor, ubo.cameraPosition.xyz,
                                         normalize(cameraToFrag), viewDistance,
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
