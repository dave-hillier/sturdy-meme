#version 450

const float PI = 3.14159265359;
const int NUM_CASCADES = 4;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];  // Per-cascade light matrices
    vec4 cascadeSplits;                   // View-space split depths
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;                       // rgb = moon color, a = moon phase (0-1)
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;  // xyz = position, w = intensity
    vec4 pointLightColor;     // rgb = color, a = radius
    vec4 windDirectionAndSpeed;           // xy = direction, z = speed, w = time
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;               // Julian day for sidereal rotation
} ubo;

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
const float MOON_ANGULAR_RADIUS = SUN_ANGULAR_RADIUS * 0.8;  // Slightly larger than sun due to inverse celestialDisc function

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

// Cloud parameters (Phase 4.2 - Volumetric Clouds)
const float CLOUD_LAYER_BOTTOM = 1.5;     // km above surface
const float CLOUD_LAYER_TOP = 4.0;        // km above surface
const float CLOUD_COVERAGE = 0.5;         // 0-1 coverage amount
const float CLOUD_DENSITY = 0.3;          // Base density multiplier
const int CLOUD_MARCH_STEPS = 32;         // Ray march samples
const int CLOUD_LIGHT_STEPS = 6;          // Light sampling steps

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

// Sample cloud density at a point
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

    // Wind offset for animation - driven by wind system
    // Extract wind parameters from uniform
    vec2 windDir = ubo.windDirectionAndSpeed.xy;
    float windSpeed = ubo.windDirectionAndSpeed.z;
    float windTime = ubo.windDirectionAndSpeed.w;

    // Scroll clouds in wind direction at wind speed
    // Use 3D wind offset with vertical component for natural cloud evolution
    vec3 windOffset = vec3(windDir.x * windSpeed * windTime,
                           windTime * 0.1,  // Slow vertical evolution
                           windDir.y * windSpeed * windTime);

    // Sample noise at different scales for shape and detail
    vec3 samplePos = worldPos * 0.5 + windOffset;  // Base scale

    // Large-scale shape noise (4 octaves for good quality/perf balance)
    float baseNoise = fbm(samplePos * 0.25, 4);

    // Apply coverage with softer transition
    float coverageThreshold = 1.0 - CLOUD_COVERAGE;
    float density = smoothstep(coverageThreshold, coverageThreshold + 0.35, baseNoise);

    // Apply height gradient
    density *= heightGradient;

    // Detail erosion (2 octaves - cheaper but still effective)
    float detailNoise = fbm(samplePos * 1.0 + vec3(100.0), 2);
    density -= detailNoise * 0.2 * (1.0 - heightFraction);
    density = max(density, 0.0);

    return density * CLOUD_DENSITY;
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
    float z = (altitude - OZONE_LAYER_CENTER) / OZONE_LAYER_WIDTH;
    return exp(-0.5 * z * z);
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

