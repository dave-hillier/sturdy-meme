#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"

// Vertex attributes (matching tree_leaf.vert)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 6) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int cascadeIndex;
    float alphaTest;
} push;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    vec3 localPos = inPosition;
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * push.model * vec4(localPos, 1.0);
    fragTexCoord = inTexCoord;
}
