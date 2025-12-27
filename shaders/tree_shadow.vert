#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"

// Vertex attributes (matching tree.vert)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 6) in vec4 inColor;

// Wind uniform buffer (same binding as tree.vert, shared descriptor set)
layout(binding = BINDING_TREE_GFX_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int cascadeIndex;
} push;

void main() {
    vec3 localPos = inPosition;
    // Branch level stored in vertex color alpha (0-1, where 0 = trunk, 1 = tip branches)
    float branchLevel = inColor.a * 3.0;  // Scale back to 0-3 range
    // Pivot point for rotation stored in RGB (local space)
    vec3 pivotPoint = inColor.rgb;
    vec3 localTangent = inTangent.xyz;

    // Extract wind parameters
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windStrength = wind.windDirectionAndStrength.z;
    float gustFreq = wind.windParams.x;
    float windTime = wind.windParams.w;

    // === GPU Gems 3 Style Wind Animation (matching tree.vert) ===

    // Get tree base position for noise sampling
    vec3 treeBaseWorld = vec3(push.model[3][0], push.model[3][1], push.model[3][2]);

    // Per-tree phase offset
    float treePhase = simplex3(treeBaseWorld * 0.1) * 6.28318;

    // Wind direction in 3D
    vec3 windDir3D = vec3(windDir.x, 0.0, windDir.y);
    vec3 windPerp3D = vec3(-windDir.y, 0.0, windDir.x);

    // Main bending oscillation
    float mainBendTime = windTime * gustFreq;
    float mainBend =
        0.5 * sin(mainBendTime + treePhase) +
        0.3 * sin(mainBendTime * 2.1 + treePhase * 1.3) +
        0.2 * sin(mainBendTime * 3.7 + treePhase * 0.7);

    float perpBend =
        0.3 * sin(mainBendTime * 1.3 + treePhase + 1.57) +
        0.2 * sin(mainBendTime * 2.7 + treePhase * 0.9);

    // Wind direction-relative motion
    vec3 branchDir = normalize(localTangent);
    vec3 branchDirWorld = normalize(mat3(push.model) * branchDir);
    float windAlignment = dot(branchDirWorld, windDir3D);
    float directionScale = mix(1.5, 0.5, (windAlignment + 1.0) * 0.5);

    // Branch flexibility
    float flexibility = 0.02 + branchLevel * 0.025;

    // Apply bending
    vec3 offsetFromPivot = localPos - pivotPoint;
    float heightAbovePivot = max(0.0, offsetFromPivot.y);
    float bendAmount = heightAbovePivot * flexibility * windStrength * directionScale;

    vec3 bendOffset = windDir3D * mainBend * bendAmount +
                      windPerp3D * perpBend * bendAmount * 0.5;

    // Detail motion
    float detailFreq = windTime * gustFreq * 5.0;
    float detailNoise = simplex3(vec3(localPos.x * 2.0, localPos.y * 2.0, detailFreq * 0.3));
    float detailAmount = branchLevel * 0.01 * windStrength;
    vec3 detailOffset = vec3(detailNoise, 0.0, detailNoise * 0.7) * detailAmount;

    vec3 animatedLocalPos = localPos + bendOffset + detailOffset;

    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * push.model * vec4(animatedLocalPos, 1.0);
}
