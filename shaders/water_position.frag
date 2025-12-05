#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * water_position.frag - Water G-Buffer Position Pass Fragment Shader
 *
 * Phase 3: Outputs per-pixel water data to mini G-buffer.
 *
 * Output 0 (Data): R=materialID, G=LOD, B=foam, A=blend
 * Output 1 (Normal): RGB=mesh normal, A=water depth
 */

#include "constants_common.glsl"
#include "terrain_height_common.glsl"
#include "flow_common.glsl"
#include "bindings.glsl"

layout(binding = BINDING_UBO) uniform UniformBufferObject {
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

layout(std140, binding = BINDING_WATER_UBO) uniform WaterUniforms {
    vec4 waterColor;
    vec4 waveParams;
    vec4 waveParams2;
    vec4 waterExtent;
    vec4 scatteringCoeffs;
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
    float padding;
} water;

// Terrain heightmap for depth calculation
layout(binding = BINDING_WATER_TERRAIN_HEIGHT) uniform sampler2D terrainHeightMap;

// Flow map for foam and flow data
layout(binding = BINDING_WATER_FLOW_MAP) uniform sampler2D flowMap;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragWaveHeight;

// G-buffer outputs
layout(location = 0) out vec4 outData;    // R=materialID, G=LOD, B=foam, A=blend
layout(location = 1) out vec4 outNormal;  // RGB=normal, A=depth

void main() {
    // Calculate view distance for LOD
    float viewDistance = length(fragWorldPos - ubo.cameraPosition.xyz);

    // Calculate LOD factor (0 = near/high detail, 1 = far/low detail)
    float lodFactor = smoothstep(water.fbmNearDistance, water.fbmFarDistance, viewDistance);

    // Sample flow map
    vec2 flowUV = (fragWorldPos.xz - water.waterExtent.xy) / water.waterExtent.zw;
    flowUV = clamp(flowUV, 0.0, 1.0);
    vec4 flowData = texture(flowMap, flowUV);

    // Decode flow data
    float flowSpeed = flowData.b;
    float shoreDist = flowData.a;

    // Calculate water depth from terrain
    vec2 terrainUV = fragWorldPos.xz / water.terrainSize + 0.5;
    float terrainHeight = 0.0;
    float waterDepth = 0.0;
    bool insideTerrain = terrainUV.x >= 0.0 && terrainUV.x <= 1.0 &&
                         terrainUV.y >= 0.0 && terrainUV.y <= 1.0;

    if (insideTerrain) {
        terrainHeight = sampleTerrainHeight(terrainHeightMap, terrainUV, water.terrainHeightScale);
        waterDepth = max(0.0, fragWorldPos.y - terrainHeight);

        // Discard if terrain is above water
        if (terrainHeight > fragWorldPos.y + 0.1) {
            discard;
        }
    } else {
        waterDepth = 50.0;  // Assume deep water outside terrain
    }

    // Calculate foam amount (simplified for position pass)
    float foamAmount = 0.0;

    // Wave peak foam
    foamAmount = max(foamAmount, smoothstep(water.foamThreshold * 0.7, water.foamThreshold, fragWaveHeight));

    // Flow foam
    foamAmount = max(foamAmount, smoothstep(0.3, 0.8, flowSpeed) * water.flowFoamStrength);

    // Shore foam
    if (insideTerrain && waterDepth < water.shoreFoamWidth) {
        float shoreFoam = 1.0 - smoothstep(0.0, water.shoreFoamWidth * 0.5, waterDepth);
        foamAmount = max(foamAmount, shoreFoam);
    }

    // Obstacle foam (SDF-based)
    float obstacleFoam = (1.0 - smoothstep(0.0, 0.15, shoreDist)) * flowSpeed;
    foamAmount = max(foamAmount, obstacleFoam);

    foamAmount = clamp(foamAmount, 0.0, 1.0);

    // Pack output data
    // Data texture: R=materialID, G=LOD, B=foam, A=blend
    float materialID = 0.0;  // Default water material
    float blendMaterial = 0.0;  // No blend by default

    outData = vec4(materialID, lodFactor, foamAmount, blendMaterial);

    // Normal texture: RGB=normal (encoded), A=water depth
    vec3 normalEncoded = fragNormal * 0.5 + 0.5;  // Encode [-1,1] to [0,1]
    float depthEncoded = clamp(waterDepth / 100.0, 0.0, 1.0);  // Normalize depth

    outNormal = vec4(normalEncoded, depthEncoded);
}
