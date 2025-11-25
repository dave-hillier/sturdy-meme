#version 450

const int NUM_CASCADES = 4;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];  // Per-cascade light matrices
    vec4 cascadeSplits;                   // View-space split depths
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;  // xyz = position, w = intensity
    vec4 pointLightColor;     // rgb = color, a = radius
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float padding;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int cascadeIndex;  // Which cascade we're rendering
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;  // Not used, but must match vertex format

void main() {
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * push.model * vec4(inPosition, 1.0);
}
