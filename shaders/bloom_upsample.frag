#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = BINDING_BLOOM_INPUT) uniform sampler2D inputTexture;

layout(push_constant) uniform PushConstants {
    vec2 resolution;
    float filterRadius;
    float padding;
} pc;

// 9-tap tent filter for upsampling
void main() {
    vec2 texelSize = pc.filterRadius / pc.resolution;

    // 3x3 tent filter weights:
    // 1 2 1
    // 2 4 2
    // 1 2 1
    // Sum = 16

    vec3 result = vec3(0.0);

    // Center (4x weight)
    result += texture(inputTexture, vUV).rgb * 4.0;

    // Edge samples (2x weight)
    result += texture(inputTexture, vUV + vec2(-1.0,  0.0) * texelSize).rgb * 2.0;
    result += texture(inputTexture, vUV + vec2( 1.0,  0.0) * texelSize).rgb * 2.0;
    result += texture(inputTexture, vUV + vec2( 0.0, -1.0) * texelSize).rgb * 2.0;
    result += texture(inputTexture, vUV + vec2( 0.0,  1.0) * texelSize).rgb * 2.0;

    // Corner samples (1x weight)
    result += texture(inputTexture, vUV + vec2(-1.0, -1.0) * texelSize).rgb;
    result += texture(inputTexture, vUV + vec2( 1.0, -1.0) * texelSize).rgb;
    result += texture(inputTexture, vUV + vec2(-1.0,  1.0) * texelSize).rgb;
    result += texture(inputTexture, vUV + vec2( 1.0,  1.0) * texelSize).rgb;

    result /= 16.0;

    outColor = vec4(result, 1.0);
}