// Simplified transmittance for cloud lighting (fewer samples for performance)
vec3 computeTransmittanceToLight(vec3 cloudPos, vec3 lightDir) {
    return computeAtmosphericTransmittance(cloudPos, lightDir, 8);
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
vec3 computeRayleighScatteringBlended(float density, float phase, float sunAltitude) {
    // Standard RGB Rayleigh
    vec3 scatterRGB = density * RAYLEIGH_SCATTERING_BASE * phase;

    // LMS-space Rayleigh for more accurate sunsets
    vec3 scatterLMS = density * RAYLEIGH_LMS * phase;
    vec3 lmsInRGB = LMS_TO_RGB * scatterLMS;

    // Blend: use LMS more when sun is near horizon (better sunset colors)
    // and standard RGB when sun is high (cleaner blue sky)
    float lmsBlend = smoothstep(0.3, -0.1, sunAltitude);  // 0 at high sun, 1 at sunset
    return mix(scatterRGB, lmsInRGB, lmsBlend * 0.7);  // Max 70% LMS contribution
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

    float stepSize = (end - start) / float(sampleCount);
    vec3 transmittance = vec3(1.0);
    vec3 inscatter = vec3(0.0);

    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 moonDir = normalize(ubo.moonDirection.xyz);
    float cosViewSun = dot(dir, sunDir);
    float cosViewMoon = dot(dir, moonDir);
    float rayleighPSun = rayleighPhase(cosViewSun);
    float rayleighPMoon = rayleighPhase(cosViewMoon);
    float miePSun = cornetteShanksPhase(cosViewSun, MIE_ANISOTROPY);
    float miePMoon = cornetteShanksPhase(cosViewMoon, MIE_ANISOTROPY);

    float sunAltitude = sunDir.y;
    float moonAltitude = moonDir.y;
    float moonIntensity = ubo.moonDirection.w;

    // Smooth twilight transition - moon scattering fades in during sunset
    // At sun altitude 0.17 (10°): no moon atmospheric scattering
    // At sun altitude -0.1 (-6°): full moon atmospheric scattering
    float twilightFactor = smoothstep(0.17, -0.1, sunAltitude);
    float moonVisibility = smoothstep(-0.09, 0.1, moonAltitude);
    float moonAtmoContribution = twilightFactor * moonVisibility;

    for (int i = 0; i < sampleCount; i++) {
        float t = start + (float(i) + 0.5) * stepSize;
        vec3 pos = origin + dir * t;
        float altitude = max(length(pos) - PLANET_RADIUS, 0.0);

        float rayleighDensity = exp(-altitude / RAYLEIGH_SCALE_HEIGHT);
        float mieDensity = exp(-altitude / MIE_SCALE_HEIGHT);
        float ozone = ozoneDensity(altitude);

        // Sun contribution: Use blended LMS/RGB Rayleigh for accurate sunset colors
        vec3 rayleighScatterSun = computeRayleighScatteringBlended(rayleighDensity, rayleighPSun, sunAltitude);
        vec3 mieScatterSun = mieDensity * vec3(MIE_SCATTERING_BASE);

        // Moon contribution: simpler Rayleigh scattering (moonlight is dimmer, less color sensitivity needed)
        vec3 rayleighScatterMoon = rayleighDensity * RAYLEIGH_SCATTERING_BASE * rayleighPMoon;
        vec3 mieScatterMoon = mieDensity * vec3(MIE_SCATTERING_BASE);

        // Extinction uses standard RGB coefficients
        vec3 rayleighScatterRGB = rayleighDensity * RAYLEIGH_SCATTERING_BASE;
        vec3 extinction = rayleighScatterRGB + mieScatterSun +
                          mieDensity * vec3(MIE_ABSORPTION_BASE) +
                          ozone * OZONE_ABSORPTION;

        // Earth shadow modulates in-scattering (Phase 4.1.8)
        float earthShadowSun = computeEarthShadow(pos, sunDir);
        float earthShadowMoon = computeEarthShadow(pos, moonDir);

        // Combine sun scattering
        vec3 segmentScatterSun = (rayleighScatterSun + mieScatterSun * miePSun) * earthShadowSun;

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

    return ScatteringResult(inscatter, transmittance);
}

// Compute sky irradiance by sampling the atmosphere in multiple directions
// This replaces hardcoded ambient colors with physically-based values
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

    // Sample directions for hemisphere integration (cosine-weighted)
    // Using 6 directions for performance (up, and 5 around horizon)
    const int NUM_SAMPLES = 6;
    vec3 sampleDirs[NUM_SAMPLES];
    sampleDirs[0] = vec3(0.0, 1.0, 0.0);    // Zenith
    sampleDirs[1] = vec3(1.0, 0.3, 0.0);    // East elevated
    sampleDirs[2] = vec3(-1.0, 0.3, 0.0);   // West elevated
    sampleDirs[3] = vec3(0.0, 0.3, 1.0);    // North elevated
    sampleDirs[4] = vec3(0.0, 0.3, -1.0);   // South elevated
    sampleDirs[5] = vec3(0.707, 0.1, 0.707); // Horizon diagonal

    float weights[NUM_SAMPLES];
    weights[0] = 0.30;  // Zenith has highest weight
    weights[1] = 0.14;
    weights[2] = 0.14;
    weights[3] = 0.14;
    weights[4] = 0.14;
    weights[5] = 0.14;

    float totalWeight = 0.0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        vec3 dir = normalize(sampleDirs[i]);

        // Simple atmospheric scattering sample (reduced quality for irradiance)
        ScatteringResult scatter = integrateAtmosphere(position, dir, 8);

        // Apply sun and moon light
        vec3 skyColor = scatter.inscatter * sunLight;
        if (moonContribution > 0.01) {
            skyColor += scatter.inscatter * moonLight * 0.5 * moonContribution;
        }

        // Night minimum to prevent total darkness
        float night = 1.0 - smoothstep(-0.05, 0.08, sunAltitude);
        skyColor += night * vec3(0.01, 0.015, 0.03);

        // Weight by cosine and sample weight
        float cosWeight = max(dir.y, 0.0);
        float weight = weights[i] * (0.2 + 0.8 * cosWeight);
        result.skyIrradiance += skyColor * weight;
        totalWeight += weight;
    }

    result.skyIrradiance /= totalWeight;

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
    vec3 sunLight = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 moonLight = ubo.moonColor.rgb * ubo.moonDirection.w;

    float sunAltitude = ubo.sunDirection.y;
    float moonAltitude = ubo.moonDirection.y;

    // Smooth twilight transition factor - moon fades in as sun approaches/passes horizon
    float twilightFactor = smoothstep(0.17, -0.1, sunAltitude);
    float moonVisibility = smoothstep(-0.09, 0.1, moonAltitude);
    float moonContribution = twilightFactor * moonVisibility;

    // Compute physically-based sky irradiance at cloud altitude
    // Sample position in middle of cloud layer for irradiance calculation
    vec3 cloudSamplePos = vec3(0.0, PLANET_RADIUS + (CLOUD_LAYER_BOTTOM + CLOUD_LAYER_TOP) * 0.5, 0.0);
    SkyIrradiance skyIrrad = computeSkyIrradiance(cloudSamplePos, sunDir, moonDir,
                                                   sunLight, moonLight,
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
    if (dir.y < 0.0) return 0.0;

    // Apply sidereal rotation based on Julian day
    dir = rotateSidereal(dir, ubo.julianDay);

    float theta = atan(dir.z, dir.x);
    float phi = asin(clamp(dir.y, -1.0, 1.0));

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

    return star * brightness * nightFactor;
}

float celestialDisc(vec3 dir, vec3 celestialDir, float size) {
    float d = dot(normalize(dir), normalize(celestialDir));
    return smoothstep(1.0 - size, 1.0 - size * 0.3, d);
}

// Lunar phase mask - creates moon phases using a simple 2D approach
// phase: 0 = new moon, 0.25 = first quarter, 0.5 = full moon, 0.75 = last quarter, 1 = new moon
// Returns 0-1 illumination factor
float lunarPhaseMask(vec3 dir, vec3 moonDir, float phase, float discSize) {
    vec3 viewDir = normalize(dir);
    vec3 moonCenter = normalize(moonDir);

    // Calculate angular distance from moon center
    float centerDot = dot(viewDir, moonCenter);
    float angularDist = acos(clamp(centerDot, -1.0, 1.0));

    // Outside the disc
    if (angularDist > discSize) return 0.0;

    // Create coordinate system on moon disc
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

    // Phase angle determines terminator position
    // phase 0.0: new moon (terminator at center, facing viewer)
    // phase 0.5: full moon (terminator behind moon, all visible)
    // phase 1.0: new moon again
    float phaseAngle = (phase - 0.5) * 2.0 * PI;

    // Terminator moves across the disc based on phase
    // For crescent, we need to consider the sphere curvature
    float sunAngle = phaseAngle;
    vec3 sunLocalDir = vec3(sin(sunAngle), 0.0, cos(sunAngle));

    // Surface normal at this point
    vec3 normal = normalize(vec3(x, y, z));

    // Lambertian lighting
    float lighting = dot(normal, sunLocalDir);

    // Smooth terminator
    float lit = smoothstep(-0.1, 0.1, lighting);

    // Earthshine (subtle light on dark side, reduced for better contrast)
    return max(lit, 0.05);
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
    float moonVisibility = smoothstep(-0.09, 0.1, moonAltitude);
    float moonSkyContribution = twilightFactor * moonVisibility;

    // Remap the ray direction for atmosphere calculation:
    // - Rays with Y >= 0 use their actual direction
    // - Rays with Y < 0 blend to fog/horizon color to disguise the end of the world
    if (normDir.y < -0.001) {
        // Sample the horizon atmosphere by using a horizontal ray
        vec3 horizonDir = normalize(vec3(normDir.x, 0.0, normDir.z));
        ScatteringResult horizonResult = integrateAtmosphere(vec3(0.0, PLANET_RADIUS + 0.001, 0.0), horizonDir, 16);

        vec3 sunLight = ubo.sunColor.rgb * ubo.sunDirection.w;
        vec3 moonLight = ubo.moonColor.rgb * ubo.moonDirection.w;

        // Sun contribution to horizon
        vec3 horizonColor = horizonResult.inscatter * sunLight;

        // Moon contribution - fades in smoothly during twilight
        if (moonSkyContribution > 0.01) {
            horizonColor += horizonResult.inscatter * moonLight * moonSkyContribution;
        }

        // Multiple scattering compensation (energy-conserving)
        vec3 horizonTransmittance = horizonResult.transmittance;
        horizonColor += sunLight * 0.08 * (1.0 - horizonTransmittance);
        if (moonSkyContribution > 0.01) {
            horizonColor += moonLight * 0.04 * (1.0 - horizonTransmittance) * moonSkyContribution;
        }

        // Night sky floor (energy-conserving blend, not additive)
        float nightFactor = 1.0 - smoothstep(-0.1, 0.08, sunAltitude);
        vec3 nightHorizonRadiance = vec3(0.008, 0.012, 0.02);
        float nightBlend = nightFactor * (1.0 - moonSkyContribution * 0.5);
        horizonColor = mix(horizonColor, max(horizonColor, nightHorizonRadiance), nightBlend);

        // Blend from horizon color to slightly darker fog as we go further below horizon
        // This creates a smooth transition and disguises the world boundary
        float belowHorizonFactor = clamp(-normDir.y * 2.0, 0.0, 1.0);
        vec3 fogColor = horizonColor * mix(1.0, 0.5, belowHorizonFactor);

        return fogColor;
    }

    // Place observer on planet surface (camera height is negligible for atmosphere)
    vec3 origin = vec3(0.0, PLANET_RADIUS + 0.001, 0.0);

    ScatteringResult result = integrateAtmosphere(origin, normDir, 24);

    vec3 sunLight = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 moonLight = ubo.moonColor.rgb * ubo.moonDirection.w;

    // Compute atmospheric transmittance from viewer to sky (for energy conservation)
    vec3 skyTransmittance = result.transmittance;

    // Sky inscatter from sun - primary contribution during day
    vec3 sunSkyContrib = result.inscatter * sunLight;

    // Moon sky contribution - fades in smoothly during twilight
    // Use consistent twilight factor from above (moonSkyContribution)
    vec3 moonSkyContrib = vec3(0.0);
    if (moonSkyContribution > 0.01) {
        // Moon illuminates the atmosphere similar to sun but dimmer
        moonSkyContrib = result.inscatter * moonLight * moonSkyContribution;
    }

    // Multiple scattering compensation (approximates light scattered more than once)
    // This is energy-conserving: based on what wasn't transmitted
    vec3 multiScatterSun = sunLight * 0.08 * (1.0 - skyTransmittance);
    vec3 multiScatterMoon = moonLight * 0.04 * (1.0 - skyTransmittance) * moonSkyContribution;

    // Combine all sky contributions
    vec3 sky = sunSkyContrib + moonSkyContrib + multiScatterSun + multiScatterMoon;

    // Night sky radiance - represents the dark sky with slight airglow
    // This is a minimum floor, not additive, to prevent color shifts
    float nightFactor = 1.0 - smoothstep(-0.1, 0.08, sunAltitude);
    vec3 nightSkyRadiance = mix(vec3(0.005, 0.008, 0.015), vec3(0.015, 0.02, 0.035), normDir.y * 0.5 + 0.5);

    // Blend toward night sky when transitioning - energy conserving blend
    // During twilight, the sky naturally transitions; at deep night, use floor radiance
    float nightBlend = nightFactor * (1.0 - moonSkyContribution * 0.5);
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
    // This uses the inscatter we already computed, scaled by how much cloud is visible
    vec3 hazeLight = sunLight;
    if (moonSkyContribution > 0.01) {
        hazeLight += moonLight * 0.3 * moonSkyContribution;
    }
    vec3 hazeInFront = result.inscatter * hazeLight * (1.0 - clouds.transmittance) * 0.3;

    // Final composite: sky behind clouds + cloud color + haze in front
    sky = skyBehindClouds + cloudColor + hazeInFront;

    // Sun and moon discs (rendered behind clouds)
    // Only show sun/moon if clouds don't fully occlude them
    float sunDisc = celestialDisc(dir, ubo.sunDirection.xyz, SUN_ANGULAR_RADIUS);
    sky += sunLight * sunDisc * 20.0 * result.transmittance * clouds.transmittance;

    // Moon disc with lunar phase simulation
    float moonDisc = celestialDisc(dir, ubo.moonDirection.xyz, MOON_ANGULAR_RADIUS);
    float moonPhase = ubo.moonColor.a;  // Phase: 0 = new, 0.5 = full, 1 = new
    float phaseMask = lunarPhaseMask(dir, ubo.moonDirection.xyz, moonPhase, MOON_ANGULAR_RADIUS);

    // Apply phase mask with very high intensity to ensure bloom triggers
    // Moon surface is highly reflective (albedo ~0.12), but we boost for visual impact
    // During full moon, this should create a strong bloom halo
    float moonIntensity = 25.0 * phaseMask;  // Higher than sun (20.0) when fully lit
    sky += ubo.moonColor.rgb * moonDisc * moonIntensity * ubo.moonDirection.w *
           clamp(result.transmittance, vec3(0.2), vec3(1.0)) * clouds.transmittance;

    // Star field blended over the atmospheric tint (also behind clouds)
    // Stars are already modulated by nightFactor inside starField()
    float stars = starField(dir);
    sky += vec3(stars) * clouds.transmittance;

    return sky;
}

void main() {
    vec3 dir = normalize(rayDir);
    vec3 sky = renderAtmosphere(dir);
    outColor = vec4(sky, 1.0);
}
