#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * water.tese - Tessellation Evaluation Shader for Water Surface
 *
 * Interpolates tessellated vertices and applies wave displacement:
 * - FFT ocean simulation with 3 cascades (Tessendorf)
 * - Gerstner waves for smaller water bodies
 * - Wave shoaling and breaking detection
 * - Interactive splash displacement
 */

#include "ubo_common.glsl"
#include "bindings.glsl"

// Triangles with equal spacing
layout(triangles, equal_spacing, ccw) in;

// Water-specific uniforms
layout(std140, binding = BINDING_WATER_UBO) uniform WaterUniforms {
    vec4 waterColor;
    vec4 waveParams;           // x = amplitude, y = wavelength, z = steepness, w = speed
    vec4 waveParams2;
    vec4 waterExtent;          // xy = position offset, zw = size
    vec4 scatteringCoeffs;

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
};

// Displacement map (Phase 4: Interactive splashes)
layout(binding = BINDING_WATER_DISPLACEMENT) uniform sampler2D displacementMap;

// Terrain height map for breaking wave detection
layout(binding = BINDING_WATER_TERRAIN_HEIGHT) uniform sampler2D terrainHeightMap;

// FFT Ocean displacement maps (3 cascades)
layout(binding = BINDING_WATER_OCEAN_DISP) uniform sampler2D oceanDisplacement0;
layout(binding = BINDING_WATER_OCEAN_NORMAL) uniform sampler2D oceanNormal0;
layout(binding = BINDING_WATER_OCEAN_FOAM) uniform sampler2D oceanFoam0;

layout(binding = BINDING_WATER_OCEAN_DISP_1) uniform sampler2D oceanDisplacement1;
layout(binding = BINDING_WATER_OCEAN_NORMAL_1) uniform sampler2D oceanNormal1;
layout(binding = BINDING_WATER_OCEAN_FOAM_1) uniform sampler2D oceanFoam1;

layout(binding = BINDING_WATER_OCEAN_DISP_2) uniform sampler2D oceanDisplacement2;
layout(binding = BINDING_WATER_OCEAN_NORMAL_2) uniform sampler2D oceanNormal2;
layout(binding = BINDING_WATER_OCEAN_FOAM_2) uniform sampler2D oceanFoam2;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int useFFTOcean;
    float oceanSize0;
    float oceanSize1;
    float oceanSize2;
} push;

// Input from tessellation control shader
layout(location = 0) in vec3 tesWorldPos[];
layout(location = 1) in vec3 tesNormal[];
layout(location = 2) in vec2 tesTexCoord[];

// Output to fragment shader (must match water.frag inputs)
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragWaveHeight;
layout(location = 4) out float fragJacobian;
layout(location = 5) out float fragWaveSlope;
layout(location = 6) out float fragOceanFoam;
layout(location = 7) out float fragBreakingWave;

// =========================================================================
// Wave Utility Functions
// =========================================================================

vec2 worldPosToTerrainUV(vec2 worldXZ) {
    return (worldXZ / terrainSize) + 0.5;
}

float getWaterDepth(vec2 worldXZ) {
    vec2 terrainUV = worldPosToTerrainUV(worldXZ);
    if (terrainUV.x < 0.0 || terrainUV.x > 1.0 || terrainUV.y < 0.0 || terrainUV.y > 1.0) {
        return 100.0;  // Deep water outside terrain
    }
    float terrainHeight = texture(terrainHeightMap, terrainUV).r * terrainHeightScale;
    return waterLevel - terrainHeight;
}

float applyShoaling(float waveHeight, float waterDepth, float wavelength, out float breakingIntensity) {
    breakingIntensity = 0.0;
    float deepWaterDepth = wavelength * 0.5;
    if (waterDepth > deepWaterDepth || waterDepth <= 0.0) {
        return waveHeight;
    }
    float minDepth = 0.5;
    float effectiveDepth = max(waterDepth, minDepth);
    float shoalingFactor = sqrt(deepWaterDepth / effectiveDepth);
    shoalingFactor = min(shoalingFactor, 2.5);
    float shoaledHeight = waveHeight * shoalingFactor;

    float breakingThreshold = 0.78 * effectiveDepth;
    if (shoaledHeight > breakingThreshold) {
        breakingIntensity = smoothstep(breakingThreshold, breakingThreshold * 2.0, shoaledHeight);
        shoaledHeight = breakingThreshold;
    }
    return shoaledHeight;
}

