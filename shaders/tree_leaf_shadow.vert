#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "noise_common.glsl"
#include "tree_leaf_world.glsl"

// Vertex attributes (matching shared leaf quad mesh)
layout(location = 0) in vec3 inPosition;  // Local quad position
layout(location = 2) in vec2 inTexCoord;  // Quad UVs

// World-space leaf instance SSBO (from compute culling)
layout(std430, binding = BINDING_TREE_GFX_LEAF_INSTANCES) readonly buffer LeafInstanceBuffer {
    WorldLeafInstance leafInstances[];
};

// Tree render data SSBO (transforms, tints, etc.)
layout(std430, binding = BINDING_TREE_GFX_TREE_DATA) readonly buffer TreeDataBuffer {
    TreeRenderData treeData[];
};

// Wind uniform buffer (same binding as tree_leaf.vert, shared descriptor set)
layout(binding = BINDING_TREE_GFX_WIND_UBO) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
} wind;

// Simplified push constants - no more per-tree data
layout(push_constant) uniform PushConstants {
    int cascadeIndex;
    float alphaTest;
} push;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Get world-space leaf instance data from SSBO
    WorldLeafInstance leaf = leafInstances[gl_InstanceIndex];

    vec3 worldPosition = leaf.worldPosition.xyz;
    float leafSize = leaf.worldPosition.w;
    vec4 worldOrientation = leaf.worldOrientation;
    uint treeIndex = leaf.treeIndex;

    // Get tree render data
    TreeRenderData tree = treeData[treeIndex];
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

    // === Hierarchical Branch Influence (matching tree_leaf.vert) ===
    vec3 treeBaseWorld = vec3(tree.model[3][0], tree.model[3][1], tree.model[3][2]);
    float treePhase = simplex3(treeBaseWorld * 0.1) * 6.28318;

    float mainBendTime = windTime * gustFreq;
    float mainBend =
        0.5 * sin(mainBendTime + treePhase) +
        0.3 * sin(mainBendTime * 2.1 + treePhase * 1.3) +
        0.2 * sin(mainBendTime * 3.7 + treePhase * 0.7);

    float perpBend =
        0.3 * sin(mainBendTime * 1.3 + treePhase + 1.57) +
        0.2 * sin(mainBendTime * 2.7 + treePhase * 0.9);

    float leafHeight = worldPosition.y - treeBaseWorld.y;
    float branchInfluence = leafHeight * 0.03 * windStrength;

    vec3 branchSway = windDir3D * mainBend * branchInfluence +
                      windPerp3D * perpBend * branchInfluence * 0.5;

    // === Detail Leaf Motion ===
    float windOffset = 2.0 * 3.14159265 * simplex3(worldPosition / windScale) + windPhaseOffset;

    float osc1 = sin(windTime * gustFreq + windOffset);
    float osc2 = sin(2.0 * windTime * gustFreq + 1.3 * windOffset);
    float osc3 = sin(5.0 * windTime * gustFreq + 1.5 * windOffset);

    float oscillation = 0.5 * osc1 + 0.3 * osc2 + 0.2 * osc3;
    float swayFactor = 1.0 - inTexCoord.y;

    // === Leaf Rotation/Tilt ===
    vec3 tiltAxis = normalize(cross(windDir3D, vec3(0.0, 1.0, 0.0)));
    float tiltAngle = oscillation * windStrength * 0.4;
    float twistAngle = (osc2 * 0.3 + osc3 * 0.2) * windStrength * 0.3;

    vec4 tiltQuat = quatFromAxisAngle(tiltAxis, tiltAngle);
    vec3 leafUp = rotateByQuat(vec3(0.0, 1.0, 0.0), worldOrientation);
    vec4 twistQuat = quatFromAxisAngle(leafUp, twistAngle);
    vec4 windRotation = quatMul(twistQuat, tiltQuat);
    vec4 animatedOrientation = quatMul(windRotation, worldOrientation);

    // Scale and rotate
    vec3 scaledPos = inPosition * leafSize;
    vec3 rotatedPos = rotateByQuat(scaledPos, animatedOrientation);

    // Position sway
    vec3 leafSway = swayFactor * windStrength * windDir3D * oscillation * 0.3;

    // Combine all motion
    vec3 worldPos = worldPosition + branchSway + leafSway + rotatedPos;

    // Transform to shadow map space
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * vec4(worldPos, 1.0);

    fragTexCoord = inTexCoord;
}
