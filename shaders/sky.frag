#version 450

#extension GL_GOOGLE_include_directive : require

const float PI = 3.14159265359;
const int NUM_CASCADES = 4;

// LUT dimensions (must match AtmosphereLUTSystem)
const int TRANSMITTANCE_WIDTH = 256;
const int TRANSMITTANCE_HEIGHT = 64;

#include "bindings.glsl"
#include "ubo_common.glsl"

// Atmosphere LUTs (Phase 4.1 - precomputed for efficiency)
layout(binding = BINDING_SKY_TRANSMITTANCE_LUT) uniform sampler2D transmittanceLUT;  // 256x64, RGBA16F
layout(binding = BINDING_SKY_MULTISCATTER_LUT) uniform sampler2D multiScatterLUT;   // 32x32, RG16F
layout(binding = BINDING_SKY_SKYVIEW_LUT) uniform sampler2D skyViewLUT;        // 192x108, RGBA16F (updated per-frame)
// Irradiance LUTs for cloud/haze lighting (Phase 4.1.9)
layout(binding = BINDING_SKY_RAYLEIGH_IRR_LUT) uniform sampler2D rayleighIrradianceLUT;  // 64x16, RGBA16F
layout(binding = BINDING_SKY_MIE_IRR_LUT) uniform sampler2D mieIrradianceLUT;       // 64x16, RGBA16F
// Cloud Map LUT (Paraboloid projection, updated per-frame with wind animation)
layout(binding = BINDING_SKY_CLOUDMAP_LUT) uniform sampler2D cloudMapLUT;            // 256x256, RGBA16F

layout(location = 0) in vec3 rayDir;
layout(location = 0) out vec4 outColor;

// Atmospheric parameters (values from Phase 4 docs, scaled to kilometers)
const float PLANET_RADIUS = 6371.0;           // Earth radius in km
const float ATMOSPHERE_RADIUS = 6471.0;       // Top of atmosphere in km

const vec3 RAYLEIGH_SCATTERING_BASE = vec3(5.802e-3, 13.558e-3, 33.1e-3);
const float RAYLEIGH_SCALE_HEIGHT = 8.0;      // km

const float MIE_SCATTERING_BASE = 3.996e-3;
const float MIE_ABSORPTION_BASE = 4.4e-3;
const float MIE_SCALE_HEIGHT = 1.2;
const float MIE_ANISOTROPY = 0.8;

const vec3 OZONE_ABSORPTION = vec3(0.65e-3, 1.881e-3, 0.085e-3);
const float OZONE_LAYER_CENTER = 25.0;        // km
const float OZONE_LAYER_WIDTH = 15.0;

const float SUN_ANGULAR_RADIUS = 0.00935 / 2.0;  // radians (produces ~180px disc)
// Moon should be same apparent size as sun (both ~0.5 degrees in reality)
// The celestialDisc function produces a visual disc of radius acos(1.0 - size) radians
// For size = SUN_ANGULAR_RADIUS, visual radius ≈ 0.097 radians ≈ 5.5 degrees
const float MOON_DISC_SIZE = SUN_ANGULAR_RADIUS; // Same visual size as sun
const float MOON_MASK_RADIUS = 0.097;            // Visual disc radius for phase mask alignment

// LMS color space for accurate Rayleigh scattering (Phase 4.1.7)
// Standard Rec709 Rayleigh produces greenish sunsets; LMS primaries are more accurate
const mat3 RGB_TO_LMS = mat3(
    0.4122214708, 0.5363325363, 0.0514459929,
    0.2119034982, 0.6806995451, 0.1073969566,
    0.0883024619, 0.2817188376, 0.6299787005
);

const mat3 LMS_TO_RGB = mat3(
    4.0767416621, -3.3077115913, 0.2309699292,
   -1.2684380046,  2.6097574011, -0.3413193965,
   -0.0041960863, -0.7034186147, 1.7076147010
);

// Optimized Rayleigh coefficients for LMS space
const vec3 RAYLEIGH_LMS = vec3(6.95e-3, 12.28e-3, 28.44e-3);

// Solar irradiance at top of atmosphere (Phase 4.1 - physically-based values)
// These values represent the sun's spectral power and are essential for
// producing a bright blue sky during the day
const vec3 SOLAR_IRRADIANCE = vec3(1.474, 1.8504, 1.91198);

// Cloud parameters (Phase 4.2 - Volumetric Clouds)
const float CLOUD_LAYER_BOTTOM = 1.5;     // km above surface
const float CLOUD_LAYER_TOP = 4.0;        // km above surface
// CLOUD_COVERAGE and CLOUD_DENSITY now come from ubo.cloudCoverage and ubo.cloudDensity
const int CLOUD_MARCH_STEPS = 32;         // Ray march samples
const int CLOUD_LIGHT_STEPS = 6;          // Light sampling steps

// Sky-view LUT dimensions (must match AtmosphereLUTSystem)
const int SKYVIEW_WIDTH = 192;
const int SKYVIEW_HEIGHT = 108;

// Irradiance LUT dimensions (Phase 4.1.9)
const int IRRADIANCE_WIDTH = 64;
const int IRRADIANCE_HEIGHT = 16;

// Convert view direction to sky-view LUT UV coordinates
// This is the inverse of SkyViewUVToDirection in skyview_lut.comp
vec2 directionToSkyViewUV(vec3 dir) {
    dir = normalize(dir);

    // Extract elevation (theta) from Y component
    float sinTheta = dir.y;
    float theta = asin(clamp(sinTheta, -1.0, 1.0));

    // Inverse of non-linear mapping: theta = sign(v) * (PI/2) * v^2
    // Solve for v: v = sign(theta) * sqrt(abs(theta) / (PI/2))
    float absTheta = abs(theta);
    float v = sign(theta) * sqrt(absTheta / (PI / 2.0));
    float uvY = v * 0.5 + 0.5;  // Map [-1, 1] back to [0, 1]

    // Extract azimuth (phi) from XZ components
    // skyview_lut.comp uses: phi = (uv.x - 0.5) * 2.0 * PI
    // So the inverse is: uv.x = phi / (2.0 * PI) + 0.5
    float phi = atan(dir.z, dir.x);  // Range [-PI, PI]
    float uvX = phi / (2.0 * PI) + 0.5;  // Map [-PI, PI] to [0, 1]

    return vec2(uvX, uvY);
}

// Sample sky-view LUT for precomputed atmospheric scattering
vec3 sampleSkyViewLUT(vec3 viewDir) {
    vec2 uv = directionToSkyViewUV(viewDir);
    return texture(skyViewLUT, uv).rgb;
}

float hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

// 3D value noise for cloud shapes
float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);  // Smoothstep

    return mix(
        mix(mix(hash(i + vec3(0, 0, 0)), hash(i + vec3(1, 0, 0)), f.x),
            mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
        mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
            mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y),
        f.z
    );
}

// Fractal Brownian Motion for cloud detail
float fbm(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    float maxValue = 0.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise3D(p * frequency);
        maxValue += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value / maxValue;
}

// Cloud height gradient (cumulus shape - rounded bottom, flat top)
float cloudHeightGradient(float heightFraction) {
    // Remap to create rounded cumulus shape
    float gradient = smoothstep(0.0, 0.2, heightFraction) *
                     smoothstep(1.0, 0.7, heightFraction);
    return gradient;
}

// ============================================================================
// Paraboloid Cloud Map Sampling
// ============================================================================

