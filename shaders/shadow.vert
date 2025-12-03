#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "ubo_common.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    int cascadeIndex;  // Which cascade we're rendering
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
// Locations 4, 5 are bone indices/weights (unused)
layout(location = 6) in vec4 inColor;  // Not used, but must match vertex format

void main() {
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * push.model * vec4(inPosition, 1.0);
}
