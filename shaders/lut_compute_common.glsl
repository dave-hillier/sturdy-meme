// LUT Compute Shader Common Functions
// Shared utilities for atmospheric LUT compute shaders:
// - transmittance_lut.comp
// - skyview_lut.comp
// - irradiance_lut.comp
// - multiscatter_lut.comp

#ifndef LUT_COMPUTE_COMMON_GLSL
#define LUT_COMPUTE_COMMON_GLSL

// ============================================================================
// Bounds Checking
// ============================================================================

// Check if texel is within LUT bounds, returns early if out of bounds
// Usage: if (isOutOfBounds(texel, lutSize)) return;
bool isOutOfBounds(ivec2 texel, ivec2 lutSize) {
    return texel.x >= lutSize.x || texel.y >= lutSize.y;
}

// ============================================================================
// UV Normalization
// ============================================================================

// Convert texel coordinates to normalized UV [0, 1] with half-pixel offset
vec2 texelToUV(ivec2 texel, ivec2 lutSize) {
    return (vec2(texel) + 0.5) / vec2(lutSize);
}

// ============================================================================
// LUT Parameter Decoding
// ============================================================================

// Decode altitude and sun zenith angle from UV for irradiance/multiscatter LUTs
// UV mapping:
//   X: cosine of sun zenith angle [-1, 1] mapped from [0, 1]
//   Y: normalized altitude [0, 1] over atmosphere height
void decodeAltitudeSunAngleLUT(vec2 uv, AtmosphereParams params,
                                out float altitude, out float cosSunZenith) {
    cosSunZenith = uv.x * 2.0 - 1.0;
    altitude = uv.y * (params.atmosphereRadius - params.planetRadius);
}

// ============================================================================
// Transmittance LUT Sampling
// ============================================================================

// Sample transmittance LUT given altitude (r) and cosine of zenith angle (mu)
// This is a parameterized version - pass the sampler explicitly
vec3 sampleTransmittanceLUT(sampler2D transmittanceLUT, float r, float mu, AtmosphereParams params) {
    vec2 uv = TransmittanceLUTParamsToUV(r, mu, params);
    return texture(transmittanceLUT, uv).rgb;
}

// ============================================================================
// Extinction Calculation
// ============================================================================

// Compute total extinction coefficients from atmospheric density
// Returns vec3 extinction that can be used for Beer-Lambert law
vec3 computeExtinction(vec3 density, AtmosphereParams params) {
    vec3 rayleighExtinction = density.x * params.rayleighScatteringBase;
    float mieExtinction = density.y * (params.mieScatteringBase + params.mieAbsorptionBase);
    vec3 ozoneExtinction = density.z * params.ozoneAbsorption;
    return rayleighExtinction + vec3(mieExtinction) + ozoneExtinction;
}

// Compute scattering coefficients (without absorption, for in-scattering)
void computeScattering(vec3 density, AtmosphereParams params,
                       out vec3 rayleighScatter, out vec3 mieScatter) {
    rayleighScatter = density.x * params.rayleighScatteringBase;
    mieScatter = vec3(density.y * params.mieScatteringBase);
}

// ============================================================================
// Ray Marching Helpers
// ============================================================================

// Compute position along ray at step i with midpoint sampling
// Returns distance t from ray origin
float rayMarchStepT(int stepIndex, float stepSize) {
    return (float(stepIndex) + 0.5) * stepSize;
}

// Calculate altitude and radius at a point along a ray
// rayOrigin is typically vec3(0, r, 0) where r is planet radius + camera altitude
// Returns radius from planet center
float computeSampleRadius(vec3 rayOrigin, vec3 rayDir, float t) {
    vec3 pos = rayOrigin + rayDir * t;
    return length(pos);
}

// Calculate altitude from radius
float radiusToAltitude(float r, float planetRadius) {
    return max(r - planetRadius, 0.0);
}

// Combined helper: get altitude at ray march sample point
float getSampleAltitude(vec3 rayOrigin, vec3 rayDir, float t, float planetRadius) {
    float r = computeSampleRadius(rayOrigin, rayDir, t);
    return radiusToAltitude(r, planetRadius);
}

// ============================================================================
// Transmittance Computation
// ============================================================================

// Compute optical depth along a ray segment using trapezoidal integration
// This is the core integration used in transmittance_lut.comp
// Returns optical depth (use exp(-opticalDepth) for transmittance)
vec3 integrateOpticalDepth(float r, float mu, float distance, int steps, AtmosphereParams params) {
    float dx = distance / float(steps);
    vec3 opticalDepth = vec3(0.0);

    for (int i = 0; i <= steps; i++) {
        float t = float(i) * dx;

        // Current distance from planet center
        float ri = sqrt(r * r + t * t + 2.0 * r * mu * t);
        float altitude = ri - params.planetRadius;

        // Get density and extinction at this point
        vec3 density = GetAtmosphereDensity(altitude, params);
        vec3 extinction = computeExtinction(density, params);

        // Trapezoidal rule weights
        float weight = (i == 0 || i == steps) ? 0.5 : 1.0;
        opticalDepth += extinction * weight * dx;
    }

    return opticalDepth;
}

// ============================================================================
// Sun Direction Construction
// ============================================================================

// Construct sun direction vector from cosine of zenith angle
// Assumes sun is in XY plane (azimuth = 0)
vec3 sunDirFromZenithCos(float cosSunZenith) {
    float sinSunZenith = sqrt(max(0.0, 1.0 - cosSunZenith * cosSunZenith));
    return vec3(sinSunZenith, cosSunZenith, 0.0);
}

#endif // LUT_COMPUTE_COMMON_GLSL
