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
    float roughness;
    float metallic;
    float emissiveIntensity;
    float opacity;  // For camera occlusion fading (1.0 = fully visible)
    vec4 emissiveColor;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in uvec4 inBoneIndices;
layout(location = 5) in vec4 inBoneWeights;
layout(location = 6) in vec4 inColor;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) out vec4 fragColor;

void main() {
    // Compute skinned position and normal
    mat4 skinMatrix =
        boneData.bones[inBoneIndices.x] * inBoneWeights.x +
        boneData.bones[inBoneIndices.y] * inBoneWeights.y +
        boneData.bones[inBoneIndices.z] * inBoneWeights.z +
        boneData.bones[inBoneIndices.w] * inBoneWeights.w;

    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * inNormal;
    vec3 skinnedTangent = mat3(skinMatrix) * inTangent.xyz;

    vec4 worldPos = push.model * skinnedPosition;
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragNormal = mat3(push.model) * skinnedNormal;
    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;
    // Transform tangent direction by model matrix, preserve handedness in w
    fragTangent = vec4(mat3(push.model) * skinnedTangent, inTangent.w);
    fragColor = inColor;
}
