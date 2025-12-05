/*
 * fbm_common.glsl - Fractional Brownian Motion utilities with LOD support
 *
 * Based on Far Cry 5's water rendering approach:
 * - 9 octaves close to camera (high frequency detail)
 * - 3 octaves in distance (preserve reflection quality, save perf)
 * - Smooth LOD transition to avoid popping
 *
 * Never go to 0 octaves because reflections look bad in the distance.
 */

// High-quality noise function for FBM
float fbmNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);

    // Quintic interpolation for smoother results
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    // Hash function for corner values
    float a = fract(sin(dot(i, vec2(127.1, 311.7))) * 43758.5453);
    float b = fract(sin(dot(i + vec2(1.0, 0.0), vec2(127.1, 311.7))) * 43758.5453);
    float c = fract(sin(dot(i + vec2(0.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);
    float d = fract(sin(dot(i + vec2(1.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Standard FBM with fixed octave count
float fbmFixed(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * fbmNoise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

// LOD-aware FBM with smooth octave transition
// distanceFactor: 0.0 = close to camera (max octaves), 1.0 = far (min octaves)
// Returns value in [0, 1] range
float fbmLOD(vec2 p, float distanceFactor, int minOctaves, int maxOctaves) {
    // Interpolate octave count based on distance
    float octaveFloat = mix(float(maxOctaves), float(minOctaves), distanceFactor);
    int octaveCount = int(floor(octaveFloat));
    float octaveFrac = fract(octaveFloat);

    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    float totalAmplitude = 0.0;

    // Compute full octaves
    for (int i = 0; i < octaveCount; i++) {
        value += amplitude * fbmNoise(p * frequency);
        totalAmplitude += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    // Add partial octave for smooth transition
    if (octaveCount < maxOctaves && octaveFrac > 0.0) {
        value += amplitude * fbmNoise(p * frequency) * (1.0 - octaveFrac);
        totalAmplitude += amplitude * (1.0 - octaveFrac);
    }

    // Normalize to maintain consistent amplitude
    return value / max(totalAmplitude, 0.001);
}

// Calculate distance-based LOD factor
// Returns 0.0 when close (use max detail), 1.0 when far (use min detail)
float calculateFBMLODFactor(float viewDistance, float nearDistance, float farDistance) {
    return clamp((viewDistance - nearDistance) / (farDistance - nearDistance), 0.0, 1.0);
}

// Convenience function: LOD FBM for water surface detail
// viewDistance: distance from camera to fragment
// nearDist: distance where max detail is used (e.g., 50m)
// farDist: distance where min detail is used (e.g., 500m)
float fbmWaterDetail(vec2 uv, float viewDistance, float nearDist, float farDist) {
    float lodFactor = calculateFBMLODFactor(viewDistance, nearDist, farDist);
    // 9 octaves near, 3 octaves far (per Far Cry 5 talk)
    return fbmLOD(uv, lodFactor, 3, 9);
}

// Multi-scale FBM that combines multiple frequency bands
// Useful for water surface with both large swells and fine ripples
struct FBMResult {
    float lowFreq;   // Large-scale variation (swells)
    float midFreq;   // Medium-scale waves
    float highFreq;  // Fine ripples (only visible close up)
    float combined;  // Weighted combination
};

FBMResult fbmMultiScale(vec2 uv, float viewDistance, float nearDist, float farDist) {
    FBMResult result;
    float lodFactor = calculateFBMLODFactor(viewDistance, nearDist, farDist);

    // Low frequency: always 2-3 octaves (visible at all distances)
    result.lowFreq = fbmFixed(uv * 0.1, 3);

    // Mid frequency: 3-5 octaves
    result.midFreq = fbmLOD(uv * 0.5, lodFactor, 3, 5);

    // High frequency: 3-9 octaves (fades with distance)
    result.highFreq = fbmLOD(uv * 2.0, lodFactor, 0, 4);

    // Blend high frequency out at distance
    float highFreqWeight = 1.0 - lodFactor;
    result.combined = result.lowFreq * 0.4 +
                      result.midFreq * 0.4 +
                      result.highFreq * 0.2 * highFreqWeight;

    return result;
}

// Derivative-based normal from FBM (avoids extra samples)
// Returns normal perturbation in tangent space
vec3 fbmNormal(vec2 uv, float viewDistance, float nearDist, float farDist, float strength) {
    float lodFactor = calculateFBMLODFactor(viewDistance, nearDist, farDist);
    int octaves = int(mix(9.0, 3.0, lodFactor));

    // Sample FBM at offset positions for gradient
    float eps = 0.01 * (1.0 + lodFactor * 2.0); // Larger epsilon at distance

    float h = fbmFixed(uv, octaves);
    float hx = fbmFixed(uv + vec2(eps, 0.0), octaves);
    float hy = fbmFixed(uv + vec2(0.0, eps), octaves);

    // Compute gradient
    float dx = (hx - h) / eps;
    float dy = (hy - h) / eps;

    // Return normal perturbation
    return normalize(vec3(-dx * strength, 1.0, -dy * strength));
}