// Convert direction to paraboloid UV coordinates
// Maps upper hemisphere direction to [0,1]^2 UV space
vec2 directionToParaboloidUV(vec3 dir) {
    dir = normalize(dir);

    // For Y-up convention, use Y as the vertical component
    // Paraboloid projection: UV = 0.5 + (XZ / (1 + Y)) * 0.5
    float denom = 1.0 + max(dir.y, 0.001);  // Avoid division by zero at horizon
    float u = 0.5 + (dir.x / denom) * 0.5;
    float v = 0.5 + (dir.z / denom) * 0.5;

    return vec2(u, v);
}

// Sample cloud density from paraboloid cloud map LUT
// Returns: vec4(baseDensity, detailNoise, coverageMask, heightGradient)
vec4 sampleCloudMapLUT(vec3 dir) {
    // Smoothly fade clouds near horizon to match horizonBlend range
    // Use slightly elevated direction for sampling to avoid edge artifacts
    vec3 sampleDir = dir;
    sampleDir.y = max(sampleDir.y, 0.001);
    sampleDir = normalize(sampleDir);

    vec2 uv = directionToParaboloidUV(sampleDir);
    vec4 cloudData = texture(cloudMapLUT, uv);

    // Fade out smoothly as we approach/go below horizon (matches horizonBlend range)
    float horizonFade = smoothstep(-0.02, 0.02, dir.y);
    return cloudData * horizonFade;
}

// Sample cloud density at a point
// Cloud style is controlled by ubo.cloudStyle uniform:
// 0.0 = original procedural noise, 1.0 = paraboloid LUT hybrid

// Global ray direction for paraboloid cloud lookup (set by caller)
vec3 g_cloudRayDir = vec3(0.0, 1.0, 0.0);

float sampleCloudDensity(vec3 worldPos) {
    float altitude = length(worldPos) - PLANET_RADIUS;

    // Check if within cloud layer
    if (altitude < CLOUD_LAYER_BOTTOM || altitude > CLOUD_LAYER_TOP) {
        return 0.0;
    }

    // Height fraction within cloud layer
    float heightFraction = (altitude - CLOUD_LAYER_BOTTOM) /
                           (CLOUD_LAYER_TOP - CLOUD_LAYER_BOTTOM);

    // Cloud shape based on height
    float heightGradient = cloudHeightGradient(heightFraction);

    // Wind parameters (shared by both methods)
    vec2 windDir = ubo.windDirectionAndSpeed.xy;
    float windSpeed = ubo.windDirectionAndSpeed.z;
    float windTime = ubo.windDirectionAndSpeed.w;

    if (ubo.cloudStyle > 0.5) {
        // Paraboloid LUT hybrid approach: Use paraboloid LUT for large-scale coverage,
        // but add 3D procedural detail for volumetric structure

        // Get coverage from paraboloid map (indexed by view direction)
        vec4 cloudData = sampleCloudMapLUT(g_cloudRayDir);
        float coverage = cloudData.r;  // Large-scale cloud presence

        // Early out if no cloud coverage in this direction
        if (coverage < 0.01) {
            return 0.0;
        }

        // Add 3D procedural detail based on world position for volumetric structure
        // This gives depth and variation within the cloud volume

        // Slow down detail noise animation (0.02x speed for realistic cloud evolution)
        float detailTimeScale = 0.02;
        vec3 windOffset = vec3(windDir.x * windSpeed * windTime * detailTimeScale,
                               windTime * 0.002,  // Very slow vertical evolution
                               windDir.y * windSpeed * windTime * detailTimeScale);

        vec3 detailPos = worldPos * 0.8 + windOffset;
        float detailNoise = fbm(detailPos * 0.5, 2);  // 2 octaves for detail

        // Combine LUT coverage with 3D detail
        float density = coverage * heightGradient;
        density *= smoothstep(0.3, 0.7, detailNoise);  // Carve detail into cloud
        density -= (1.0 - detailNoise) * 0.15 * (1.0 - heightFraction);
        density = max(density, 0.0);

        return density * ubo.cloudDensity;
    } else {
        // Original procedural noise implementation
        // Wind offset for animation - driven by wind system
        vec3 windOffset = vec3(windDir.x * windSpeed * windTime,
                               windTime * 0.1,
                               windDir.y * windSpeed * windTime);

        vec3 samplePos = worldPos * 0.5 + windOffset;

        // Large-scale shape noise
        float baseNoise = fbm(samplePos * 0.25, 4);

        // Apply coverage with softer transition
        float coverageThreshold = 1.0 - ubo.cloudCoverage;
        float density = smoothstep(coverageThreshold, coverageThreshold + 0.35, baseNoise);

        density *= heightGradient;

        // Detail erosion
        float detailNoise = fbm(samplePos * 1.0 + vec3(100.0), 2);
        density -= detailNoise * 0.2 * (1.0 - heightFraction);
        density = max(density, 0.0);

        return density * ubo.cloudDensity;
    }
}

// Henyey-Greenstein phase function (normalized to 4π solid angle)
// Used for back-scatter approximation in clouds
float hgPhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

