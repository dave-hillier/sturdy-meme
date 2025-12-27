#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"

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

    // Extract wind parameters
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windStrength = wind.windDirectionAndStrength.z;
    float windSpeed = wind.windDirectionAndStrength.w;
    float gustFreq = wind.windParams.x;
    float gustAmp = wind.windParams.y;
    float windScale = wind.windParams.z;
    float windTime = wind.windParams.w;

    // === GPU Gems 3 Style Wind Animation ===

    // Get tree base position for noise sampling (use model matrix translation)
    vec3 treeBaseWorld = vec3(push.model[3][0], push.model[3][1], push.model[3][2]);

    // Per-tree phase offset from noise (so trees don't sway in sync)
    float treePhase = simplex3(treeBaseWorld * 0.1) * 6.28318;

    // Wind direction in 3D (XZ plane)
    vec3 windDir3D = vec3(windDir.x, 0.0, windDir.y);

    // Perpendicular to wind direction (for secondary sway)
    vec3 windPerp3D = vec3(-windDir.y, 0.0, windDir.x);

    // === Main Bending (Trunk Sway) ===
    // Multi-frequency oscillation for natural motion
    float mainBendTime = windTime * gustFreq;
    float mainBend =
        0.5 * sin(mainBendTime + treePhase) +
        0.3 * sin(mainBendTime * 2.1 + treePhase * 1.3) +
        0.2 * sin(mainBendTime * 3.7 + treePhase * 0.7);

    // Secondary perpendicular sway (figure-8 motion)
    float perpBend =
        0.3 * sin(mainBendTime * 1.3 + treePhase + 1.57) +
        0.2 * sin(mainBendTime * 2.7 + treePhase * 0.9);

    // === Wind Direction-Relative Branch Motion ===
    // Branches facing into wind move less, back-facing move more
    vec3 branchDir = normalize(localTangent);
    vec3 branchDirWorld = normalize(mat3(push.model) * branchDir);
    float windAlignment = dot(branchDirWorld, windDir3D);
    // windAlignment: 1 = facing wind, -1 = back to wind
    // Scale: back-facing (1.5x), perpendicular (1.0x), wind-facing (0.5x)
    float directionScale = mix(1.5, 0.5, (windAlignment + 1.0) * 0.5);

    // === Branch Flexibility ===
    // Higher branch level = more flexible (tips bend more than trunk)
    // branchLevel 0 = trunk (stiff), 3 = tips (flexible)
    float flexibility = 0.02 + branchLevel * 0.025;  // 0.02 to 0.095

    // === Apply Bending ===
    // Offset from pivot point (bend happens around pivot)
    vec3 offsetFromPivot = localPos - pivotPoint;
    float heightAbovePivot = max(0.0, offsetFromPivot.y);

    // Bend amount increases with height above pivot and flexibility
    float bendAmount = heightAbovePivot * flexibility * windStrength * directionScale;

    // Apply main bend in wind direction + perpendicular sway
    vec3 bendOffset = windDir3D * mainBend * bendAmount +
                      windPerp3D * perpBend * bendAmount * 0.5;

    // === High-Frequency Detail Motion for Tips ===
    // Small rapid oscillations on thinner branches
    float detailFreq = windTime * gustFreq * 5.0;
    float detailNoise = simplex3(vec3(localPos.x * 2.0, localPos.y * 2.0, detailFreq * 0.3));
    float detailAmount = branchLevel * 0.01 * windStrength;
    vec3 detailOffset = vec3(detailNoise, 0.0, detailNoise * 0.7) * detailAmount;

    // Combine all offsets
    vec3 animatedLocalPos = localPos + bendOffset + detailOffset;

    // Transform to world space
    vec4 worldPos = push.model * vec4(animatedLocalPos, 1.0);

    gl_Position = ubo.proj * ubo.view * worldPos;

    // Transform normal and tangent (apply same rotation conceptually)
    mat3 normalMatrix = mat3(push.model);
    fragNormal = normalize(normalMatrix * localNormal);
    fragTangent = vec4(normalize(normalMatrix * localTangent), tangentW);
    fragTexCoord = texCoord;
    fragWorldPos = worldPos.xyz;
    fragBranchLevel = branchLevel;
}
