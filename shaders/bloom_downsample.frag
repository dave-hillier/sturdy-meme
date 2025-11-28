#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform PushConstants {
    vec2 resolution;
    float threshold;  // Only used for first pass
    int isFirstPass;
} pc;

// Soft threshold function for smooth bloom extraction
vec3 softThreshold(vec3 color, float threshold) {
    float brightness = max(color.r, max(color.g, color.b));
    float soft = brightness - threshold + 0.5;
    soft = clamp(soft, 0.0, 1.0);
    soft = soft * soft * (3.0 - 2.0 * soft); // Smoothstep
    float contribution = max(soft, brightness - threshold);
    contribution /= max(brightness, 0.00001);
    return color * contribution;
}

// 13-tap downsample with Karis average to prevent fireflies
void main() {
    vec2 texelSize = 1.0 / pc.resolution;

    // Take 13 samples in a tent filter pattern
    vec3 a = texture(inputTexture, vUV + vec2(-2.0, -2.0) * texelSize).rgb;
    vec3 b = texture(inputTexture, vUV + vec2( 0.0, -2.0) * texelSize).rgb;
    vec3 c = texture(inputTexture, vUV + vec2( 2.0, -2.0) * texelSize).rgb;

    vec3 d = texture(inputTexture, vUV + vec2(-1.0, -1.0) * texelSize).rgb;
    vec3 e = texture(inputTexture, vUV + vec2( 1.0, -1.0) * texelSize).rgb;

    vec3 f = texture(inputTexture, vUV + vec2(-2.0,  0.0) * texelSize).rgb;
    vec3 g = texture(inputTexture, vUV).rgb;
    vec3 h = texture(inputTexture, vUV + vec2( 2.0,  0.0) * texelSize).rgb;

    vec3 i = texture(inputTexture, vUV + vec2(-1.0,  1.0) * texelSize).rgb;
    vec3 j = texture(inputTexture, vUV + vec2( 1.0,  1.0) * texelSize).rgb;

    vec3 k = texture(inputTexture, vUV + vec2(-2.0,  2.0) * texelSize).rgb;
    vec3 l = texture(inputTexture, vUV + vec2( 0.0,  2.0) * texelSize).rgb;
    vec3 m = texture(inputTexture, vUV + vec2( 2.0,  2.0) * texelSize).rgb;

    // Apply Karis average to reduce fireflies (bright single pixels)
    // Weight by 1 / (1 + brightness) to reduce impact of very bright pixels
    vec3 groups[5];
    groups[0] = (d + e + i + j) * 0.25;
    groups[1] = (a + b + g + f) * 0.25;
    groups[2] = (b + c + h + g) * 0.25;
    groups[3] = (f + g + l + k) * 0.25;
    groups[4] = (g + h + m + l) * 0.25;

    groups[0] *= 0.5 / (1.0 + max(groups[0].r, max(groups[0].g, groups[0].b)));
    groups[1] *= 0.125 / (1.0 + max(groups[1].r, max(groups[1].g, groups[1].b)));
    groups[2] *= 0.125 / (1.0 + max(groups[2].r, max(groups[2].g, groups[2].b)));
    groups[3] *= 0.125 / (1.0 + max(groups[3].r, max(groups[3].g, groups[3].b)));
    groups[4] *= 0.125 / (1.0 + max(groups[4].r, max(groups[4].g, groups[4].b)));

    vec3 result = groups[0] + groups[1] + groups[2] + groups[3] + groups[4];

    // Apply threshold only on first pass
    if (pc.isFirstPass != 0) {
        result = softThreshold(result, pc.threshold);
    }

    outColor = vec4(result, 1.0);
}
