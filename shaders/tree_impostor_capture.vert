#version 450

// Tree impostor capture vertex shader
// Renders tree geometry from a specific viewing angle for impostor atlas generation

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;      // Combined view-projection for capture camera
    mat4 model;         // Tree model matrix (identity for capture)
    vec4 captureParams; // x = cell index, y = is leaf pass, z = bounding radius, w = unused
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragViewPos;  // Position in view space for depth

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    vec4 viewPos = push.viewProj * worldPos;

    gl_Position = viewPos;

    // Transform normal to world space
    mat3 normalMatrix = mat3(push.model);
    fragNormal = normalize(normalMatrix * inNormal);

    fragTexCoord = inTexCoord;
    fragViewPos = viewPos.xyz / viewPos.w;  // NDC position for depth
}
