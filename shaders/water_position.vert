#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * water_position.vert - Water G-Buffer Position Pass
 *
 * Phase 3: Renders water mesh to mini G-buffer at reduced resolution.
 * Outputs per-pixel data for deferred water compositing.
 */

#include "constants_common.glsl"
#include "ubo_common.glsl"
#include "bindings.glsl"

layout(std140, binding = BINDING_WATER_UBO) uniform WaterUniforms {
    // Primary material properties
    vec4 waterColor;
    vec4 waveParams;
    vec4 waveParams2;
    vec4 waterExtent;
    vec4 scatteringCoeffs;

    // Phase 12: Secondary material for blending
    vec4 waterColor2;
    vec4 scatteringCoeffs2;
    vec4 blendCenter;
    float absorptionScale2;
    float scatteringScale2;
    float specularRoughness2;
    float sssIntensity2;
    float blendDistance;
    int blendMode;

    float waterLevel;
    float foamThreshold;
    float fresnelPower;
    float terrainSize;
    float terrainHeightScale;
    float shoreBlendDistance;
    float shoreFoamWidth;
    float flowStrength;
    float flowSpeed;
    float flowFoamStrength;
    float fbmNearDistance;
    float fbmFarDistance;
    float specularRoughness;
    float absorptionScale;
    float scatteringScale;
    float displacementScale;
    float sssIntensity;
    float causticsScale;
    float causticsSpeed;
    float causticsIntensity;
    float nearPlane;
    float farPlane;
    float padding1;
    float padding2;
} water;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragWaveHeight;

// Simple Gerstner wave for position pass (matches main water shader)
vec3 gerstnerWave(vec2 pos, float amplitude, float wavelength, float steepness, float speed, vec2 direction) {
    float k = 2.0 * 3.14159 / wavelength;
    float c = sqrt(9.81 / k);
    float phase = k * (dot(direction, pos) - c * speed * ubo.windDirectionAndSpeed.w);

    float sinPhase = sin(phase);
    float cosPhase = cos(phase);

    vec3 displacement;
    displacement.x = steepness * amplitude * direction.x * cosPhase;
    displacement.z = steepness * amplitude * direction.y * cosPhase;
    displacement.y = amplitude * sinPhase;

    return displacement;
}

void main() {
    // Transform to world position
    vec3 worldPos = (ubo.model * vec4(inPosition, 1.0)).xyz;

    // Apply Gerstner waves - 6 octaves for detailed surface
    float amplitude = water.waveParams.x;
    float wavelength = water.waveParams.y;
    float steepness = water.waveParams.z;
    float speed = water.waveParams.w;

    float amplitude2 = water.waveParams2.x;
    float wavelength2 = water.waveParams2.y;
    float steepness2 = water.waveParams2.z;
    float speed2 = water.waveParams2.w;

    vec2 windDir = normalize(ubo.windDirectionAndSpeed.xy + vec2(0.001));
    vec2 crossDir = vec2(-windDir.y, windDir.x) * 0.7 + windDir * 0.3;
    vec2 counterDir = -windDir * 0.5 + vec2(windDir.y, -windDir.x) * 0.5;
    vec2 diagDir = normalize(windDir + vec2(1.0, 0.5));
    vec2 diagDir2 = normalize(windDir + vec2(-0.7, 0.7));
    vec2 rippleDir = normalize(vec2(windDir.y, -windDir.x) + windDir * 0.3);

    // Primary waves (low frequency, large amplitude)
    vec3 wave1 = gerstnerWave(worldPos.xz, amplitude, wavelength, steepness, speed, windDir);
    vec3 wave2 = gerstnerWave(worldPos.xz, amplitude * 0.5, wavelength * 0.6, steepness * 0.7, speed * 1.3, crossDir);
    vec3 wave3 = gerstnerWave(worldPos.xz, amplitude * 0.25, wavelength * 0.3, steepness * 0.5, speed * 0.7, counterDir);

    // Secondary waves (high frequency detail)
    vec3 wave4 = gerstnerWave(worldPos.xz, amplitude2 * 0.3, wavelength2 * 0.4, steepness2 * 0.6, speed2 * 1.8, diagDir);
    vec3 wave5 = gerstnerWave(worldPos.xz, amplitude2 * 0.2, wavelength2 * 0.25, steepness2 * 0.5, speed2 * 2.2, diagDir2);
    vec3 wave6 = gerstnerWave(worldPos.xz, amplitude2 * 0.12, wavelength2 * 0.15, steepness2 * 0.4, speed2 * 2.8, rippleDir);

    vec3 totalWave = wave1 + wave2 + wave3 + wave4 + wave5 + wave6;
    worldPos += totalWave;

    // Store wave height for foam calculation
    fragWaveHeight = totalWave.y;

    // Transform normal (simplified for position pass)
    fragNormal = normalize((ubo.model * vec4(inNormal, 0.0)).xyz);

    fragWorldPos = worldPos;
    fragTexCoord = inTexCoord;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