// Gerstner wave function
vec3 gerstnerWave(vec2 pos, float time, vec2 direction, float wavelength, float steepness, float amplitude,
                  out vec3 tangent, out vec3 bitangent, out float jacobian) {
    float k = 2.0 * 3.14159265359 / wavelength;
    float c = sqrt(9.8 / k);
    vec2 d = normalize(direction);
    float f = k * (dot(d, pos) - c * time);
    float a = steepness / k;

    vec3 displacement;
    displacement.x = d.x * (a * cos(f));
    displacement.z = d.y * (a * cos(f));
    displacement.y = amplitude * sin(f);

    tangent = vec3(
        1.0 - d.x * d.x * steepness * sin(f),
        d.x * steepness * cos(f),
        -d.x * d.y * steepness * sin(f)
    );

    bitangent = vec3(
        -d.x * d.y * steepness * sin(f),
        d.y * steepness * cos(f),
        1.0 - d.y * d.y * steepness * sin(f)
    );

    jacobian = 1.0 - steepness * cos(f);
    return displacement;
}

// FFT sampling functions
vec4 sampleFFTOceanCascade(sampler2D dispMap, vec2 worldXZ, float oceanSize) {
    vec2 uv = worldXZ / oceanSize;
    return texture(dispMap, uv);
}

vec3 sampleFFTNormalCascade(sampler2D normalMap, vec2 worldXZ, float oceanSize) {
    vec2 uv = worldXZ / oceanSize;
    vec3 normal = texture(normalMap, uv).xyz * 2.0 - 1.0;
    return normal;
}

float sampleFFTFoamCascade(sampler2D foamMap, vec2 worldXZ, float oceanSize) {
    vec2 uv = worldXZ / oceanSize;
    return texture(foamMap, uv).r;
}

vec3 blendNormalsRNM(vec3 n1, vec3 n2) {
    n1.z += 1.0;
    n2.xy = -n2.xy;
    return normalize(n1 * dot(n1, n2) - n2 * n1.z);
}

