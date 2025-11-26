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
    // God rays parameters (Phase 4.4)
    vec2 sunScreenPos;     // Sun position in screen space [0,1]
    float godRayIntensity; // God ray strength
    float godRayDecay;     // Falloff per sample
    // Froxel volumetrics (Phase 4.3)
    float froxelEnabled;   // 1.0 = enabled
    float froxelFarPlane;  // Volumetric far plane
    float froxelDepthDist; // Depth distribution factor
    float nearPlane;       // Camera near plane
    float farPlane;        // Camera far plane
    float padding1;
    float padding2;
    float padding3;
} ubo;

layout(binding = 2) uniform sampler2D depthInput;
layout(binding = 3) uniform sampler3D froxelVolume;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Froxel grid constants (must match FroxelSystem.h)
const uint FROXEL_DEPTH = 64;

// Linearize depth from NDC (Vulkan: 0-1 range)
float linearizeDepth(float depth) {
    return ubo.nearPlane * ubo.farPlane / (ubo.farPlane - depth * (ubo.farPlane - ubo.nearPlane));
}

// Convert linear depth to froxel slice index
float depthToSlice(float linearDepth) {
    float normalized = linearDepth / ubo.froxelFarPlane;
    normalized = clamp(normalized, 0.0, 1.0);
    return log(1.0 + normalized * (pow(ubo.froxelDepthDist, float(FROXEL_DEPTH)) - 1.0)) /
           log(ubo.froxelDepthDist);
}

// Sample froxel volume for volumetric fog (Phase 4.3)
vec4 sampleFroxelFog(vec2 uv, float linearDepth) {
    // Clamp to volumetric range
    float clampedDepth = min(linearDepth, ubo.froxelFarPlane);

    // Convert to froxel UVW coordinates
    float sliceIndex = depthToSlice(clampedDepth);
    float w = sliceIndex / float(FROXEL_DEPTH);

    // Sample with trilinear filtering
    vec4 fogData = texture(froxelVolume, vec3(uv, w));

    // fogData format: RGB = L/alpha (normalized scatter), A = alpha
    // Recover actual scattering: L = (L/alpha) * alpha
    vec3 inScatter = fogData.rgb * fogData.a;
    float transmittance = 1.0 - fogData.a;

    return vec4(inScatter, transmittance);
}

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

// God rays / Light shafts (Phase 4.4)
// Screen-space radial blur from sun position
const int GOD_RAY_SAMPLES = 64;

vec3 computeGodRays(vec2 uv, vec2 sunPos) {
    // Only process if sun is roughly on screen
    if (sunPos.x < -0.5 || sunPos.x > 1.5 || sunPos.y < -0.5 || sunPos.y > 1.5) {
        return vec3(0.0);
    }

    // Direction from pixel to sun
    vec2 delta = (sunPos - uv) / float(GOD_RAY_SAMPLES);

    // Initial sample weight
    float illumination = 0.0;
    float weight = 1.0;
    vec2 sampleUV = uv;

    // Sky depth threshold - pixels at or near far plane are sky
    const float SKY_DEPTH_THRESHOLD = 0.9999;

    // Accumulate samples along ray toward sun
    for (int i = 0; i < GOD_RAY_SAMPLES; i++) {
        sampleUV += delta;

        // Check bounds
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            break;
        }

        // Only accumulate from sky pixels (not geometry like point lights)
        float sampleDepth = texture(depthInput, sampleUV).r;
        if (sampleDepth < SKY_DEPTH_THRESHOLD) {
            // This is geometry, not sky - skip it
            weight *= ubo.godRayDecay;
            continue;
        }

        // Sample brightness at this point
        vec3 sampleColor = texture(hdrInput, sampleUV).rgb;
        float brightness = getLuminance(sampleColor);

        // Only accumulate from bright sky pixels (threshold at bloom level)
        if (brightness > ubo.bloomThreshold * 0.5) {
            illumination += brightness * weight;
        }

        // Exponential decay
        weight *= ubo.godRayDecay;
    }

    // Normalize and scale
    illumination /= float(GOD_RAY_SAMPLES);

    // Fade based on distance from sun
    float distFromSun = length(uv - sunPos);
    float radialFalloff = 1.0 - clamp(distFromSun * 1.5, 0.0, 1.0);
    radialFalloff *= radialFalloff;  // Squared falloff

    // Return warm-tinted god rays
    vec3 godRayColor = vec3(1.0, 0.95, 0.8);  // Slight warm tint
    return godRayColor * illumination * radialFalloff * ubo.godRayIntensity;
}

void main() {
    vec3 hdr = texture(hdrInput, fragTexCoord).rgb;

    // Apply froxel volumetric fog (Phase 4.3)
    if (ubo.froxelEnabled > 0.5) {
        float depth = texture(depthInput, fragTexCoord).r;
        float linearDepth = linearizeDepth(depth);

        vec4 fog = sampleFroxelFog(fragTexCoord, linearDepth);
        vec3 inScatter = fog.rgb;
        float transmittance = fog.a;

        // Apply fog: scene * transmittance + in-scatter
        hdr = hdr * transmittance + inScatter;
    }

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

    // Compute god rays (Phase 4.4)
    vec3 godRays = vec3(0.0);
    if (ubo.godRayIntensity > 0.0) {
        godRays = computeGodRays(fragTexCoord, ubo.sunScreenPos);
    }

    // Combine HDR with bloom and god rays
    vec3 combined = hdr + bloom * ubo.bloomIntensity + godRays;

    // Apply exposure
    vec3 exposed = combined * exp2(finalExposure);

    // Apply ACES tone mapping
    vec3 mapped = ACESFilmic(exposed);

    outColor = vec4(mapped, 1.0);
}
