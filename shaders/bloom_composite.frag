#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = BINDING_BLOOM_INPUT) uniform sampler2D hdrTexture;
layout(binding = BINDING_BLOOM_SECONDARY) uniform sampler2D bloomTexture;

layout(push_constant) uniform PushConstants {
    float bloomIntensity;
    float padding1;
    float padding2;
    float padding3;
} pc;

void main() {
    vec3 hdr = texture(hdrTexture, vUV).rgb;
    vec3 bloom = texture(bloomTexture, vUV).rgb;

    // Additive blend with intensity control
    vec3 result = hdr + bloom * pc.bloomIntensity;

    outColor = vec4(result, 1.0);
}
