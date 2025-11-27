// Atmosphere common functions and constants
// Based on Phase 4.1 documentation (Bruneton & Neyret, Hillaire, Ghost of Tsushima)

#ifndef ATMOSPHERE_COMMON_GLSL
#define ATMOSPHERE_COMMON_GLSL

#include "constants_common.glsl"

// Atmosphere parameters structure (must match C++ struct)
struct AtmosphereParams {
    float planetRadius;
    float atmosphereRadius;
    float pad1, pad2;

    vec3 rayleighScatteringBase;
    float rayleighScaleHeight;

    float mieScatteringBase;
    float mieAbsorptionBase;
    float mieScaleHeight;
    float mieAnisotropy;

    vec3 ozoneAbsorption;
    float ozoneLayerCenter;

    float ozoneLayerWidth;
    float sunAngularRadius;
    float pad3, pad4;

    vec3 solarIrradiance;
    float pad5;
};

// Phase functions
float RayleighPhase(float cosTheta) {
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

float HenyeyGreensteinPhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

float CornetteShanksMiePhase(float cosTheta, float g) {
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float denom = 8.0 * PI * (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / denom;
}

// Atmosphere density at altitude (in same units as params - typically km)
vec3 GetAtmosphereDensity(float altitude, AtmosphereParams params) {
    // Rayleigh density (exponential falloff)
    float rayleighDensity = exp(-altitude / params.rayleighScaleHeight);

    // Mie density (exponential falloff, shorter scale height)
    float mieDensity = exp(-altitude / params.mieScaleHeight);

    // Ozone absorption (peak at ozoneLayerCenter)
    float ozoneHeight = max(0.0, params.ozoneLayerWidth - abs(altitude - params.ozoneLayerCenter));
    float ozoneDensity = ozoneHeight / params.ozoneLayerWidth;

    return vec3(rayleighDensity, mieDensity, ozoneDensity);
}

// Ray-sphere intersection
// Returns vec2(tMin, tMax) or vec2(-1, -1) if no intersection
vec2 RaySphereIntersection(vec3 rayOrigin, vec3 rayDir, vec3 sphereCenter, float sphereRadius) {
    vec3 oc = rayOrigin - sphereCenter;
    float a = dot(rayDir, rayDir);
    float b = 2.0 * dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4.0 * a * c;

    if (discriminant < 0.0) {
        return vec2(-1.0, -1.0);
    }

    float sqrtD = sqrt(discriminant);
    float t0 = (-b - sqrtD) / (2.0 * a);
    float t1 = (-b + sqrtD) / (2.0 * a);

    return vec2(t0, t1);
}

// Distance to atmosphere boundary from point at altitude r with view zenith angle mu
float DistanceToAtmosphereBoundary(float r, float mu, float atmosphereRadius) {
    float discriminant = r * r * (mu * mu - 1.0) + atmosphereRadius * atmosphereRadius;
    return max(0.0, -r * mu + sqrt(max(0.0, discriminant)));
}

// Distance to planet surface from point at altitude r with view zenith angle mu
float DistanceToPlanetSurface(float r, float mu, float planetRadius) {
    float discriminant = r * r * (mu * mu - 1.0) + planetRadius * planetRadius;
    if (discriminant < 0.0) {
        return -1.0; // Ray doesn't hit planet
    }
    return max(0.0, -r * mu - sqrt(discriminant));
}

// Check if ray from point at distance r with zenith angle mu intersects planet
bool RayIntersectsPlanet(float r, float mu, float planetRadius) {
    return mu < 0.0 && r * r * (mu * mu - 1.0) + planetRadius * planetRadius >= 0.0;
}

// UV to transmittance LUT parameters
void UVToTransmittanceLUTParams(vec2 uv, out float r, out float mu, AtmosphereParams params) {
    float H = sqrt(params.atmosphereRadius * params.atmosphereRadius -
                   params.planetRadius * params.planetRadius);
    float rho = H * uv.y;
    r = sqrt(rho * rho + params.planetRadius * params.planetRadius);

    float dMin = params.atmosphereRadius - r;
    float dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);

    mu = (d == 0.0) ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

// Transmittance LUT parameters to UV
vec2 TransmittanceLUTParamsToUV(float r, float mu, AtmosphereParams params) {
    float H = sqrt(params.atmosphereRadius * params.atmosphereRadius -
                   params.planetRadius * params.planetRadius);
    float rho = sqrt(max(0.0, r * r - params.planetRadius * params.planetRadius));

    float discriminant = r * r * (mu * mu - 1.0) + params.atmosphereRadius * params.atmosphereRadius;
    float d = max(0.0, -r * mu + sqrt(max(0.0, discriminant)));

    float dMin = params.atmosphereRadius - r;
    float dMax = rho + H;

    float xMu = (d - dMin) / (dMax - dMin);
    float xR = rho / H;

    return vec2(xMu, xR);
}

// Hammersley sequence for quasi-random sampling
vec2 Hammersley(uint i, uint N) {
    uint bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float y = float(bits) * 2.3283064365386963e-10; // / 0x100000000

    return vec2(float(i) / float(N), y);
}

// Uniform hemisphere sampling
vec3 UniformHemisphere(vec2 xi) {
    float phi = 2.0 * PI * xi.x;
    float cosTheta = 1.0 - xi.y;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    return vec3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
}

// Cosine-weighted hemisphere sampling
vec3 CosineHemisphere(vec2 xi) {
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);

    return vec3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
}

// ============================================================================
// Simplified Atmospheric Scattering (used in fragment shaders)
// ============================================================================

// Result structure for scattering calculations
struct ScatteringResult {
    vec3 inscatter;
    vec3 transmittance;
};

// Simplified ray-sphere intersection
vec2 raySphereIntersect(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return vec2(1e9, -1e9);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

// Simplified ozone density
float ozoneDensity(float altitude) {
    float z = (altitude - OZONE_LAYER_CENTER) / OZONE_LAYER_WIDTH;
    return exp(-0.5 * z * z);
}

// Integrate atmospheric scattering along a ray
ScatteringResult integrateAtmosphere(vec3 origin, vec3 dir, float maxDistance, int sampleCount, vec3 sunDirection) {
    vec2 atmo = raySphereIntersect(origin, dir, ATMOSPHERE_RADIUS);
    float start = max(atmo.x, 0.0);
    float end = min(atmo.y, maxDistance);

    if (end <= 0.0) {
        return ScatteringResult(vec3(0.0), vec3(1.0));
    }

    vec2 planet = raySphereIntersect(origin, dir, PLANET_RADIUS);
    if (planet.x > 0.0) {
        end = min(end, planet.x);
    }

    if (end <= start) {
        return ScatteringResult(vec3(0.0), vec3(1.0));
    }

    float stepSize = (end - start) / float(sampleCount);
    vec3 transmittance = vec3(1.0);
    vec3 inscatter = vec3(0.0);

    vec3 sunDir = normalize(sunDirection);
    float cosViewSun = dot(dir, sunDir);
    float rayleighP = RayleighPhase(cosViewSun);
    float mieP = CornetteShanksMiePhase(cosViewSun, MIE_ANISOTROPY);

    for (int i = 0; i < sampleCount; i++) {
        float t = start + (float(i) + 0.5) * stepSize;
        vec3 pos = origin + dir * t;
        float altitude = max(length(pos) - PLANET_RADIUS, 0.0);

        float rayleighDensity = exp(-altitude / RAYLEIGH_SCALE_HEIGHT);
        float mieDensity = exp(-altitude / MIE_SCALE_HEIGHT);
        float ozone = ozoneDensity(altitude);

        vec3 rayleighScatter = rayleighDensity * RAYLEIGH_SCATTERING_BASE;
        vec3 mieScatter = mieDensity * vec3(MIE_SCATTERING_BASE);

        vec3 extinction = rayleighScatter + mieScatter +
                          mieDensity * vec3(MIE_ABSORPTION_BASE) +
                          ozone * OZONE_ABSORPTION;

        vec3 segmentScatter = rayleighScatter * rayleighP + mieScatter * mieP;

        vec3 attenuation = exp(-extinction * stepSize);
        inscatter += transmittance * segmentScatter * stepSize;
        transmittance *= attenuation;
    }

    return ScatteringResult(inscatter, transmittance);
}

// ============================================================================
// Height Fog Functions (Phase 4.3 - Volumetric Haze)
// ============================================================================

// Exponential height falloff - good for general atmospheric haze
float exponentialHeightDensity(float height) {
    float relativeHeight = height - FOG_BASE_HEIGHT;
    return FOG_DENSITY * exp(-max(relativeHeight, 0.0) / FOG_SCALE_HEIGHT);
}

// Sigmoidal layer density - good for low-lying ground fog
float sigmoidalLayerDensity(float height) {
    float t = (height - FOG_BASE_HEIGHT) / FOG_LAYER_THICKNESS;
    // Smooth transition from full density below to zero above
    return FOG_LAYER_DENSITY / (1.0 + exp(t * 2.0));
}

// Combined fog density at a given height
float getHeightFogDensity(float height) {
    return exponentialHeightDensity(height) + sigmoidalLayerDensity(height);
}

// Analytically integrate exponential fog density along a ray
// Returns optical depth (for transmittance calculation)
float integrateExponentialFog(vec3 startPos, vec3 endPos) {
    float h0 = startPos.y;
    float h1 = endPos.y;
    float distance = length(endPos - startPos);

    if (distance < 0.001) return 0.0;

    float deltaH = h1 - h0;

    // For nearly horizontal rays, use simple density * distance
    if (abs(deltaH) < 0.01) {
        float avgHeight = (h0 + h1) * 0.5;
        return getHeightFogDensity(avgHeight) * distance;
    }

    // Analytical integration of exponential density along ray
    float invScaleHeight = 1.0 / FOG_SCALE_HEIGHT;

    // Exponential fog component
    float expIntegral = FOG_DENSITY * FOG_SCALE_HEIGHT *
        abs(exp(-((h0 - FOG_BASE_HEIGHT)) * invScaleHeight) -
            exp(-((h1 - FOG_BASE_HEIGHT)) * invScaleHeight)) /
        max(abs(deltaH / distance), 0.001);

    // Sigmoidal component (approximate with average)
    float avgSigmoidal = (sigmoidalLayerDensity(h0) + sigmoidalLayerDensity(h1)) * 0.5;
    float sigIntegral = avgSigmoidal * distance;

    return expIntegral + sigIntegral;
}

// Apply height fog with in-scattering
vec3 applyHeightFog(vec3 color, vec3 cameraPos, vec3 fragPos, vec3 sunDir, vec3 sunColor) {
    vec3 viewDir = fragPos - cameraPos;
    float viewDistance = length(viewDir);
    viewDir = normalize(viewDir);

    // Calculate fog optical depth along the view ray
    float opticalDepth = integrateExponentialFog(cameraPos, fragPos);

    // Beer-Lambert transmittance
    float transmittance = exp(-opticalDepth);

    // In-scattering from sun (with Mie-like phase function for forward scattering)
    float cosTheta = dot(viewDir, sunDir);
    float phase = CornetteShanksMiePhase(cosTheta, 0.6);  // Slightly lower g for fog

    // Sun visibility (above horizon and not blocked by terrain)
    float sunVisibility = smoothstep(-0.1, 0.1, sunDir.y);

    // Fog color: blend between sun-lit and ambient based on sun angle
    vec3 fogSunColor = sunColor * phase * sunVisibility;

    // Ambient sky light contribution (approximate hemisphere irradiance)
    float night = 1.0 - smoothstep(-0.05, 0.08, sunDir.y);
    vec3 ambientFog = mix(vec3(0.4, 0.5, 0.6), vec3(0.02, 0.03, 0.05), night);

    // Combined in-scatter (energy conserving)
    vec3 inScatter = (fogSunColor + ambientFog * 0.3) * (1.0 - transmittance);

    return color * transmittance + inScatter;
}

// Apply aerial perspective (combined height fog + atmospheric scattering)
vec3 applyAerialPerspective(vec3 color, vec3 cameraPos, vec3 viewDir, float viewDistance, vec3 sunDir, vec3 sunColor) {
    // Reconstruct fragment position from normalized view direction and distance
    vec3 fragPos = cameraPos + viewDir * viewDistance;

    // Apply local height fog first (scene scale)
    vec3 fogged = applyHeightFog(color, cameraPos, fragPos, sunDir, sunColor);

    // Then apply large-scale atmospheric scattering (km scale)
    vec3 origin = vec3(0.0, PLANET_RADIUS + max(cameraPos.y, 0.0), 0.0);
    ScatteringResult result = integrateAtmosphere(origin, normalize(viewDir), viewDistance, 8, sunDir);

    vec3 scatterLight = result.inscatter * (sunColor + vec3(0.02));

    float night = 1.0 - smoothstep(-0.05, 0.08, sunDir.y);
    scatterLight += night * vec3(0.01, 0.015, 0.03) * (1.0 - result.transmittance);

    // Combine: atmospheric scattering adds to fogged scene
    // Use reduced atmospheric effect since we're at scene scale
    float atmoBlend = clamp(viewDistance * 0.001, 0.0, 0.3);
    vec3 finalColor = mix(fogged, fogged * result.transmittance + scatterLight, atmoBlend);

    return finalColor;
}

#endif // ATMOSPHERE_COMMON_GLSL
