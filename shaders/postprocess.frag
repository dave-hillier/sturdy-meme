#version 450

layout(binding = 0) uniform sampler2D hdrInput;

layout(binding = 1) uniform PostProcessUniforms {
    vec4 exposureParams;    // x: manual exposure, y: auto flag, z: previous exposure, w: delta time
    vec4 histogramParams;   // x: adaptation speed, y: low percentile, z: high percentile, w: exposure bias
    vec4 bloomParams;       // x: threshold, y: intensity, z: radius, w: unused
    vec4 colorLift;         // xyz: lift
    vec4 colorGamma;        // xyz: gamma, w: saturation
    vec4 colorGain;         // xyz: gain, w: Purkinje strength
} ubo;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const int BIN_COUNT = 16;
const float TARGET_LUMINANCE = 0.18;
const float LOG_LUM_MIN = -10.0;
const float LOG_LUM_MAX = 6.0;

vec2 samplePattern[16] = vec2[](
    vec2(-0.75, -0.25), vec2(-0.25, -0.75), vec2(0.25, -0.25), vec2(0.75, -0.75),
    vec2(-0.75, 0.25),  vec2(-0.25, 0.75),  vec2(0.25, 0.25),  vec2(0.75, 0.75),
    vec2(-0.5, -0.5),   vec2(0.5, -0.5),    vec2(-0.5, 0.5),   vec2(0.5, 0.5),
    vec2(0.0, -1.0),    vec2(-1.0, 0.0),    vec2(1.0, 0.0),    vec2(0.0, 1.0)
);

// ACES Filmic Tone Mapping
vec3 ACESFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float getLuminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

float binToLuminance(int bin) {
    float t = (float(bin) + 0.5) / float(BIN_COUNT);
    return exp2(mix(LOG_LUM_MIN, LOG_LUM_MAX, t));
}

float buildHistogram(out float histogram[BIN_COUNT], out float averageLum) {
    for (int i = 0; i < BIN_COUNT; i++) {
        histogram[i] = 0.0;
    }

    float totalWeight = 0.0;
    float logLumSum = 0.0;

    // Slight jitter to avoid visible banding in adaptation
    vec2 jitter = fract(fragTexCoord.yx * vec2(37.0, 17.0)) - 0.5;

    for (int i = 0; i < 16; i++) {
        vec2 uv = clamp(samplePattern[i] * 0.35 + 0.5 + jitter * 0.02, 0.0, 1.0);
        vec3 color = texture(hdrInput, uv).rgb;
        float lum = clamp(getLuminance(color), 0.0001, 64.0);
        float logLum = log2(lum);
        float bin = clamp((logLum - LOG_LUM_MIN) / (LOG_LUM_MAX - LOG_LUM_MIN), 0.0, 0.999) * float(BIN_COUNT);
        int idx = int(bin);
        histogram[idx] += 1.0;
        totalWeight += 1.0;
        logLumSum += logLum;
    }

    averageLum = exp2(logLumSum / max(totalWeight, 1.0));
    return totalWeight;
}

float percentileFromHistogram(float histogram[BIN_COUNT], float totalWeight, float percentile) {
    float target = clamp(percentile, 0.0, 1.0) * totalWeight;
    float accum = 0.0;
    for (int i = 0; i < BIN_COUNT; i++) {
        accum += histogram[i];
        if (accum >= target) {
            return binToLuminance(i);
        }
    }
    return binToLuminance(BIN_COUNT / 2);
}

vec3 extractBright(vec3 color, float threshold) {
    float brightness = getLuminance(color);
    float knee = threshold * 0.5;
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.0001);
    float contribution = max(soft, brightness - threshold) / max(brightness, 0.0001);
    return color * max(contribution, 0.0);
}

