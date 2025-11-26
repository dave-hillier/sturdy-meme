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
    vec4 moonColor;                       // rgb = moon color
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;  // xyz = position, w = intensity
    vec4 pointLightColor;     // rgb = color, a = radius
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float padding;
} ubo;

// Atmosphere LUTs (Phase 4.1)
layout(binding = 6) uniform sampler2D transmittanceLUT;
layout(binding = 7) uniform sampler2D multiScatterLUT;

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

const float SUN_ANGULAR_RADIUS = 0.00935 / 2.0;  // radians

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

    // Wind offset for animation (use timeOfDay for slow drift)
    vec3 windOffset = vec3(ubo.timeOfDay * 50.0, 0.0, ubo.timeOfDay * 20.0);

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

// Henyey-Greenstein phase function for cloud scattering
float hgPhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

// Cloud phase function with depth-dependent scattering (Ghost of Tsushima technique)
float cloudPhase(float cosTheta, float transmittanceToLight, float segmentTransmittance) {
    float opticalDepthFactor = transmittanceToLight * segmentTransmittance;

    // Lerp between back-scatter (dense) and forward-scatter (wispy)
    // Forward scattering dominates in wispy/thin areas, back-scatter in dense areas
    float gForward = 0.8;
    float gBack = -0.15;
    float g = mix(gBack, gForward, opticalDepthFactor);

    float phase = hgPhase(cosTheta, abs(g));

    // Boost back-scatter for multi-scattering approximation
    // Value of 2.16 from Ghost of Tsushima - simulating dense Mie layer with 0.9 albedo
    if (g < 0.0) {
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

// ============================================================================
// Atmosphere LUT sampling functions (Phase 4.1)
// ============================================================================

// Convert altitude and view zenith angle to transmittance LUT UV
vec2 transmittanceLUTParams(float r, float mu) {
    float H = ATMOSPHERE_RADIUS - PLANET_RADIUS;
    float altitude = r - PLANET_RADIUS;

    // Altitude mapping (squared for precision near surface)
    float xR = sqrt(clamp(altitude / H, 0.0, 1.0));

    // Angle mapping
    float discriminant = r * r - PLANET_RADIUS * PLANET_RADIUS;
    float H_at_r = sqrt(max(0.0, discriminant));

    float xMu = clamp((mu + H_at_r) / (2.0 * H_at_r), 0.0, 1.0);

    return vec2(xMu, xR);
}

// Sample transmittance LUT
vec3 sampleTransmittanceLUT(float r, float mu) {
    vec2 uv = transmittanceLUTParams(r, mu);
    return texture(transmittanceLUT, uv).rgb;
}

// Sample multi-scatter LUT
vec3 sampleMultiScatterLUT(float r, float sunCosZenith) {
    float H = ATMOSPHERE_RADIUS - PLANET_RADIUS;
    float altitude = r - PLANET_RADIUS;

    // X: sun zenith angle (-1 to 1)
    float xSun = (sunCosZenith + 1.0) * 0.5;

    // Y: altitude (0 to 1)
    float xAlt = clamp(altitude / H, 0.0, 1.0);

    vec2 uv = vec2(xSun, xAlt);
    return texture(multiScatterLUT, uv).rgb;
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

// March through cloud layer
struct CloudResult {
    vec3 scattering;
    float transmittance;
};

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

    // Approximate sky irradiance at cloud altitude (Ghost of Tsushima approach)
    // In a full implementation, this would come from precomputed irradiance LUTs
    // Here we approximate based on sun position and Rayleigh scattering color
    float sunAltitude = ubo.sunDirection.y;
    float moonAltitude = ubo.moonDirection.y;

    // Smooth twilight transition factor - moon fades in as sun approaches/passes horizon
    // At sun altitude 0.17 (10°): twilightFactor = 0 (no moon contribution to clouds)
    // At sun altitude -0.1 (-6°): twilightFactor = 1 (full moon contribution)
    float twilightFactor = smoothstep(0.17, -0.1, sunAltitude);
    // Also require moon to be reasonably above horizon
    float moonVisibility = smoothstep(-0.09, 0.1, moonAltitude);
    float moonContribution = twilightFactor * moonVisibility;

    // Sky color varies with sun altitude - bluer overhead, warmer at horizon
    vec3 zenithColor = vec3(0.3, 0.5, 0.9);   // Blue zenith
    vec3 horizonColor = vec3(0.7, 0.6, 0.5);  // Warm horizon
    float sunInfluence = smoothstep(-0.1, 0.5, sunAltitude);

    // Base ambient from sky hemisphere
    vec3 skyAmbient = mix(horizonColor * 0.3, zenithColor * 0.5, sunInfluence);

    // Add ground bounce contribution (important for cloud undersides)
    vec3 groundBounce = vec3(0.15, 0.12, 0.08) * max(sunAltitude, 0.0);

    // Gradually blend in moonlit ambient during twilight (cooler blue-grey tones)
    if (moonContribution > 0.01) {
        vec3 moonAmbient = ubo.moonColor.rgb * ubo.moonDirection.w * 0.15;
        skyAmbient = mix(skyAmbient, moonAmbient, moonContribution);
        groundBounce = mix(groundBounce, moonAmbient * 0.3, moonContribution);
    }

    // Combined ambient light - brighter overall to match proper irradiance
    vec3 ambientLight = (skyAmbient + groundBounce) * 0.8;

    for (int i = 0; i < CLOUD_MARCH_STEPS; i++) {
        if (result.transmittance < 0.01) break;

        float t = tStart + (float(i) + 0.5) * stepSize;
        vec3 pos = origin + dir * t;

        float density = sampleCloudDensity(pos);

        if (density > 0.005) {  // Skip very thin cloud regions
            // Sample transmittance to sun
            float transmittanceToSun = sampleCloudTransmittanceToSun(pos, sunDir);

            // Phase function for sun
            float phaseSun = cloudPhase(cosThetaSun, transmittanceToSun, result.transmittance);

            // In-scattering from sun
            vec3 sunScatter = sunLight * transmittanceToSun * phaseSun;

            // Add moon scattering - scales smoothly with twilight transition
            vec3 moonScatter = vec3(0.0);
            if (moonContribution > 0.01) {
                float transmittanceToMoon = sampleCloudTransmittanceToSun(pos, moonDir);
                float phaseMoon = cloudPhase(cosThetaMoon, transmittanceToMoon, result.transmittance);
                moonScatter = moonLight * transmittanceToMoon * phaseMoon * moonContribution;
            }

            // Add ambient scattering
            vec3 totalScatter = sunScatter + moonScatter + ambientLight;

            // Beer-Lambert extinction
            float extinction = density * stepSize * 10.0;
            float segmentTransmittance = exp(-extinction);

            // Energy-conserving integration
            vec3 integScatter = totalScatter * (1.0 - segmentTransmittance);
            result.scattering += result.transmittance * integScatter;
            result.transmittance *= segmentTransmittance;
        }
    }

    return result;
}

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

    // Add multi-scatter contribution for increased brightness (Phase 4.1.4)
    // Sample at middle altitude for approximation
    float sampleAltitude = 4.0;  // km - middle of atmosphere
    float sampleR = PLANET_RADIUS + sampleAltitude;
    vec3 multiScatter = sampleMultiScatterLUT(sampleR, sunDir.y);

    // Multi-scatter adds secondary bounced light
    inscatter += multiScatter * 0.3;

    return ScatteringResult(inscatter, transmittance);
}

float starField(vec3 dir) {
    float nightFactor = 1.0 - smoothstep(-0.1, 0.2, ubo.sunDirection.y);
    if (nightFactor < 0.01) return 0.0;

    dir = normalize(dir);
    if (dir.y < 0.0) return 0.0;
    float theta = atan(dir.z, dir.x);
    float phi = asin(clamp(dir.y, -1.0, 1.0));

    vec2 gridCoord = vec2(theta * 200.0, phi * 200.0);
    vec2 cell = floor(gridCoord);

    vec3 p = vec3(cell, 0.0);
    float h = hash(p);

    float star = step(0.992, h);
    float brightness = hash(p + vec3(1.0)) * 0.5 + 0.5;

    return star * brightness * nightFactor;
}

float celestialDisc(vec3 dir, vec3 celestialDir, float size) {
    float d = dot(normalize(dir), normalize(celestialDir));
    return smoothstep(1.0 - size, 1.0 - size * 0.3, d);
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
        vec3 horizonColor = horizonResult.inscatter * sunLight;

        // Add moon contribution - fades in smoothly during twilight
        if (moonSkyContribution > 0.01) {
            horizonColor += horizonResult.inscatter * moonLight * 0.5 * moonSkyContribution;
        }

        // Add multiple scattering compensation
        horizonColor += sunLight * 0.1 * (1.0 - horizonResult.transmittance);
        if (moonSkyContribution > 0.01) {
            horizonColor += moonLight * 0.05 * (1.0 - horizonResult.transmittance) * moonSkyContribution;
        }

        // Night fallback
        float night = 1.0 - smoothstep(-0.05, 0.08, sunAltitude);
        vec3 nightTint = mix(vec3(0.01, 0.015, 0.03), vec3(0.03, 0.05, 0.08), horizonResult.transmittance.y);
        horizonColor += night * nightTint;

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

    // Sky inscatter is modulated by both sun and moon light
    // The integrateAtmosphere function now handles moon contribution internally
    vec3 sky = result.inscatter * sunLight;

    // Add moon-based atmospheric scattering - fades in smoothly during twilight
    if (moonSkyContribution > 0.01) {
        sky += result.inscatter * moonLight * 0.5 * moonSkyContribution;
    }

    // Add simple multiple scattering compensation to keep horizon bright
    sky += sunLight * 0.1 * (1.0 - result.transmittance);
    if (moonSkyContribution > 0.01) {
        sky += moonLight * 0.05 * (1.0 - result.transmittance) * moonSkyContribution;
    }

    // Night fallback color when sun is below the horizon
    float night = 1.0 - smoothstep(-0.05, 0.08, sunAltitude);
    vec3 nightTint = mix(vec3(0.01, 0.015, 0.03), vec3(0.03, 0.05, 0.08), result.transmittance.y);
    sky += night * nightTint;

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

    float moonDisc = celestialDisc(dir, ubo.moonDirection.xyz, 0.012);
    sky += ubo.moonColor.rgb * moonDisc * 2.0 * ubo.moonDirection.w *
           clamp(result.transmittance, vec3(0.2), vec3(1.0)) * clouds.transmittance;

    // Star field blended over the atmospheric tint (also behind clouds)
    float stars = starField(dir);
    sky += vec3(stars) * (0.5 + 0.5 * night) * clouds.transmittance;

    return sky;
}

void main() {
    vec3 dir = normalize(rayDir);
    vec3 sky = renderAtmosphere(dir);
    outColor = vec4(sky, 1.0);
}
