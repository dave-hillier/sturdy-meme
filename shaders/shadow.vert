#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 rayleighScattering;
    vec4 mieScattering;
    vec4 absorptionExtinction;
    vec4 atmosphereParams;
    float timeOfDay;
    float shadowMapSize;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

void main() {
    gl_Position = ubo.lightSpaceMatrix * push.model * vec4(inPosition, 1.0);
}
