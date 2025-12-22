#version 450

#extension GL_GOOGLE_include_directive : require

#include "tree_leaf_instance.glsl"

// Vertex attributes (matching shared leaf quad mesh)
layout(location = 0) in vec3 inPosition;  // Local quad position
layout(location = 1) in vec3 inNormal;    // Not used - computed from orientation
layout(location = 2) in vec2 inTexCoord;  // Quad UVs

// Leaf instance SSBO (binding 2 for capture)
layout(std430, binding = 2) readonly buffer LeafInstanceBuffer {
    LeafInstance leafInstances[];
};

layout(push_constant) uniform PushConstants {
    mat4 viewProj;      // Combined view-projection for capture camera
    mat4 model;         // Tree model matrix (identity for capture)
    vec4 captureParams; // x = cell index, y = is leaf pass, z = bounding radius, w = alpha test
    int firstInstance;  // Offset into leafInstances[] for this tree
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragViewPos;  // Position in view space for depth

void main() {
    // Get leaf instance data from SSBO
    int instanceIndex = push.firstInstance + gl_InstanceIndex;
    LeafInstance leaf = leafInstances[instanceIndex];

    vec3 leafPosition = leaf.positionAndSize.xyz;
    float leafSize = leaf.positionAndSize.w;
    vec4 orientation = leaf.orientation;

    // Scale the local quad position by leaf size
    vec3 scaledPos = inPosition * leafSize;

    // Rotate by leaf orientation quaternion
    vec3 rotatedPos = rotateByQuat(scaledPos, orientation);

    // Transform to world space: leaf position is in tree-local space
    vec3 treeLocalPos = leafPosition + rotatedPos;
    vec4 worldPos = push.model * vec4(treeLocalPos, 1.0);
    vec4 viewPos = push.viewProj * worldPos;

    gl_Position = viewPos;

    // Compute normal from orientation
    vec3 localNormal = vec3(0.0, 0.0, 1.0);
    vec3 rotatedNormal = rotateByQuat(localNormal, orientation);
    mat3 normalMatrix = mat3(push.model);
    fragNormal = normalize(normalMatrix * rotatedNormal);

    fragTexCoord = inTexCoord;
    fragViewPos = viewPos.xyz / viewPos.w;  // NDC position for depth
}
