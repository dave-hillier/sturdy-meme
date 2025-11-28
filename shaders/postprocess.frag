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
layout(binding = 4) uniform sampler2D bloomTexture;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Froxel grid constants (must match FroxelSystem.h)
const uint FROXEL_WIDTH = 128;
const uint FROXEL_HEIGHT = 64;
const uint FROXEL_DEPTH = 64;

// Linearize depth from NDC (Vulkan: 0-1 range)
float linearizeDepth(float depth) {
    return ubo.nearPlane * ubo.farPlane / (ubo.farPlane - depth * (ubo.farPlane - ubo.nearPlane));
}

// ============================================================================
// Tricubic B-Spline Filtering (Phase 4.3.7)
// Provides smoother fog gradients than trilinear filtering
// ============================================================================

// Cubic B-spline weight function
// Returns (w0, w1, w2, w3) weights and optimized texture offsets
vec4 bsplineWeights(float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    float invT = 1.0 - t;
    float invT2 = invT * invT;
    float invT3 = invT2 * invT;

    // Cubic B-spline basis functions
    float w0 = invT3 / 6.0;
    float w1 = (4.0 - 6.0 * t2 + 3.0 * t3) / 6.0;
    float w2 = (1.0 + 3.0 * t + 3.0 * t2 - 3.0 * t3) / 6.0;
    float w3 = t3 / 6.0;

    return vec4(w0, w1, w2, w3);
}

// Optimized tricubic using 8 bilinear samples instead of 64 point samples
// Based on GPU Gems 2, Chapter 20: Fast Third-Order Texture Filtering
vec4 sampleFroxelTricubic(vec3 uvw) {
    vec3 texSize = vec3(float(FROXEL_WIDTH), float(FROXEL_HEIGHT), float(FROXEL_DEPTH));
    vec3 invTexSize = 1.0 / texSize;

    // Convert to texel coordinates
    vec3 texCoord = uvw * texSize - 0.5;
    vec3 texCoordFloor = floor(texCoord);
    vec3 frac = texCoord - texCoordFloor;

    // Calculate B-spline weights for each axis
    vec4 xWeights = bsplineWeights(frac.x);
    vec4 yWeights = bsplineWeights(frac.y);
    vec4 zWeights = bsplineWeights(frac.z);

    // Combine adjacent weights for bilinear optimization
    // g0 = w0 + w1, g1 = w2 + w3
    vec2 gX = vec2(xWeights.x + xWeights.y, xWeights.z + xWeights.w);
    vec2 gY = vec2(yWeights.x + yWeights.y, yWeights.z + yWeights.w);
    vec2 gZ = vec2(zWeights.x + zWeights.y, zWeights.z + zWeights.w);

    // Calculate bilinear sample offsets
    // h0 = w1 / g0 - 1, h1 = w3 / g1 + 1
    vec2 hX = vec2(xWeights.y / gX.x - 1.0, xWeights.w / gX.y + 1.0);
    vec2 hY = vec2(yWeights.y / gY.x - 1.0, yWeights.w / gY.y + 1.0);
    vec2 hZ = vec2(zWeights.y / gZ.x - 1.0, zWeights.w / gZ.y + 1.0);

    // Base texel position
    vec3 baseUV = (texCoordFloor + 0.5) * invTexSize;

    // Sample 8 bilinear taps (2x2x2 grid)
    vec4 c000 = texture(froxelVolume, baseUV + vec3(hX.x, hY.x, hZ.x) * invTexSize);
    vec4 c100 = texture(froxelVolume, baseUV + vec3(hX.y, hY.x, hZ.x) * invTexSize);
    vec4 c010 = texture(froxelVolume, baseUV + vec3(hX.x, hY.y, hZ.x) * invTexSize);
    vec4 c110 = texture(froxelVolume, baseUV + vec3(hX.y, hY.y, hZ.x) * invTexSize);
    vec4 c001 = texture(froxelVolume, baseUV + vec3(hX.x, hY.x, hZ.y) * invTexSize);
    vec4 c101 = texture(froxelVolume, baseUV + vec3(hX.y, hY.x, hZ.y) * invTexSize);
    vec4 c011 = texture(froxelVolume, baseUV + vec3(hX.x, hY.y, hZ.y) * invTexSize);
    vec4 c111 = texture(froxelVolume, baseUV + vec3(hX.y, hY.y, hZ.y) * invTexSize);

    // Blend in X
    vec4 c00 = mix(c000 * gX.x, c100 * gX.y, 0.5) * 2.0;  // Normalized blend
    vec4 c10 = mix(c010 * gX.x, c110 * gX.y, 0.5) * 2.0;
    vec4 c01 = mix(c001 * gX.x, c101 * gX.y, 0.5) * 2.0;
    vec4 c11 = mix(c011 * gX.x, c111 * gX.y, 0.5) * 2.0;

    // Proper weighted blend
    c00 = c000 * gX.x + c100 * gX.y;
    c10 = c010 * gX.x + c110 * gX.y;
    c01 = c001 * gX.x + c101 * gX.y;
    c11 = c011 * gX.x + c111 * gX.y;

    // Blend in Y
    vec4 c0 = c00 * gY.x + c10 * gY.y;
    vec4 c1 = c01 * gY.x + c11 * gY.y;

    // Blend in Z
    return c0 * gZ.x + c1 * gZ.y;
}

// Convert linear depth to froxel slice index
float depthToSlice(float linearDepth) {
    float normalized = linearDepth / ubo.froxelFarPlane;
    normalized = clamp(normalized, 0.0, 1.0);
    return log(1.0 + normalized * (pow(ubo.froxelDepthDist, float(FROXEL_DEPTH)) - 1.0)) /
           log(ubo.froxelDepthDist);
}

// Sample froxel volume for volumetric fog (Phase 4.3)
// Uses tricubic B-spline filtering for smoother gradients
vec4 sampleFroxelFog(vec2 uv, float linearDepth) {
    // Clamp to volumetric range
    float clampedDepth = min(linearDepth, ubo.froxelFarPlane);

    // Convert to froxel UVW coordinates
    float sliceIndex = depthToSlice(clampedDepth);
    float w = sliceIndex / float(FROXEL_DEPTH);

    // Sample with tricubic B-spline filtering for smoother fog gradients
    vec4 fogData = sampleFroxelTricubic(vec3(uv, w));

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

    // Sample bloom from multi-pass bloom texture
    vec3 bloom = texture(bloomTexture, fragTexCoord).rgb;

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
