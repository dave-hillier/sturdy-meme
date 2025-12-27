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
layout(location = 6) out float fragLodBlendFactor;

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

    // Extract wind parameters
    float windStrength = wind.windDirectionAndStrength.z;
    float windScale = wind.windParams.z;
    float windTime = wind.windParams.w;
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float gustFreq = wind.windParams.x;

    // Wind direction in 3D
    vec3 windDir3D = vec3(windDir.x, 0.0, windDir.y);
    vec3 windPerp3D = vec3(-windDir.y, 0.0, windDir.x);

    // === Hierarchical Branch Influence (GPU Gems 3) ===
    // Leaves inherit motion from their parent tree's trunk/branch sway
    // Get tree base position from model matrix
    vec3 treeBaseWorld = vec3(tree.model[3][0], tree.model[3][1], tree.model[3][2]);

    // Per-tree phase (same calculation as in tree.vert for consistency)
    float treePhase = simplex3(treeBaseWorld * 0.1) * 6.28318;

    // Compute main trunk sway (same as tree.vert)
    float mainBendTime = windTime * gustFreq;
    float mainBend =
        0.5 * sin(mainBendTime + treePhase) +
        0.3 * sin(mainBendTime * 2.1 + treePhase * 1.3) +
        0.2 * sin(mainBendTime * 3.7 + treePhase * 0.7);

    float perpBend =
        0.3 * sin(mainBendTime * 1.3 + treePhase + 1.57) +
        0.2 * sin(mainBendTime * 2.7 + treePhase * 0.9);

    // Leaf height relative to tree base (approximate branch level)
    float leafHeight = worldPosition.y - treeBaseWorld.y;
    float branchInfluence = leafHeight * 0.03 * windStrength;

    // Hierarchical offset from tree sway (leaves move with branches)
    vec3 branchSway = windDir3D * mainBend * branchInfluence +
                      windPerp3D * perpBend * branchInfluence * 0.5;

    // === Detail Leaf Motion ===
    // Sample wind noise using world position for spatial coherence
    float windOffset = 2.0 * 3.14159265 * simplex3(worldPosition / windScale) + windPhaseOffset;

    // Multi-frequency oscillation values
    float osc1 = sin(windTime * gustFreq + windOffset);
    float osc2 = sin(2.0 * windTime * gustFreq + 1.3 * windOffset);
    float osc3 = sin(5.0 * windTime * gustFreq + 1.5 * windOffset);

    float oscillation = 0.5 * osc1 + 0.3 * osc2 + 0.2 * osc3;

    // Leaves sway more at tips (UV.y=0 = top of leaf) than at branch attachment (UV.y=1 = bottom)
    float swayFactor = 1.0 - inTexCoord.y;

    // === Leaf Rotation/Tilt from Wind Pressure (GPU Gems 3) ===
    // Wind pushes leaf to rotate around its attachment point
    // Tilt axis is perpendicular to wind direction (leaves fold over)
    vec3 tiltAxis = normalize(cross(windDir3D, vec3(0.0, 1.0, 0.0)));
    float tiltAngle = oscillation * windStrength * 0.4;  // Up to ~23 degrees

    // Also add some twist around leaf's up axis for flutter
    float twistAngle = (osc2 * 0.3 + osc3 * 0.2) * windStrength * 0.3;

    // Create rotation quaternions
    vec4 tiltQuat = quatFromAxisAngle(tiltAxis, tiltAngle);

    // Twist around leaf's local up (approximated as world Y rotated by orientation)
    vec3 leafUp = rotateByQuat(vec3(0.0, 1.0, 0.0), worldOrientation);
    vec4 twistQuat = quatFromAxisAngle(leafUp, twistAngle);

    // Combine rotations: first tilt, then twist
    vec4 windRotation = quatMul(twistQuat, tiltQuat);

    // Apply wind rotation to leaf orientation
    vec4 animatedOrientation = quatMul(windRotation, worldOrientation);

    // Scale the local quad position by leaf size
    vec3 scaledPos = inPosition * leafSize;

    // Rotate by animated orientation quaternion
    vec3 rotatedPos = rotateByQuat(scaledPos, animatedOrientation);

    // Apply position sway (reduced since rotation now handles much of the motion)
    vec3 leafSway = swayFactor * windStrength * windDir3D * oscillation * 0.3;

    // Combine all motion: base position + branch sway + leaf sway + rotated offset
    vec3 worldPos = worldPosition + branchSway + leafSway + rotatedPos;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    // Compute normal from animated orientation
    vec3 localNormal = vec3(0.0, 0.0, 1.0);
    vec3 worldNormal = rotateByQuat(localNormal, animatedOrientation);
    fragNormal = normalize(worldNormal);

    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos;
    fragLeafSize = leafSize;
    fragLeafTint = leafTint;
    fragAutumnHueShift = autumnHueShift;
    fragLodBlendFactor = tree.windPhaseAndLOD.y;
}
