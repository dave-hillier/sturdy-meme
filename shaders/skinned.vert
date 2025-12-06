#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;
const int MAX_BONES = 128;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "push_constants_common.glsl"

layout(binding = BINDING_BONE_MATRICES) uniform BoneMatrices {
    mat4 bones[MAX_BONES];
} boneData;

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

    // For proper normal transformation with skinning, we need to use the inverse-transpose
    // of the 3x3 rotation/scale portion. Since bone matrices can include scale from animation
    // blending (linear blend of matrices is not orthonormal), we compute the proper normal matrix.
    mat3 skinMatrix3 = mat3(skinMatrix);
    mat3 normalMatrix = transpose(inverse(skinMatrix3));
    vec3 skinnedNormal = normalize(normalMatrix * inNormal);
    vec3 skinnedTangent = normalize(skinMatrix3 * inTangent.xyz);

    vec4 worldPos = material.model * skinnedPosition;
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragNormal = normalize(mat3(material.model) * skinnedNormal);
    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;
    // Transform tangent direction by model matrix, preserve handedness in w
    fragTangent = vec4(mat3(material.model) * skinnedTangent, inTangent.w);
    fragColor = inColor;
}
