#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"
#include "tree_leaf_world.glsl"

// Vertex attributes (matching shared leaf quad mesh)
layout(location = 0) in vec3 inPosition;  // Local quad position [-0.5, 0.5] x [0, 1] x [0, 0]
layout(location = 1) in vec3 inNormal;    // Default normal (not used - computed from orientation)
layout(location = 2) in vec2 inTexCoord;  // Quad UVs

// World-space leaf instance SSBO (from compute culling)
layout(std430, binding = BINDING_TREE_GFX_LEAF_INSTANCES) readonly buffer LeafInstanceBuffer {
    WorldLeafInstance leafInstances[];
};

// Tree render data SSBO (transforms, tints, etc.)
layout(std430, binding = BINDING_TREE_GFX_TREE_DATA) readonly buffer TreeDataBuffer {
    TreeRenderData treeData[];
};

// Wind uniform buffer
layout(binding = BINDING_TREE_GFX_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

// Simplified push constants - no more per-tree data
layout(push_constant) uniform PushConstants {
    float time;
    float alphaTest;
} push;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out float fragLeafSize;
layout(location = 4) out vec3 fragLeafTint;
layout(location = 5) out float fragAutumnHueShift;

void main() {
    // Get world-space leaf instance data from SSBO
    WorldLeafInstance leaf = leafInstances[gl_InstanceIndex];

    vec3 worldPosition = leaf.worldPosition.xyz;
    float leafSize = leaf.worldPosition.w;
    vec4 worldOrientation = leaf.worldOrientation;
    uint treeIndex = leaf.treeIndex;

    // Get tree render data
    TreeRenderData tree = treeData[treeIndex];
    vec3 leafTint = tree.tintAndParams.rgb;
    float autumnHueShift = tree.tintAndParams.a;
    float windPhaseOffset = tree.windPhaseAndLOD.x;

    // Scale the local quad position by leaf size
    vec3 scaledPos = inPosition * leafSize;

    // Rotate by world-space orientation quaternion
    vec3 rotatedPos = rotateByQuatWorld(scaledPos, worldOrientation);

    // World position (already in world space from compute shader)
    vec3 worldPos = worldPosition + rotatedPos;

    // Wind animation for leaves (matches ez-tree behavior)
    float windStrength = wind.windDirectionAndStrength.z;
    float windScale = wind.windParams.z;
    float windTime = wind.windParams.w;
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float gustFreq = wind.windParams.x;

    // Sample wind noise using world position so all vertices of this leaf get same wind
    float windOffset = 2.0 * 3.14159265 * simplex3(worldPosition / windScale) + windPhaseOffset;

    // Leaves sway more at tips (UV.y=0 = top of leaf) than at branch attachment (UV.y=1 = bottom)
    float swayFactor = 1.0 - inTexCoord.y;

    // Multi-frequency wind sway (matching ez-tree formula)
    vec3 windSway = swayFactor * windStrength * vec3(windDir.x, 0.0, windDir.y) * (
        0.5 * sin(windTime * gustFreq + windOffset) +
        0.3 * sin(2.0 * windTime * gustFreq + 1.3 * windOffset) +
        0.2 * sin(5.0 * windTime * gustFreq + 1.5 * windOffset)
    );

    worldPos += windSway;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    // Compute normal from world-space orientation
    vec3 localNormal = vec3(0.0, 0.0, 1.0);
    vec3 worldNormal = rotateByQuatWorld(localNormal, worldOrientation);
    fragNormal = normalize(worldNormal);

    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos;
    fragLeafSize = leafSize;
    fragLeafTint = leafTint;
    fragAutumnHueShift = autumnHueShift;
}
