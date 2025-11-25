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

// Poisson disc bloom - samples spread energy smoothly
vec3 bloomMultiTap(vec2 uv, float radiusPixels) {
    const vec2 poisson[12] = vec2[](
        vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457),
        vec2(-0.203, 0.621),  vec2(0.962, -0.195), vec2(0.473, -0.480),
        vec2(0.519, 0.767),   vec2(0.185, -0.893), vec2(0.507, 0.064),
        vec2(0.896, 0.412),   vec2(-0.322, -0.933), vec2(-0.792, -0.598)
    );

    vec2 texelSize = radiusPixels / vec2(textureSize(hdrInput, 0));

    vec3 center = extractBright(texture(hdrInput, uv).rgb, ubo.bloomThreshold);
    vec3 accum = center * 0.35;
    float weightSum = 0.35;

    for (int i = 0; i < 12; i++) {
        vec2 offset = poisson[i] * texelSize;
        float w = exp(-dot(poisson[i], poisson[i]) * 2.5);
        vec3 bright = extractBright(texture(hdrInput, uv + offset).rgb, ubo.bloomThreshold);
        accum += bright * w;
        weightSum += w;
    }

    return accum / max(weightSum, 0.0001);
}

// Chain multiple radii for smooth falloff
vec3 sampleBloom(vec2 uv, float radius) {
    float baseRadius = max(radius, 0.5);

    // Blend a few radii to build a soft falloff without large spaced kernels
    vec3 small = bloomMultiTap(uv, baseRadius * 0.6);
    vec3 medium = bloomMultiTap(uv, baseRadius);
    vec3 large = bloomMultiTap(uv, baseRadius * 1.8);

    return small * 0.4 + medium * 0.35 + large * 0.25;
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