// Cornette-Shanks phase function for Mie scattering (normalized to 4π solid angle)
// More physically accurate than HG, includes polarization term (1+cos²θ)
// Used for forward-scatter to be consistent with atmospheric Mie phase
float cornetteShanksPhaseCloud(float cosTheta, float g) {
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float denom = 8.0 * PI * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

// Cloud phase function with depth-dependent scattering (Ghost of Tsushima technique)
// Uses Cornette-Shanks for forward scatter (consistent with atmospheric Mie)
// and Henyey-Greenstein for back scatter (multi-scattering approximation)
float cloudPhase(float cosTheta, float transmittanceToLight, float segmentTransmittance) {
    float opticalDepthFactor = transmittanceToLight * segmentTransmittance;

    // Lerp between back-scatter (dense) and forward-scatter (wispy)
    // Forward scattering dominates in wispy/thin areas, back-scatter in dense areas
    float gForward = 0.8;
    float gBack = -0.15;
    float g = mix(gBack, gForward, opticalDepthFactor);

    float phase;
    if (g >= 0.0) {
        // Forward scatter: use Cornette-Shanks for consistency with atmospheric Mie
        phase = cornetteShanksPhaseCloud(cosTheta, g);
    } else {
        // Back scatter: use HG with multi-scattering boost
        phase = hgPhase(cosTheta, -g);
        // Value of 2.16 from Ghost of Tsushima - simulating dense Mie layer with 0.9 albedo
        // This approximates multiple scattering in optically thick regions
        phase *= 2.16;
    }

    return phase;
}

// Sample light transmittance to sun through clouds (optimized)
float sampleCloudTransmittanceToSun(vec3 pos, vec3 sunDir) {
    float opticalDepth = 0.0;
    float stepSize = (CLOUD_LAYER_TOP - CLOUD_LAYER_BOTTOM) / float(CLOUD_LIGHT_STEPS);

    for (int i = 0; i < CLOUD_LIGHT_STEPS; i++) {
        float t = stepSize * (float(i) + 0.5);
        vec3 samplePos = pos + sunDir * t;

        // Quick altitude check before expensive density sample
        float alt = length(samplePos) - PLANET_RADIUS;
        if (alt < CLOUD_LAYER_BOTTOM || alt > CLOUD_LAYER_TOP) continue;

        float density = sampleCloudDensity(samplePos);
        opticalDepth += density * stepSize * 10.0;

        if (opticalDepth > 4.0) break;  // Early out when heavily shadowed
    }

    return exp(-opticalDepth);
}

vec2 raySphereIntersect(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return vec2(1e9, -1e9);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

// Ray-plane intersection for cloud layer bounds
vec2 intersectCloudLayer(vec3 origin, vec3 dir) {
    // Intersect with spherical shells at cloud layer boundaries
    vec2 bottomHit = raySphereIntersect(origin, dir, PLANET_RADIUS + CLOUD_LAYER_BOTTOM);
    vec2 topHit = raySphereIntersect(origin, dir, PLANET_RADIUS + CLOUD_LAYER_TOP);

    float tEnter = max(bottomHit.x, 0.0);
    float tExit = topHit.y;

    // Handle case where we're inside the layer
    if (bottomHit.x < 0.0 && bottomHit.y > 0.0) {
        tEnter = 0.0;
    }

    return vec2(tEnter, tExit);
}

// Cloud result structure for volumetric cloud rendering
struct CloudResult {
    vec3 scattering;
    float transmittance;
};

float rayleighPhase(float cosTheta) {
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

float cornetteShanksPhase(float cosTheta, float g) {
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float denom = 8.0 * PI * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

float ozoneDensity(float altitude) {
    // Use UBO params if available (non-zero), otherwise use defaults
    float ozoneCenter = ubo.atmosOzoneAbsorption.w > 0.01 ? ubo.atmosOzoneAbsorption.w : OZONE_LAYER_CENTER;
    float ozoneWidth = ubo.atmosOzoneWidth > 0.01 ? ubo.atmosOzoneWidth : OZONE_LAYER_WIDTH;
    float z = (altitude - ozoneCenter) / ozoneWidth;
    return exp(-0.5 * z * z);
}

// ============================================================================
// Transmittance LUT Sampling (Phase 4.1.3)
// ============================================================================

// Convert (r, mu) parameters to transmittance LUT UV coordinates
// r = distance from planet center (in km)
// mu = cosine of zenith angle (dot(up, direction))
vec2 transmittanceLUTParamsToUV(float r, float mu) {
    float H = sqrt(ATMOSPHERE_RADIUS * ATMOSPHERE_RADIUS - PLANET_RADIUS * PLANET_RADIUS);
    float rho = sqrt(max(0.0, r * r - PLANET_RADIUS * PLANET_RADIUS));

    // Distance to atmosphere boundary
    float discriminant = r * r * (mu * mu - 1.0) + ATMOSPHERE_RADIUS * ATMOSPHERE_RADIUS;
    float d = max(0.0, -r * mu + sqrt(max(0.0, discriminant)));

    float dMin = ATMOSPHERE_RADIUS - r;
    float dMax = rho + H;

    float xMu = (d - dMin) / (dMax - dMin);
    float xR = rho / H;

    return vec2(xMu, xR);
}

// Sample transmittance LUT for a ray from altitude r with zenith angle mu
// Returns transmittance (0-1) for each wavelength
vec3 sampleTransmittanceLUT(float r, float mu) {
    vec2 uv = transmittanceLUTParamsToUV(r, mu);
    return texture(transmittanceLUT, uv).rgb;
}

// Sample transmittance from a world position toward a direction
// worldPos should be relative to planet center (Y=0 is planet surface)
vec3 sampleTransmittanceFromPos(vec3 worldPos, vec3 direction) {
    float r = length(worldPos);
    float mu = dot(normalize(worldPos), direction);
    return sampleTransmittanceLUT(r, mu);
}

// ============================================================================
// Multi-Scatter LUT Sampling (Phase 4.1.4)
// ============================================================================

// Sample multi-scatter LUT for second-order scattering approximation
// altitude = height above planet surface (km)
// cosSunZenith = cosine of angle between up and sun direction
vec2 sampleMultiScatterLUT(float altitude, float cosSunZenith) {
    // UV mapping: x = cosSunZenith remapped to [0,1], y = normalized altitude
    float u = cosSunZenith * 0.5 + 0.5;
    float v = altitude / (ATMOSPHERE_RADIUS - PLANET_RADIUS);
    return texture(multiScatterLUT, vec2(u, v)).rg;
}

// ============================================================================
// Irradiance LUT Sampling (Phase 4.1.9)
// ============================================================================

// Encode altitude and sun zenith angle to irradiance LUT UV
vec2 irradianceLUTParamsToUV(float altitude, float cosSunZenith) {
    // UV mapping: x = cosSunZenith remapped from [-1,1] to [0,1]
    //             y = normalized altitude [0, atmosphere height]
    float u = cosSunZenith * 0.5 + 0.5;
    float v = clamp(altitude / (ATMOSPHERE_RADIUS - PLANET_RADIUS), 0.0, 1.0);
    return vec2(u, v);
}

// Sample atmospheric irradiance for lighting clouds and haze
// Returns separate Rayleigh and Mie irradiance (without phase function)
// This allows clouds to apply their own phase function when using the irradiance
struct AtmosphericIrradiance {
    vec3 rayleigh;  // Rayleigh scattered irradiance
    vec3 mie;       // Mie scattered irradiance
};

AtmosphericIrradiance sampleAtmosphericIrradiance(vec3 worldPos, vec3 sunDir) {
    AtmosphericIrradiance result;

    float altitude = max(length(worldPos) - PLANET_RADIUS, 0.0);
    float cosSunZenith = dot(normalize(worldPos), sunDir);

    vec2 uv = irradianceLUTParamsToUV(altitude, cosSunZenith);

    result.rayleigh = texture(rayleighIrradianceLUT, uv).rgb;
    result.mie = texture(mieIrradianceLUT, uv).rgb;

    return result;
}

// Combined irradiance with phase functions applied
// Use Rayleigh phase for diffuse ambient, Mie phase for specular highlights
vec3 sampleAtmosphericIrradianceWithPhase(vec3 worldPos, vec3 sunDir, vec3 viewDir, float mieG) {
    AtmosphericIrradiance irr = sampleAtmosphericIrradiance(worldPos, sunDir);

    float cosTheta = dot(viewDir, sunDir);
    float rayleighP = rayleighPhase(cosTheta);
    float mieP = cornetteShanksPhase(cosTheta, mieG);

    return irr.rayleigh * rayleighP + irr.mie * mieP;
}

// ============================================================================
// LUT-Based Atmospheric Transmittance
// ============================================================================

// Get transmittance from a point to the sun using LUT (fast path)
// This replaces the expensive ray-marched computeAtmosphericTransmittance for sun visibility
vec3 getTransmittanceToSunLUT(vec3 worldPos, vec3 sunDir) {
    float r = length(worldPos);
    if (r < PLANET_RADIUS) {
        r = PLANET_RADIUS + 0.001;  // Clamp to surface
    }
    float mu = dot(normalize(worldPos), sunDir);
    return sampleTransmittanceLUT(r, mu);
}

// Compute atmospheric transmittance from a point toward a light direction
// This integrates optical depth through the atmosphere to properly attenuate direct light
vec3 computeAtmosphericTransmittance(vec3 worldPos, vec3 lightDir, int samples) {
    // Find intersection with atmosphere boundary
    vec2 atmo = raySphereIntersect(worldPos, lightDir, ATMOSPHERE_RADIUS);
    if (atmo.y <= 0.0) return vec3(1.0);  // No intersection

    float pathLength = atmo.y;
    float stepSize = pathLength / float(samples);

    vec3 opticalDepth = vec3(0.0);

    for (int i = 0; i < samples; i++) {
        float t = (float(i) + 0.5) * stepSize;
        vec3 samplePos = worldPos + lightDir * t;
        float altitude = max(length(samplePos) - PLANET_RADIUS, 0.0);

        // Accumulate optical depth from all scattering/absorption sources
        float rayleighDensity = exp(-altitude / RAYLEIGH_SCALE_HEIGHT);
        float mieDensity = exp(-altitude / MIE_SCALE_HEIGHT);
        float ozone = ozoneDensity(altitude);

        vec3 extinction = rayleighDensity * RAYLEIGH_SCATTERING_BASE +
                          mieDensity * vec3(MIE_SCATTERING_BASE + MIE_ABSORPTION_BASE) +
                          ozone * OZONE_ABSORPTION;

        opticalDepth += extinction * stepSize;
    }

    return exp(-opticalDepth);
}

// Simplified transmittance for cloud lighting - now uses LUT for efficiency
vec3 computeTransmittanceToLight(vec3 cloudPos, vec3 lightDir) {
    // Use LUT-based transmittance (fast path)
    return getTransmittanceToSunLUT(cloudPos, lightDir);
}

// Earth shadow at sunrise/sunset (Phase 4.1.8)
// Computes shadow factor for a point in the atmosphere
float computeEarthShadow(vec3 worldPos, vec3 sunDir) {
    // worldPos is relative to planet center (origin at planet center)
    float altitude = length(worldPos) - PLANET_RADIUS;

    // Project position onto sun direction
    float sunDist = dot(worldPos, sunDir);

    // Check if behind planet relative to sun (in shadow cone)
    if (sunDist < 0.0) {
        // Calculate perpendicular distance from sun ray through planet center
        float perpDist = length(worldPos - sunDir * sunDist);

        // Shadow radius decreases with altitude (cone shape)
        float shadowRadius = PLANET_RADIUS * (1.0 - altitude / (ATMOSPHERE_RADIUS - PLANET_RADIUS));

        if (perpDist < shadowRadius) {
            // In umbra/penumbra region - wider penumbra for softer shadows
            float penumbraWidth = (ATMOSPHERE_RADIUS - PLANET_RADIUS) * 0.25;
            float shadow = smoothstep(shadowRadius - penumbraWidth, shadowRadius, perpDist);
            // Don't fully darken - keep minimum light for atmosphere scattering
            return mix(0.15, 1.0, shadow);
        }
    }

    return 1.0;  // Fully lit
}

struct ScatteringResult {
    vec3 inscatter;
    vec3 transmittance;
};

// Compute Rayleigh scattering with blended LMS for accurate sunset colors
// Uses LMS primarily at low sun angles for better sunsets, standard RGB otherwise
vec3 computeRayleighScatteringBlended(float density, float phase, float sunAltitude, vec3 rayleighBase) {
    // Standard RGB Rayleigh using provided base (from UBO or default)
    vec3 scatterRGB = density * rayleighBase * phase;

    // LMS-space Rayleigh for more accurate sunsets
    // Scale LMS by ratio of UBO base to default to maintain proportions
    float baseScale = length(rayleighBase) / length(RAYLEIGH_SCATTERING_BASE);
    vec3 scatterLMS = density * RAYLEIGH_LMS * baseScale * phase;
    vec3 lmsInRGB = LMS_TO_RGB * scatterLMS;

    // Blend: use LMS more when sun is near horizon (better sunset colors)
    // and standard RGB when sun is high (cleaner blue sky)
    float lmsBlend = smoothstep(0.3, -0.1, sunAltitude);  // 0 at high sun, 1 at sunset
    return mix(scatterRGB, lmsInRGB, lmsBlend * 0.7);  // Max 70% LMS contribution
}

// Overload for backward compatibility with default Rayleigh base
vec3 computeRayleighScatteringBlended(float density, float phase, float sunAltitude) {
    return computeRayleighScatteringBlended(density, phase, sunAltitude, RAYLEIGH_SCATTERING_BASE);
}

ScatteringResult integrateAtmosphere(vec3 origin, vec3 dir, int sampleCount) {
    vec2 atmo = raySphereIntersect(origin, dir, ATMOSPHERE_RADIUS);
    float start = max(atmo.x, 0.0);
    float end = atmo.y;

    if (end <= 0.0) {
        return ScatteringResult(vec3(0.0), vec3(1.0));
    }

    vec2 planet = raySphereIntersect(origin, dir, PLANET_RADIUS);
    if (planet.x > 0.0) {
        end = min(end, planet.x);
    }

    // Extract atmosphere parameters from UBO (use defaults if UBO values are zero/disabled)
    vec3 rayleighBase = length(ubo.atmosRayleighScattering.xyz) > 0.0001
        ? ubo.atmosRayleighScattering.xyz : RAYLEIGH_SCATTERING_BASE;
    float rayleighScaleH = ubo.atmosRayleighScattering.w > 0.01
        ? ubo.atmosRayleighScattering.w : RAYLEIGH_SCALE_HEIGHT;
    float mieScatter = ubo.atmosMieParams.x > 0.0001
        ? ubo.atmosMieParams.x : MIE_SCATTERING_BASE;
    float mieAbsorb = ubo.atmosMieParams.y > 0.0001
        ? ubo.atmosMieParams.y : MIE_ABSORPTION_BASE;
    float mieScaleH = ubo.atmosMieParams.z > 0.01
        ? ubo.atmosMieParams.z : MIE_SCALE_HEIGHT;
    float mieAniso = abs(ubo.atmosMieParams.w) > 0.001
        ? clamp(ubo.atmosMieParams.w, -0.99, 0.99) : MIE_ANISOTROPY;
    vec3 ozoneAbs = length(ubo.atmosOzoneAbsorption.xyz) > 0.00001
        ? ubo.atmosOzoneAbsorption.xyz : OZONE_ABSORPTION;

    float stepSize = (end - start) / float(sampleCount);
    vec3 transmittance = vec3(1.0);
    vec3 inscatter = vec3(0.0);

    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 moonDir = normalize(ubo.moonDirection.xyz);
    float cosViewSun = dot(dir, sunDir);
    float cosViewMoon = dot(dir, moonDir);
    float rayleighPSun = rayleighPhase(cosViewSun);
    float rayleighPMoon = rayleighPhase(cosViewMoon);
    float miePSun = cornetteShanksPhase(cosViewSun, mieAniso);
    float miePMoon = cornetteShanksPhase(cosViewMoon, mieAniso);

    float sunAltitude = sunDir.y;
    float moonAltitude = moonDir.y;
    float moonIntensity = ubo.moonDirection.w;

    // Smooth twilight transition - moon scattering fades in during sunset
    // At sun altitude 0.17 (10°): no moon atmospheric scattering
    // At sun altitude -0.1 (-6°): full moon atmospheric scattering
    float twilightFactor = smoothstep(0.17, -0.1, sunAltitude);
    // Moon visibility: matches C++ altFactor range (-2° to +10°, i.e., -0.035 to 0.17 radians)
    float moonVisibility = smoothstep(-0.035, 0.17, moonAltitude);
    float moonAtmoContribution = twilightFactor * moonVisibility;

    for (int i = 0; i < sampleCount; i++) {
        float t = start + (float(i) + 0.5) * stepSize;
        vec3 pos = origin + dir * t;
        float altitude = max(length(pos) - PLANET_RADIUS, 0.0);

        float rayleighDensity = exp(-altitude / rayleighScaleH);
        float mieDensity = exp(-altitude / mieScaleH);
        float ozone = ozoneDensity(altitude);

        // Sample transmittance LUT for sunlight reaching this point
        float posR = length(pos);
        float muSun = dot(normalize(pos), sunDir);
        vec3 sunTransmittance = sampleTransmittanceLUT(posR, muSun);

        // Sun contribution: Use blended LMS/RGB Rayleigh for accurate sunset colors
        vec3 rayleighScatterSun = computeRayleighScatteringBlended(rayleighDensity, rayleighPSun, sunAltitude, rayleighBase);
        vec3 mieScatterSun = mieDensity * vec3(mieScatter);

        // Moon contribution: simpler Rayleigh scattering (moonlight is dimmer, less color sensitivity needed)
        vec3 rayleighScatterMoon = rayleighDensity * rayleighBase * rayleighPMoon;
        vec3 mieScatterMoon = mieDensity * vec3(mieScatter);

        // Extinction uses standard RGB coefficients
        vec3 rayleighScatterRGB = rayleighDensity * rayleighBase;
        vec3 extinction = rayleighScatterRGB + mieScatterSun +
                          mieDensity * vec3(mieAbsorb) +
                          ozone * ozoneAbs;

        // Earth shadow modulates in-scattering (Phase 4.1.8)
        float earthShadowSun = computeEarthShadow(pos, sunDir);
        float earthShadowMoon = computeEarthShadow(pos, moonDir);

        // Combine sun scattering - multiply by sun transmittance (light reaching this point)
        vec3 segmentScatterSun = sunTransmittance * (rayleighScatterSun + mieScatterSun * miePSun) * earthShadowSun;

        // Add moon scattering - scales smoothly with twilight transition
        vec3 segmentScatterMoon = vec3(0.0);
        if (moonAtmoContribution > 0.01) {
            segmentScatterMoon = (rayleighScatterMoon + mieScatterMoon * miePMoon) * earthShadowMoon * moonIntensity * moonAtmoContribution;
        }

        vec3 segmentScatter = segmentScatterSun + segmentScatterMoon;

        vec3 attenuation = exp(-extinction * stepSize);
        inscatter += transmittance * segmentScatter * stepSize;
        transmittance *= attenuation;
    }

    // Apply solar irradiance to produce physically-correct sky brightness
    // The base scattering coefficients produce HDR values that need exposure adjustment
    // for display. A factor of ~5 brings the sky to typical daytime brightness levels.
    const float SKY_EXPOSURE = 5.0;
    inscatter *= SOLAR_IRRADIANCE * SKY_EXPOSURE;

    return ScatteringResult(inscatter, transmittance);
}

// Compute sky irradiance using precomputed irradiance LUTs (Phase 4.1.9)
// Much faster than the previous hemisphere sampling approach
struct SkyIrradiance {
    vec3 skyIrradiance;     // Hemisphere irradiance from sky
    vec3 groundIrradiance;  // Ground bounce irradiance (for cloud undersides)
};

SkyIrradiance computeSkyIrradiance(vec3 position, vec3 sunDir, vec3 moonDir,
                                   vec3 sunLight, vec3 moonLight,
                                   float sunAltitude, float moonContribution) {
    SkyIrradiance result;
    result.skyIrradiance = vec3(0.0);
    result.groundIrradiance = vec3(0.0);

    // Sample irradiance LUTs for this position (Phase 4.1.9)
    // These LUTs store precomputed Rayleigh and Mie scattered irradiance
    AtmosphericIrradiance irr = sampleAtmosphericIrradiance(position, sunDir);

    // For hemisphere irradiance, use isotropic phase (averaged over all view directions)
    // Rayleigh isotropic: integrate RayleighPhase over sphere = 1/(4*PI) * 4*PI = 1
    // Mie isotropic: for g=0.8, integrate HG over sphere ≈ 0.25 (heavily forward-peaked)
    const float RAYLEIGH_ISO = 1.0;
    const float MIE_ISO = 0.25;

    // Sun contribution to sky irradiance
    result.skyIrradiance = sunLight * (irr.rayleigh * RAYLEIGH_ISO + irr.mie * MIE_ISO);

    // Moon contribution - use same LUT approach but with reduced intensity
    if (moonContribution > 0.01) {
        AtmosphericIrradiance moonIrr = sampleAtmosphericIrradiance(position, moonDir);
        result.skyIrradiance += moonLight * (moonIrr.rayleigh * RAYLEIGH_ISO + moonIrr.mie * MIE_ISO) *
                                0.5 * moonContribution;
    }

    // Night sky floor to prevent total darkness
    float night = 1.0 - smoothstep(-0.05, 0.08, sunAltitude);
    result.skyIrradiance += night * vec3(0.01, 0.015, 0.03);

    // Compute ground bounce from transmittance toward ground
    // Ground reflects sunlight with albedo ~0.2 (typical terrain)
    vec3 groundDir = vec3(0.0, -1.0, 0.0);
    float groundSunDot = max(dot(-groundDir, sunDir), 0.0);
    float groundMoonDot = max(dot(-groundDir, moonDir), 0.0);

    // Ground albedo with slight color (brownish terrain)
    vec3 groundAlbedo = vec3(0.18, 0.15, 0.12);

    // Sun contribution to ground, attenuated by atmosphere
    vec3 sunTransmittance = computeTransmittanceToLight(vec3(0.0, PLANET_RADIUS, 0.0), sunDir);
    result.groundIrradiance = groundAlbedo * sunLight * sunTransmittance * groundSunDot;

    // Moon contribution to ground
    if (moonContribution > 0.01) {
        vec3 moonTransmittance = computeTransmittanceToLight(vec3(0.0, PLANET_RADIUS, 0.0), moonDir);
        result.groundIrradiance += groundAlbedo * moonLight * moonTransmittance * groundMoonDot * moonContribution;
    }

    // Scale ground irradiance by hemisphere solid angle factor
    result.groundIrradiance *= 0.5;

    return result;
}

// March through cloud layer with physically-based lighting
// Uses atmospheric transmittance and computed sky irradiance for accurate results
CloudResult marchClouds(vec3 origin, vec3 dir) {
    // Set global ray direction for paraboloid cloud map lookup
    g_cloudRayDir = dir;

    CloudResult result;
    result.scattering = vec3(0.0);
    result.transmittance = 1.0;

    // Find intersection with cloud layer
    vec2 cloudHit = intersectCloudLayer(origin, dir);
    if (cloudHit.x >= cloudHit.y || cloudHit.y < 0.0) {
        return result;
    }

    float tStart = cloudHit.x;
    float tEnd = cloudHit.y;
    float stepSize = (tEnd - tStart) / float(CLOUD_MARCH_STEPS);

    // Add jitter to reduce banding (screen-space dither based on ray direction)
    float jitter = hash(dir * 1000.0 + vec3(ubo.timeOfDay * 0.1));
    tStart += stepSize * jitter * 0.5;

    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 moonDir = normalize(ubo.moonDirection.xyz);
    float cosThetaSun = dot(dir, sunDir);
    float cosThetaMoon = dot(dir, moonDir);

    // Cloud lighting must use the same brightness scale as the sky
    // The sky uses SOLAR_IRRADIANCE * SKY_EXPOSURE (see integrateAtmosphereWithLUT)
    // Without this scaling, clouds appear ~5x darker than the sky behind them
    const float SKY_EXPOSURE = 5.0;

    // Base sun/moon color with intensity (for ambient via irradiance LUT)
    // The irradiance LUT already includes solarIrradiance, so we only add SKY_EXPOSURE here
    vec3 sunLightBase = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 moonLightBase = ubo.moonColor.rgb * ubo.moonDirection.w;

    // Full brightness for direct cloud lighting (includes SOLAR_IRRADIANCE for physical correctness)
    vec3 sunLight = sunLightBase * SOLAR_IRRADIANCE * SKY_EXPOSURE;
    vec3 moonLight = moonLightBase * SOLAR_IRRADIANCE * SKY_EXPOSURE;

    float sunAltitude = ubo.sunDirection.y;
    float moonAltitude = ubo.moonDirection.y;

    // Smooth twilight transition factor - moon fades in as sun approaches/passes horizon
    float twilightFactor = smoothstep(0.17, -0.1, sunAltitude);
    // Moon visibility: matches C++ altFactor range (-2° to +10°, i.e., -0.035 to 0.17 radians)
    float moonVisibility = smoothstep(-0.035, 0.17, moonAltitude);
    float moonContribution = twilightFactor * moonVisibility;

    // Compute physically-based sky irradiance at cloud altitude
    // Sample position in middle of cloud layer for irradiance calculation
    // Use sunLightBase * SKY_EXPOSURE (not sunLight) because irradiance LUT already has solarIrradiance
    vec3 cloudSamplePos = vec3(0.0, PLANET_RADIUS + (CLOUD_LAYER_BOTTOM + CLOUD_LAYER_TOP) * 0.5, 0.0);
    SkyIrradiance skyIrrad = computeSkyIrradiance(cloudSamplePos, sunDir, moonDir,
                                                   sunLightBase * SKY_EXPOSURE, moonLightBase * SKY_EXPOSURE,
                                                   sunAltitude, moonContribution);

    // Cloud scattering albedo (single-scattering albedo for water droplets)
    const float CLOUD_ALBEDO = 0.99;  // High albedo for water droplets

    // Compute atmospheric transmittance from cloud layer to sun/moon
    // This attenuates direct light reaching the clouds
    vec3 cloudLayerPos = vec3(0.0, PLANET_RADIUS + CLOUD_LAYER_BOTTOM, 0.0);
    vec3 sunAtmoTransmittance = computeTransmittanceToLight(cloudLayerPos, sunDir);
    vec3 moonAtmoTransmittance = computeTransmittanceToLight(cloudLayerPos, moonDir);

    // Attenuate direct light by atmospheric transmittance
    vec3 attenuatedSunLight = sunLight * sunAtmoTransmittance;
    vec3 attenuatedMoonLight = moonLight * moonAtmoTransmittance;

    for (int i = 0; i < CLOUD_MARCH_STEPS; i++) {
        if (result.transmittance < 0.01) break;

        float t = tStart + (float(i) + 0.5) * stepSize;
        vec3 pos = origin + dir * t;

        float density = sampleCloudDensity(pos);

        if (density > 0.005) {  // Skip very thin cloud regions
            // Sample transmittance to sun through clouds
            float cloudTransmittanceToSun = sampleCloudTransmittanceToSun(pos, sunDir);

            // Phase function for sun using Cornette-Shanks for consistency with atmosphere
            // Use depth-dependent phase for multi-scattering approximation
            float phaseSun = cloudPhase(cosThetaSun, cloudTransmittanceToSun, result.transmittance);

            // In-scattering from sun (with atmospheric transmittance)
            vec3 sunScatter = attenuatedSunLight * cloudTransmittanceToSun * phaseSun;

            // Add moon scattering - scales smoothly with twilight transition
            vec3 moonScatter = vec3(0.0);
            if (moonContribution > 0.01) {
                float cloudTransmittanceToMoon = sampleCloudTransmittanceToSun(pos, moonDir);
                float phaseMoon = cloudPhase(cosThetaMoon, cloudTransmittanceToMoon, result.transmittance);
                moonScatter = attenuatedMoonLight * cloudTransmittanceToMoon * phaseMoon * moonContribution;
            }

            // Compute height fraction for ambient weighting
            float altitude = length(pos) - PLANET_RADIUS;
            float heightFraction = clamp((altitude - CLOUD_LAYER_BOTTOM) / (CLOUD_LAYER_TOP - CLOUD_LAYER_BOTTOM), 0.0, 1.0);

            // Blend between sky and ground irradiance based on height in cloud
            // Top of cloud gets more sky, bottom gets more ground bounce
            vec3 ambientIrradiance = mix(skyIrrad.groundIrradiance, skyIrrad.skyIrradiance, heightFraction);

            // Apply isotropic phase for ambient (1/4π for sphere, but we use hemisphere factor)
            float ambientPhase = 0.25 / PI;
            vec3 ambientScatter = ambientIrradiance * ambientPhase;

            // Total in-scattering with energy conservation
            // Direct light uses phase function, ambient is isotropic
            vec3 totalScatter = (sunScatter + moonScatter + ambientScatter) * CLOUD_ALBEDO;

            // Beer-Lambert extinction
            float extinction = density * stepSize * 10.0;
            float segmentTransmittance = exp(-extinction);

            // Energy-conserving integration (scattered + transmitted = 1)
            // The (1 - segmentTransmittance) factor ensures energy conservation
            vec3 integScatter = totalScatter * (1.0 - segmentTransmittance);
            result.scattering += result.transmittance * integScatter;
            result.transmittance *= segmentTransmittance;
        }
    }

    return result;
}

// Rotate vector around Y-axis (celestial pole) for sidereal rotation
vec3 rotateSidereal(vec3 dir, float julianDay) {
    // Sidereal rotation: 360.9856 degrees per day (one sidereal day = 0.99726958 solar days)
    // Use J2000.0 (JD 2451545.0) as reference epoch
    const float J2000 = 2451545.0;
    const float SIDEREAL_RATE = 360.9856;  // degrees per day

    float daysSinceJ2000 = julianDay - J2000;
    float rotationAngle = daysSinceJ2000 * SIDEREAL_RATE;
    float angleRad = radians(rotationAngle);

    // Rotation matrix around Y-axis
    float c = cos(angleRad);
    float s = sin(angleRad);
    mat3 rotation = mat3(
        c,  0.0, s,
        0.0, 1.0, 0.0,
        -s, 0.0, c
    );

    return rotation * dir;
}

float starField(vec3 dir) {
    // Stars appear as sun goes below horizon - consistent with twilight transition
    float sunAltitude = ubo.sunDirection.y;
    float nightFactor = 1.0 - smoothstep(-0.1, 0.08, sunAltitude);
    if (nightFactor < 0.01) return 0.0;

    dir = normalize(dir);

    // Smooth fade for stars near horizon (matches horizonBlend range)
    float horizonFade = smoothstep(-0.02, 0.02, dir.y);
    if (horizonFade < 0.01) return 0.0;

    // Use horizon direction for sampling stars below horizon to avoid discontinuity
    vec3 starDir = dir;
    starDir.y = max(starDir.y, 0.001);
    starDir = normalize(starDir);

    // Apply sidereal rotation based on Julian day
    starDir = rotateSidereal(starDir, ubo.julianDay);

    float theta = atan(starDir.z, starDir.x);
    float phi = asin(clamp(starDir.y, -1.0, 1.0));

    vec2 gridCoord = vec2(theta * 200.0, phi * 200.0);
    vec2 cell = floor(gridCoord);
    vec2 cellFrac = fract(gridCoord);

    vec3 p = vec3(cell, 0.0);
    float h = hash(p);

    // Only generate star if hash is high enough
    if (h < 0.992) return 0.0;

    // Get random position within the cell for this star
    vec2 starPos = vec2(hash(p + vec3(1.0)), hash(p + vec3(2.0)));

    // Calculate distance from current pixel to star center
    vec2 delta = cellFrac - starPos;
    float dist = length(delta);

    // Create small, intense point with softer falloff for visibility
    // Small but visible dots that trigger bloom
    float starSize = 0.04; // Small angular size, visible but still point-like
    float star = smoothstep(starSize * 1.5, starSize * 0.3, dist);

    // High intensity to trigger bloom (3-8x brighter)
    float brightness = (hash(p + vec3(3.0)) * 0.6 + 0.4) * 6.0;

    return star * brightness * nightFactor * horizonFade;
}

float celestialDisc(vec3 dir, vec3 celestialDir, float size) {
    float d = dot(normalize(dir), normalize(celestialDir));
    return smoothstep(1.0 - size, 1.0 - size * 0.3, d);
}

// Lunar phase mask - creates moon phases based on actual sun-moon geometry
// sunDir: actual sun direction in world space
// Returns 0-1 illumination factor
float lunarPhaseMask(vec3 dir, vec3 moonDir, vec3 sunDir, float discSize) {
    vec3 viewDir = normalize(dir);
    vec3 moonCenter = normalize(moonDir);
    vec3 sunDirection = normalize(sunDir);

    // Calculate angular distance from moon center
    float centerDot = dot(viewDir, moonCenter);
    float angularDist = acos(clamp(centerDot, -1.0, 1.0));

    // Outside the disc
    if (angularDist > discSize) return 0.0;

    // Create coordinate system on moon disc aligned with view
    vec3 up = abs(moonCenter.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 right = normalize(cross(up, moonCenter));
    vec3 tangentUp = normalize(cross(moonCenter, right));

    // Project view direction onto disc
    vec3 toPoint = viewDir - moonCenter * centerDot;
    float x = dot(toPoint, right) / (sin(discSize) + 0.001);
    float y = dot(toPoint, tangentUp) / (sin(discSize) + 0.001);

    // Distance from center in disc coordinates
    float r = sqrt(x * x + y * y);
    if (r > 1.0) return 0.0;

    // Calculate 3D sphere normal (z points toward viewer)
    float z = sqrt(max(0.0, 1.0 - r * r));

    // Transform sun direction into moon's local coordinate system
    // The moon's local frame: right, tangentUp, moonCenter (toward viewer)
    vec3 sunLocal;
    sunLocal.x = dot(sunDirection, right);
    sunLocal.y = dot(sunDirection, tangentUp);
    sunLocal.z = dot(sunDirection, moonCenter);
    sunLocal = normalize(sunLocal);

    // Surface normal at this point in local space
    vec3 normal = normalize(vec3(x, y, z));

    // Lambertian lighting based on actual sun direction
    float lighting = dot(normal, sunLocal);

    // Smooth terminator
    float lit = smoothstep(-0.1, 0.1, lighting);

    // Earthshine (subtle light on dark side - user-configurable)
    return max(lit, ubo.moonEarthshine);
}

// Solar eclipse mask - simulates the moon passing in front of the sun
// eclipseAmount: 0 = no eclipse, 1 = total eclipse (moon centered on sun)
// Returns 0-1 where 1 = fully visible sun, 0 = fully occluded
float solarEclipseMask(vec3 dir, vec3 sunDir, vec3 moonDir, float eclipseAmount, float sunRadius) {
    if (eclipseAmount <= 0.0) return 1.0;

    vec3 viewDir = normalize(dir);
    vec3 sunCenter = normalize(sunDir);

    // During eclipse, the moon is positioned between Earth and Sun
    // We interpolate moon position toward sun position based on eclipse amount
    vec3 eclipseMoonDir = normalize(mix(moonDir, sunCenter, eclipseAmount));

    // Calculate angular distance from moon center to view direction
    float moonDot = dot(viewDir, eclipseMoonDir);
    float moonAngularDist = acos(clamp(moonDot, -1.0, 1.0));

    // Moon is same apparent size as sun during eclipse
    float moonRadius = sunRadius * 1.02;  // Slight variation for annular/total

    // Smooth edge for the moon shadow
    float eclipseShadow = 1.0 - smoothstep(moonRadius * 0.9, moonRadius * 1.1, moonAngularDist);

    return 1.0 - eclipseShadow * eclipseAmount;
}

// Solar corona effect - visible during total eclipse
vec3 solarCorona(vec3 dir, vec3 sunDir, float eclipseAmount, float sunRadius) {
    // Corona only becomes visible very close to totality (95%+)
    if (eclipseAmount < 0.9) return vec3(0.0);

    vec3 viewDir = normalize(dir);
    vec3 sunCenter = normalize(sunDir);

    float sunDot = dot(viewDir, sunCenter);
    float angularDist = acos(clamp(sunDot, -1.0, 1.0));

    // Corona extends 2-4 sun radii out
    float coronaStart = sunRadius * 1.0;
    float coronaEnd = sunRadius * 4.0;

    // Radial falloff for corona
    float coronaFade = 1.0 - smoothstep(coronaStart, coronaEnd, angularDist);
    coronaFade *= smoothstep(sunRadius * 0.8, coronaStart, angularDist);

    // Corona is very bright near the limb and fades rapidly
    float coronaIntensity = pow(coronaFade, 1.5) * 15.0;

    // Corona color is pearly white
    vec3 coronaColor = vec3(1.0, 0.98, 0.95);

    // Only visible during near-total eclipse (95%+ with smooth transition)
    float totalityFactor = smoothstep(0.9, 0.98, eclipseAmount);

    return coronaColor * coronaIntensity * totalityFactor;
}

vec3 renderAtmosphere(vec3 dir) {
    vec3 normDir = normalize(dir);

    // For a game scene, we want the sky horizon to match the flat ground plane at Y=0.
    // Rays pointing downward (negative Y) are below the horizon.
    // We still do atmospheric scattering for rays near the horizon to get the
    // characteristic horizon glow, but clamp the minimum elevation angle.

    float sunAltitude = ubo.sunDirection.y;
    float moonAltitude = ubo.moonDirection.y;

    // Smooth twilight transition factor for sky rendering
    float twilightFactor = smoothstep(0.17, -0.1, sunAltitude);
    // Moon visibility: matches C++ altFactor range (-2° to +10°, i.e., -0.035 to 0.17 radians)
    float moonVisibility = smoothstep(-0.035, 0.17, moonAltitude);
    float moonSkyContribution = twilightFactor * moonVisibility;

    // Compute horizon/below-horizon color for blending
    // Use sky-view LUT sampled at horizon level for consistent atmosphere response
    vec3 horizonDir = normalize(vec3(normDir.x, 0.001, normDir.z));
    vec3 horizonLUTColor = sampleSkyViewLUT(horizonDir);

    // Sample transmittance LUT for horizon direction
    vec3 horizonOrigin = vec3(0.0, PLANET_RADIUS + 0.001, 0.0);
    vec3 horizonTransmittance = sampleTransmittanceFromPos(horizonOrigin, horizonDir);

    vec3 sunLight = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 moonLight = ubo.moonColor.rgb * ubo.moonDirection.w;

    // Sun contribution to horizon using LUT
    vec3 horizonColor = horizonLUTColor * sunLight;

    // Moon contribution - fades in smoothly during twilight
    if (moonSkyContribution > 0.01) {
        horizonColor += horizonLUTColor * moonLight * moonSkyContribution;
    }

    // Multiple scattering compensation (reduced to avoid overpowering sky color)
    horizonColor += sunLight * 0.02 * (1.0 - horizonTransmittance);
    if (moonSkyContribution > 0.01) {
        horizonColor += moonLight * 0.01 * (1.0 - horizonTransmittance) * moonSkyContribution;
    }

    // Night sky floor (energy-conserving blend, not additive)
    float nightFactor = 1.0 - smoothstep(-0.1, 0.08, sunAltitude);
    vec3 nightHorizonRadiance = vec3(0.008, 0.012, 0.02);
    float nightBlend = nightFactor * (1.0 - moonSkyContribution * 0.5);
    horizonColor = mix(horizonColor, max(horizonColor, nightHorizonRadiance), nightBlend);

    // Blend factor for smooth transition near horizon (y from -0.02 to 0.02)
    // Below -0.02: horizonBlend=0 (full horizon color)
    // Above 0.02: horizonBlend=1 (full sky)
    float horizonBlend = smoothstep(-0.02, 0.02, normDir.y);

    // Darkening factor for below-horizon rays (kicks in below y=-0.02)
    float belowHorizonDarken = clamp((-normDir.y - 0.02) * 2.0, 0.0, 1.0);

    // Place observer on planet surface (camera height is negligible for atmosphere)
    vec3 origin = vec3(0.0, PLANET_RADIUS + 0.001, 0.0);

    // Use sky-view LUT for fast atmospheric scattering lookup (Phase 4.1.5)
    // The LUT is precomputed per-frame with current sun direction and responds to UI parameter changes
    vec3 skyLUTColor = sampleSkyViewLUT(normDir);

    // Sample transmittance LUT for viewer-to-sky transmittance
    // This replaces the expensive integrateAtmosphere() call - LUTs are already computed with correct params
    vec3 skyTransmittance = sampleTransmittanceFromPos(origin, normDir);

    // sunLight and moonLight already defined above for horizon calculation

    // Use LUT-based sky color which responds to atmosphere parameter changes from UI
    // The LUT already includes proper Rayleigh/Mie scattering with current parameters
    vec3 sunSkyContrib = skyLUTColor * sunLight;

    // Moon sky contribution - fades in smoothly during twilight
    // Sample LUT with moon direction for moon's atmospheric scattering contribution
    vec3 moonSkyContrib = vec3(0.0);
    if (moonSkyContribution > 0.01) {
        // Moon illuminates the atmosphere similar to sun but dimmer
        moonSkyContrib = skyLUTColor * moonLight * moonSkyContribution;
    }

    // Multiple scattering compensation (approximates light scattered more than once)
    // Multi-scattered light is more isotropic, so we use a reduced intensity
    // and don't let it dominate the forward-scattered sunlight near the sun
    // The 0.02 factor is much smaller to avoid washing out the sun halo
    vec3 multiScatterSun = sunLight * 0.02 * (1.0 - skyTransmittance);
    vec3 multiScatterMoon = moonLight * 0.01 * (1.0 - skyTransmittance) * moonSkyContribution;

    // Combine all sky contributions
    vec3 sky = sunSkyContrib + moonSkyContrib + multiScatterSun + multiScatterMoon;

    // Add Mie forward-scattering halo around sun
    // The base Mie contribution in inscatter is correct but subtle due to small MIE_SCATTERING_BASE.
    // For a visible sun halo, we add an explicit Mie phase-weighted glow that represents
    // the bright forward-scattering peak around the sun disc.
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    float cosSun = dot(normDir, sunDir);
    // Use UBO mie anisotropy if available, otherwise default
    float mieAniso = abs(ubo.atmosMieParams.w) > 0.001 ? clamp(ubo.atmosMieParams.w, -0.99, 0.99) : MIE_ANISOTROPY;
    float miePhase = cornetteShanksPhase(cosSun, mieAniso);

    // The halo intensity should fall off smoothly
    // Mie phase already provides the angular falloff - just scale it for visibility
    // Use a wider falloff to create a natural glow extending ~30° from sun
    float haloFalloff = smoothstep(0.0, 0.98, cosSun);  // Gradual falloff, strongest near sun
    vec3 sunHalo = sunLight * miePhase * haloFalloff * skyTransmittance * 0.08;
    sky += sunHalo;

    // Night sky radiance - represents the dark sky with slight airglow
    // This is a minimum floor, not additive, to prevent color shifts
    // nightFactor already defined above for horizon calculation
    vec3 nightSkyRadiance = mix(vec3(0.005, 0.008, 0.015), vec3(0.015, 0.02, 0.035), normDir.y * 0.5 + 0.5);

    // Blend toward night sky when transitioning - energy conserving blend
    // During twilight, the sky naturally transitions; at deep night, use floor radiance
    // nightBlend already defined above for horizon calculation
    sky = mix(sky, max(sky, nightSkyRadiance), nightBlend);

    // Render volumetric clouds (Phase 4.2)
    CloudResult clouds = marchClouds(origin, normDir);

    // Composite clouds over sky (Ghost of Tsushima technique)
    // Clouds are lit by sky irradiance at their altitude - NOT darkened by atmospheric transmittance
    // The atmospheric transmittance only affects what's BEHIND the clouds (the sky), not the clouds themselves
    // Atmospheric haze is then added between camera and clouds

    // Cloud color is their own scattering (already lit during ray march)
    vec3 cloudColor = clouds.scattering;

    // The sky behind clouds IS attenuated by atmosphere
    vec3 skyBehindClouds = sky * clouds.transmittance;

    // Add atmospheric in-scattering between camera and clouds (haze in front of clouds)
    // This uses the LUT inscatter, scaled by how much cloud is visible
    vec3 hazeLight = sunLight;
    if (moonSkyContribution > 0.01) {
        hazeLight += moonLight * 0.3 * moonSkyContribution;
    }
    vec3 hazeInFront = skyLUTColor * hazeLight * (1.0 - clouds.transmittance) * 0.3;

    // Final composite: sky behind clouds + cloud color + haze in front
    sky = skyBehindClouds + cloudColor + hazeInFront;

    // Sun and moon discs (rendered behind clouds)
    // Only show sun/moon if clouds don't fully occlude them
    float sunDisc = celestialDisc(dir, ubo.sunDirection.xyz, SUN_ANGULAR_RADIUS);

    // Apply eclipse masking to sun
    float eclipseMask = solarEclipseMask(dir, ubo.sunDirection.xyz, ubo.moonDirection.xyz,
                                         ubo.eclipseAmount, MOON_MASK_RADIUS);
    sky += sunLight * sunDisc * 20.0 * eclipseMask * skyTransmittance * clouds.transmittance;

    // Add solar corona during eclipse (visible during totality)
    vec3 corona = solarCorona(dir, ubo.sunDirection.xyz, ubo.eclipseAmount, MOON_MASK_RADIUS);
    sky += corona * skyTransmittance * clouds.transmittance;

    // Moon disc with lunar phase simulation
    // Use MOON_DISC_SIZE for celestialDisc (creates visible disc)
    // Use MOON_MASK_RADIUS for lunarPhaseMask (angular radius for phase calculation)
    // Pass actual sun direction for correct terminator orientation
    float moonDisc = celestialDisc(dir, ubo.moonDirection.xyz, MOON_DISC_SIZE);
    float phaseMask = lunarPhaseMask(dir, ubo.moonDirection.xyz, ubo.sunDirection.xyz, MOON_MASK_RADIUS);

    // Apply phase mask with intensity scaled by illumination
    // Moon surface has albedo ~0.12, so it's much dimmer than the sun
    // Use user-configurable disc intensity scaled by phase mask
    float moonDiscBrightness = ubo.moonDiscIntensity * phaseMask;
    // Allow moon to redden at horizon like the sun (remove minimum clamp)
    sky += ubo.moonColor.rgb * moonDisc * moonDiscBrightness * ubo.moonDirection.w *
           skyTransmittance * clouds.transmittance;

    // Star field blended over the atmospheric tint (also behind clouds)
    // Stars are already modulated by nightFactor inside starField()
    float stars = starField(dir);
    sky += vec3(stars) * clouds.transmittance;

    // Blend with horizon color near the horizon
    sky = mix(horizonColor, sky, horizonBlend);

    // Apply gradual darkening for below-horizon rays
    sky *= mix(1.0, 0.5, belowHorizonDarken);

    return sky;
}

void main() {
    vec3 dir = normalize(rayDir);
    vec3 sky = renderAtmosphere(dir);
    outColor = vec4(sky, 1.0);
}
