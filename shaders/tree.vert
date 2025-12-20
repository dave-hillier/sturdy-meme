#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"

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
    vec3 localNormal = inNormal;
    vec3 localTangent = inTangent.xyz;
    float tangentW = inTangent.w;
    vec2 texCoord = inTexCoord;

    // Transform to world space
    vec4 worldPos = push.model * vec4(localPos, 1.0);

    // Branches do not animate with wind (ez-tree behavior - only leaves sway)

    gl_Position = ubo.proj * ubo.view * worldPos;

    // Transform normal and tangent
    mat3 normalMatrix = mat3(push.model);
    fragNormal = normalize(normalMatrix * localNormal);
    fragTangent = vec4(normalize(normalMatrix * localTangent), tangentW);
    fragTexCoord = texCoord;
    fragWorldPos = worldPos.xyz;
    fragBranchLevel = branchLevel;
}