void main() {
    // Barycentric interpolation of control point attributes
    vec3 pos = gl_TessCoord.x * tesWorldPos[0]
             + gl_TessCoord.y * tesWorldPos[1]
             + gl_TessCoord.z * tesWorldPos[2];

    vec3 normal = gl_TessCoord.x * tesNormal[0]
                + gl_TessCoord.y * tesNormal[1]
                + gl_TessCoord.z * tesNormal[2];

    vec2 texCoord = gl_TessCoord.x * tesTexCoord[0]
                  + gl_TessCoord.y * tesTexCoord[1]
                  + gl_TessCoord.z * tesTexCoord[2];

    float time = ubo.windDirectionAndSpeed.w;
    vec2 windDir = normalize(ubo.windDirectionAndSpeed.xy + vec2(0.001));

    vec3 worldPos = pos;
    vec2 worldXZ = pos.xz;

    vec3 totalDisplacement = vec3(0.0);
    vec3 finalNormal = vec3(0.0, 1.0, 0.0);
    float totalJacobian = 1.0;
    float oceanFoam = 0.0;

    if (push.useFFTOcean != 0) {
        // FFT Ocean Mode
        vec4 disp0 = sampleFFTOceanCascade(oceanDisplacement0, worldXZ, push.oceanSize0);
        vec4 disp1 = sampleFFTOceanCascade(oceanDisplacement1, worldXZ, push.oceanSize1);
        vec4 disp2 = sampleFFTOceanCascade(oceanDisplacement2, worldXZ, push.oceanSize2);

        totalDisplacement = disp0.xyz + disp1.xyz + disp2.xyz;
        totalJacobian = disp0.w * disp1.w * disp2.w;

        vec3 normal0 = sampleFFTNormalCascade(oceanNormal0, worldXZ, push.oceanSize0);
        vec3 normal1 = sampleFFTNormalCascade(oceanNormal1, worldXZ, push.oceanSize1);
        vec3 normal2 = sampleFFTNormalCascade(oceanNormal2, worldXZ, push.oceanSize2);

        vec3 blendedNormal = blendNormalsRNM(normal0, normal1);
        blendedNormal = blendNormalsRNM(blendedNormal, normal2);
        finalNormal = normalize(blendedNormal);

        float foam0 = sampleFFTFoamCascade(oceanFoam0, worldXZ, push.oceanSize0);
        float foam1 = sampleFFTFoamCascade(oceanFoam1, worldXZ, push.oceanSize1);
        float foam2 = sampleFFTFoamCascade(oceanFoam2, worldXZ, push.oceanSize2);
        oceanFoam = max(max(foam0, foam1), foam2);

        worldPos += totalDisplacement;

    } else {
        // Gerstner Wave Mode
        vec3 totalTangent = vec3(1.0, 0.0, 0.0);
        vec3 totalBitangent = vec3(0.0, 0.0, 1.0);

        vec3 tangent1, bitangent1;
        float jacobian1;
        vec3 wave1 = gerstnerWave(worldXZ, time * waveParams.w, windDir,
                                   waveParams.y, waveParams.z, waveParams.x,
                                   tangent1, bitangent1, jacobian1);
        totalDisplacement += wave1;
        totalJacobian *= jacobian1;

        vec3 tangent2, bitangent2;
        float jacobian2;
        vec2 crossDir = vec2(-windDir.y, windDir.x) * 0.7 + windDir * 0.3;
        vec3 wave2 = gerstnerWave(worldXZ, time * waveParams2.w * 1.3, crossDir,
                                   waveParams2.y * 0.6, waveParams2.z * 0.7, waveParams2.x * 0.5,
                                   tangent2, bitangent2, jacobian2);
        totalDisplacement += wave2;
        totalJacobian *= jacobian2;

        vec3 tangent3, bitangent3;
        float jacobian3;
        vec2 counterDir = -windDir * 0.5 + vec2(windDir.y, -windDir.x) * 0.5;
        vec3 wave3 = gerstnerWave(worldXZ, time * waveParams.w * 0.7, counterDir,
                                   waveParams.y * 0.3, waveParams.z * 0.5, waveParams.x * 0.4,
                                   tangent3, bitangent3, jacobian3);
        totalDisplacement += wave3;
        totalJacobian *= jacobian3;

        vec3 tangent4, bitangent4;
        float jacobian4;
        vec2 rippleDir = normalize(windDir + vec2(0.3, 0.7));
        vec3 wave4 = gerstnerWave(worldXZ, time * waveParams2.w * 1.5, rippleDir,
                                   2.0, 0.2, 0.05,
                                   tangent4, bitangent4, jacobian4);
        totalDisplacement += wave4;
        totalJacobian *= jacobian4;

        totalTangent = normalize(tangent1 + tangent2 * 0.7 + tangent3 * 0.5 + tangent4 * 0.3);
        totalBitangent = normalize(bitangent1 + bitangent2 * 0.7 + bitangent3 * 0.5 + bitangent4 * 0.3);
        finalNormal = normalize(cross(totalBitangent, totalTangent));

        worldPos += totalDisplacement;
    }

    // Interactive displacement
    vec2 displacementUV = (worldPos.xz - waterExtent.xy) / waterExtent.zw + 0.5;
    displacementUV = clamp(displacementUV, 0.0, 1.0);
    float interactiveDisplacement = texture(displacementMap, displacementUV).r;
    worldPos.y += interactiveDisplacement * displacementScale;

    // Wave shoaling and breaking
    float breakingIntensity = 0.0;
    float waterDepth = getWaterDepth(worldPos.xz);
    float primaryWavelength = push.useFFTOcean != 0 ? push.oceanSize0 * 0.5 : waveParams.y;
    float originalWaveHeight = totalDisplacement.y;

    if (waterDepth > 0.0 && waterDepth < primaryWavelength) {
        float shoaledHeight = applyShoaling(abs(originalWaveHeight), waterDepth, primaryWavelength, breakingIntensity);
        float shoalingMultiplier = (abs(originalWaveHeight) > 0.001)
            ? shoaledHeight / abs(originalWaveHeight)
            : 1.0;
        worldPos.y += (shoaledHeight - abs(originalWaveHeight)) * sign(originalWaveHeight);
        totalDisplacement.y = originalWaveHeight * shoalingMultiplier;
    }

    // Output
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    fragWorldPos = worldPos;
    fragNormal = finalNormal;
    fragTexCoord = texCoord;
    fragWaveHeight = totalDisplacement.y + interactiveDisplacement * displacementScale;
    fragJacobian = totalJacobian;
    fragOceanFoam = oceanFoam;
    fragBreakingWave = breakingIntensity;
    fragWaveSlope = 1.0 - abs(finalNormal.y);
}
