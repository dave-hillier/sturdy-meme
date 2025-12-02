#version 450

const int NUM_CASCADES = 4;
const int MAX_BONES = 128;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];  // Per-cascade light matrices
    vec4 cascadeSplits;                   // View-space split depths
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;                       // rgb = moon color
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;  // xyz = position, w = intensity
    vec4 pointLightColor;     // rgb = color, a = radius
    vec4 windDirectionAndSpeed;           // xy = direction, z = speed, w = time
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;           // Julian day for sidereal rotation
} ubo;

layout(binding = 10) uniform BoneMatrices {
    mat4 bones[MAX_BONES];
} boneData;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int cascadeIndex;  // Which cascade we're rendering
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in uvec4 inBoneIndices;
layout(location = 5) in vec4 inBoneWeights;

void main() {
    // Compute skinned position
    mat4 skinMatrix =
        boneData.bones[inBoneIndices.x] * inBoneWeights.x +
        boneData.bones[inBoneIndices.y] * inBoneWeights.y +
        boneData.bones[inBoneIndices.z] * inBoneWeights.z +
        boneData.bones[inBoneIndices.w] * inBoneWeights.w;

    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);

    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * push.model * skinnedPosition;
}
