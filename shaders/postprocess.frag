#version 450

layout(binding = 0) uniform sampler2D hdrInput;

layout(binding = 1) uniform PostProcessUniforms {
    float exposure;
    float bloomThreshold;
    float bloomIntensity;
    float autoExposure;  // 0 = manual, 1 = auto
    float previousExposure;
    float deltaTime;
    float adaptationSpeed;
    float bloomRadius;
} ubo;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// ACES Filmic Tone Mapping
vec3 ACESFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Compute luminance using standard weights
float getLuminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Sample luminance at multiple points for auto-exposure
float computeAverageLuminance() {
    const float minLum = 0.001;
    const float maxLum = 10.0;

    // Sample a 5x5 grid across the image (center-weighted)
    float totalLogLum = 0.0;
    float totalWeight = 0.0;

    // Center-weighted sampling pattern
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            vec2 uv = vec2(0.1 + 0.2 * float(x), 0.1 + 0.2 * float(y));

            // Center-weighted bias
            vec2 center = vec2(0.5, 0.5);
            float dist = length(uv - center);
            float weight = 1.0 - dist * 0.5;

            vec3 color = texture(hdrInput, uv).rgb;
            float lum = clamp(getLuminance(color), minLum, maxLum);

            // Log-average for geometric mean
            totalLogLum += log(lum) * weight;
            totalWeight += weight;
        }
    }

    // Convert back from log space (geometric mean)
    return exp(totalLogLum / totalWeight);
}

// Extract bright pixels for bloom with soft knee
vec3 extractBright(vec3 color, float threshold) {
    float brightness = getLuminance(color);
    // Soft knee for smoother transition
    float knee = threshold * 0.5;
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.0001);
    float contribution = max(soft, brightness - threshold) / max(brightness, 0.0001);
    return color * max(contribution, 0.0);
}

// Simple blur using weighted samples at multiple scales
vec3 sampleBloom(vec2 uv, float radius) {
    vec2 texelSize = 1.0 / vec2(textureSize(hdrInput, 0));
    vec3 bloom = vec3(0.0);
    float totalWeight = 0.0;

    // Multi-scale sampling for wider blur without separate passes
    // Scale 1: Fine detail (13-tap cross)
    const float scale1 = 1.0;
    const float weight1 = 0.3;

    // Scale 2: Medium blur
    const float scale2 = 4.0;
    const float weight2 = 0.4;

    // Scale 3: Wide blur
    const float scale3 = 8.0;
    const float weight3 = 0.3;

    // Gaussian-like 13-tap filter pattern (3 scales)
    vec2 offsets[13];
    offsets[0] = vec2(0.0, 0.0);
    offsets[1] = vec2(1.0, 0.0);
    offsets[2] = vec2(-1.0, 0.0);
    offsets[3] = vec2(0.0, 1.0);
    offsets[4] = vec2(0.0, -1.0);
    offsets[5] = vec2(1.0, 1.0);
    offsets[6] = vec2(-1.0, -1.0);
    offsets[7] = vec2(1.0, -1.0);
    offsets[8] = vec2(-1.0, 1.0);
    offsets[9] = vec2(2.0, 0.0);
    offsets[10] = vec2(-2.0, 0.0);
    offsets[11] = vec2(0.0, 2.0);
    offsets[12] = vec2(0.0, -2.0);

    float weights[13];
    weights[0] = 0.2;  // center
    weights[1] = 0.12; weights[2] = 0.12;  // horizontal
    weights[3] = 0.12; weights[4] = 0.12;  // vertical
    weights[5] = 0.06; weights[6] = 0.06;  // diagonal
    weights[7] = 0.06; weights[8] = 0.06;
    weights[9] = 0.02; weights[10] = 0.02;  // outer
    weights[11] = 0.02; weights[12] = 0.02;

    // Sample at scale 1
    for (int i = 0; i < 13; i++) {
        vec2 sampleUV = uv + offsets[i] * texelSize * radius * scale1;
        vec3 color = texture(hdrInput, sampleUV).rgb;
        vec3 bright = extractBright(color, ubo.bloomThreshold);
        bloom += bright * weights[i] * weight1;
        totalWeight += weights[i] * weight1;
    }

    // Sample at scale 2
    for (int i = 0; i < 13; i++) {
        vec2 sampleUV = uv + offsets[i] * texelSize * radius * scale2;
        vec3 color = texture(hdrInput, sampleUV).rgb;
        vec3 bright = extractBright(color, ubo.bloomThreshold);
        bloom += bright * weights[i] * weight2;
        totalWeight += weights[i] * weight2;
    }

    // Sample at scale 3
    for (int i = 0; i < 13; i++) {
        vec2 sampleUV = uv + offsets[i] * texelSize * radius * scale3;
        vec3 color = texture(hdrInput, sampleUV).rgb;
        vec3 bright = extractBright(color, ubo.bloomThreshold);
        bloom += bright * weights[i] * weight3;
        totalWeight += weights[i] * weight3;
    }

    return bloom / totalWeight;
}

void main() {
    vec3 hdr = texture(hdrInput, fragTexCoord).rgb;

    float finalExposure = ubo.exposure;

    // Auto-exposure: compute target exposure from scene luminance
    if (ubo.autoExposure > 0.5) {
        float avgLum = computeAverageLuminance();

        // Target middle gray (0.18) for average luminance
        // exposure = log2(targetLum / avgLum)
        const float targetLum = 0.18;
        float targetExp = log2(targetLum / max(avgLum, 0.001));

        // Clamp to reasonable range
        finalExposure = clamp(targetExp, -4.0, 4.0);
    }

    // Compute bloom
    vec3 bloom = vec3(0.0);
    if (ubo.bloomIntensity > 0.0) {
        bloom = sampleBloom(fragTexCoord, ubo.bloomRadius);
    }

    // Combine HDR with bloom
    vec3 combined = hdr + bloom * ubo.bloomIntensity;

    // Apply exposure
    vec3 exposed = combined * exp2(finalExposure);

    // Apply ACES tone mapping
    vec3 mapped = ACESFilmic(exposed);

    outColor = vec4(mapped, 1.0);
}
