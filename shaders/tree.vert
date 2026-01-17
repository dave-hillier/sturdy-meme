#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"
#include "wind_animation_common.glsl"

// Vertex attributes (matching Mesh::getAttributeDescriptions)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
// Locations 4, 5 are bone indices/weights (unused for trees)
layout(location = 6) in vec4 inColor;  // RGB = pivot point, A = branch level (0-1)

// Wind uniform buffer
layout(binding = BINDING_TREE_GFX_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float time;
    float lodBlendFactor;  // 0=full geometry, 1=full impostor (unused in vert, but must match frag layout)
    vec2 _pad;             // Explicit padding to align vec3 to 16 bytes
    vec3 barkTint;
    float roughnessScale;
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) out float fragBranchLevel;

void main() {
    vec3 localPos = inPosition;
    // Branch level stored in vertex color alpha (0-1, where 0 = trunk, 1 = tip branches)
    float branchLevel = inColor.a * 3.0;  // Scale back to 0-3 range
    // Pivot point for rotation stored in RGB (local space)
    vec3 pivotPoint = inColor.rgb;
    vec3 localNormal = inNormal;
    vec3 localTangent = inTangent.xyz;
    float tangentW = inTangent.w;
    vec2 texCoord = inTexCoord;

    // Extract wind parameters using common function
    WindParams windParams = windExtractParams(wind.windDirectionAndStrength, wind.windParams);

    // Get tree base position for noise sampling (use model matrix translation)
    vec3 treeBaseWorld = vec3(push.model[3][0], push.model[3][1], push.model[3][2]);

    // Calculate tree oscillation using GPU Gems 3 style animation
    TreeWindOscillation osc = windCalculateTreeOscillation(treeBaseWorld, windParams);

    // Wind direction-relative motion (branches facing wind move less)
    vec3 branchDir = normalize(localTangent);
    vec3 branchDirWorld = normalize(mat3(push.model) * branchDir);
    float directionScale = windCalculateDirectionScale(branchDirWorld, osc.windDir3D);

    // Branch flexibility (tips bend more than trunk)
    float flexibility = windCalculateBranchFlexibility(branchLevel);

    // Apply bending around pivot point
    vec3 offsetFromPivot = localPos - pivotPoint;

    // Calculate inherited sway from trunk at the branch attachment point
    // For trunk (pivotPoint.y = 0), this is zero. For branches, inherits trunk movement.
    float pivotHeight = pivotPoint.y;
    float trunkFlexibility = windCalculateBranchFlexibility(0.0);  // Level 0 flexibility
    float inheritedAmount = pivotHeight * trunkFlexibility * windParams.strength;
    vec3 inheritedSway = osc.windDir3D * osc.mainBend * inheritedAmount +
                         osc.windPerp3D * osc.perpBend * inheritedAmount * 0.5;

    // Calculate this branch's own sway (additional movement beyond inherited)
    vec3 branchSway = windCalculateBendOffset(osc, offsetFromPivot, flexibility, windParams.strength, directionScale);

    // High-frequency detail motion for tips
    vec3 detailOffset = windCalculateDetailOffset(localPos, branchLevel, windParams.strength, windParams.gustFreq, windParams.time);

    // Transform to world space FIRST, then apply world-space wind offsets
    // Total offset = inherited from trunk + branch's own sway + detail
    vec4 worldPos = push.model * vec4(localPos, 1.0);
    worldPos.xyz += inheritedSway + branchSway + detailOffset;

    gl_Position = ubo.proj * ubo.view * worldPos;

    // Transform normal and tangent (apply same rotation conceptually)
    mat3 normalMatrix = mat3(push.model);
    fragNormal = normalize(normalMatrix * localNormal);
    fragTangent = vec4(normalize(normalMatrix * localTangent), tangentW);
    fragTexCoord = texCoord;
    fragWorldPos = worldPos.xyz;
    fragBranchLevel = branchLevel;
}
