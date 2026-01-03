#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"
#include "wind_animation_common.glsl"

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

    // Extract wind parameters using common function
    WindParams windParams = windExtractParams(wind.windDirectionAndStrength, wind.windParams);

    // Get tree base position for noise sampling
    vec3 treeBaseWorld = vec3(push.model[3][0], push.model[3][1], push.model[3][2]);

    // Calculate tree oscillation using GPU Gems 3 style animation
    TreeWindOscillation osc = windCalculateTreeOscillation(treeBaseWorld, windParams);

    // Wind direction-relative motion
    vec3 branchDir = normalize(localTangent);
    vec3 branchDirWorld = normalize(mat3(push.model) * branchDir);
    float directionScale = windCalculateDirectionScale(branchDirWorld, osc.windDir3D);

    // Branch flexibility
    float flexibility = windCalculateBranchFlexibility(branchLevel);

    // Apply bending around pivot point
    vec3 offsetFromPivot = localPos - pivotPoint;
    vec3 bendOffset = windCalculateBendOffset(osc, offsetFromPivot, flexibility, windParams.strength, directionScale);

    // Detail motion
    vec3 detailOffset = windCalculateDetailOffset(localPos, branchLevel, windParams.strength, windParams.gustFreq, windParams.time);

    // Transform to world space FIRST, then apply world-space wind offsets
    // This ensures shadow matches the main render pass
    vec4 worldPos = push.model * vec4(localPos, 1.0);
    worldPos.xyz += bendOffset + detailOffset;

    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * worldPos;
}
