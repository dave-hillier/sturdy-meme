#version 450

#extension GL_GOOGLE_include_directive : require

#include "ubo_common.glsl"

// Push constants for tree rendering
layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float alphaTest;
    int isLeaf;
} pc;

// Vertex inputs (same as Mesh::Vertex)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 6) in vec4 inColor;

// Outputs to fragment shader
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragColor;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;

void main() {
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    // Transform normal and tangent to world space
    mat3 normalMatrix = mat3(pc.model);
    fragNormal = normalize(normalMatrix * inNormal);
    fragTangent = normalize(normalMatrix * inTangent.xyz);

    // Calculate bitangent (cross product with tangent handedness)
    fragBitangent = normalize(cross(fragNormal, fragTangent) * inTangent.w);

    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;
    fragColor = inColor;
}