vec3 bloomBlur(vec2 uv, vec2 texelSize, float radius) {
    vec2 offsets[9] = vec2[](
        vec2(-1.0, -1.0), vec2(0.0, -1.0), vec2(1.0, -1.0),
        vec2(-1.0, 0.0),  vec2(0.0, 0.0),  vec2(1.0, 0.0),
        vec2(-1.0, 1.0),  vec2(0.0, 1.0),  vec2(1.0, 1.0)
    );
    float weights[9] = float[](0.05, 0.09, 0.05, 0.09, 0.16, 0.09, 0.05, 0.09, 0.05);

    vec3 accum = vec3(0.0);
    float total = 0.0;
    for (int i = 0; i < 9; i++) {
        vec2 sampleUV = uv + offsets[i] * texelSize * radius;
        vec3 sampleColor = texture(hdrInput, sampleUV).rgb;
        vec3 bright = extractBright(sampleColor, ubo.bloomParams.x);
        accum += bright * weights[i];
        total += weights[i];
    }
    return accum / max(total, 0.0001);
}

vec3 bloomChain(vec2 uv) {
    vec2 texelSize = 1.0 / vec2(textureSize(hdrInput, 0));

    // Downsample and blur at progressively larger footprints
    vec3 level0 = bloomBlur(uv, texelSize, ubo.bloomParams.z * 0.5);
    vec3 level1 = bloomBlur(uv, texelSize * 2.0, ubo.bloomParams.z * 1.0);
    vec3 level2 = bloomBlur(uv, texelSize * 4.0, ubo.bloomParams.z * 1.5);
    vec3 level3 = bloomBlur(uv, texelSize * 8.0, ubo.bloomParams.z * 2.5);

    // Upsample and accumulate
    vec3 up2 = level3;
    vec3 up1 = level2 + up2 * 0.6;
    vec3 up0 = level1 + up1 * 0.6;

    vec3 bloom = level0 + up0 * 0.6;
    return bloom * ubo.bloomParams.y;
}

vec3 applyColorGrading(vec3 color, float nightFactor) {
    vec3 graded = color + ubo.colorLift.xyz;
    graded = pow(max(graded, vec3(0.0001)), ubo.colorGamma.xyz);
    graded *= ubo.colorGain.xyz;

    float luma = dot(graded, vec3(0.2126, 0.7152, 0.0722));
    graded = mix(vec3(luma), graded, ubo.colorGamma.w);

    // Purkinje effect: favor blue channel in low light
    vec3 mesopicTarget = vec3(graded.r * 0.7, graded.g * 0.9, graded.b * 1.2);
    graded = mix(graded, mesopicTarget, ubo.colorGain.w * nightFactor);
    return graded;
}

void main() {
    vec3 hdr = texture(hdrInput, fragTexCoord).rgb;

    float histogram[BIN_COUNT];
    float averageLum;
    float totalWeight = buildHistogram(histogram, averageLum);

    float darkLum = percentileFromHistogram(histogram, totalWeight, ubo.histogramParams.y);
    float brightLum = percentileFromHistogram(histogram, totalWeight, ubo.histogramParams.z);
    float targetLum = mix(darkLum, brightLum, 0.5);

    float targetExposure = log2(TARGET_LUMINANCE / max(targetLum, 0.0001)) + ubo.histogramParams.w;
    targetExposure = clamp(targetExposure, -8.0, 8.0);

    float adaptRate = 1.0 - exp(-ubo.histogramParams.x * ubo.exposureParams.w);
    float finalExposure = mix(ubo.exposureParams.x, targetExposure, ubo.exposureParams.y);
    finalExposure = mix(ubo.exposureParams.z, finalExposure, adaptRate);

    float nightFactor = 1.0 - smoothstep(0.05, 0.25, averageLum);

    vec3 bloom = (ubo.bloomParams.y > 0.0) ? bloomChain(fragTexCoord) : vec3(0.0);
    vec3 combined = hdr + bloom;

    vec3 exposed = combined * exp2(finalExposure);
    vec3 mapped = ACESFilmic(exposed);

    vec3 graded = applyColorGrading(mapped, nightFactor);
    outColor = vec4(graded, 1.0);
}
