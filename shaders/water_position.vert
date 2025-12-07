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

    // Apply Gerstner waves
    float amplitude = water.waveParams.x;
    float wavelength = water.waveParams.y;
    float steepness = water.waveParams.z;
    float speed = water.waveParams.w;

    vec2 waveDir1 = normalize(vec2(1.0, 0.3));
    vec2 waveDir2 = normalize(vec2(-0.5, 0.8));

    vec3 wave1 = gerstnerWave(worldPos.xz, amplitude, wavelength, steepness, speed, waveDir1);
    vec3 wave2 = gerstnerWave(worldPos.xz, amplitude * 0.5, wavelength * 0.7, steepness * 0.8, speed * 1.2, waveDir2);

    vec3 totalWave = wave1 + wave2;
    worldPos += totalWave;

    // Store wave height for foam calculation
    fragWaveHeight = totalWave.y;

    // Transform normal (simplified for position pass)
    fragNormal = normalize((ubo.model * vec4(inNormal, 0.0)).xyz);

    fragWorldPos = worldPos;
    fragTexCoord = inTexCoord;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
