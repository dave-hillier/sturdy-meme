// Atmosphere common functions and constants
// Based on Phase 4.1 documentation (Bruneton & Neyret, Hillaire, Ghost of Tsushima)
//
// IMPORTANT: Shaders that include this file must include ubo_common.glsl BEFORE
// including atmosphere_common.glsl. The fog and scattering functions use
// ubo.atmosRayleighScattering, ubo.atmosMieParams, ubo.heightFogParams, etc.

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

// ============================================================================
// UBO-Dependent Functions (require ubo_common.glsl to be included first)
// These functions use runtime parameters from the UBO for UI control
// ============================================================================
#ifdef UBO_COMMON_GLSL

// Simplified ozone density (uses UBO params for UI control)
float ozoneDensity(float altitude) {
    float ozoneCenter = ubo.atmosOzoneAbsorption.w;  // Center stored in w component
    float ozoneWidth = max(ubo.atmosOzoneWidth, 0.1);  // Prevent division by zero
    float z = (altitude - ozoneCenter) / ozoneWidth;
    return exp(-0.5 * z * z);
}

// Integrate atmospheric scattering along a ray (uses UBO params for UI control)
ScatteringResult integrateAtmosphere(vec3 origin, vec3 dir, float maxDistance, int sampleCount, vec3 sunDirection) {
    vec2 atmo = raySphereIntersect(origin, dir, ATMOSPHERE_RADIUS);
    float start = max(atmo.x, 0.0);
    float end = min(atmo.y, maxDistance);

    if (end <= start) {
        return ScatteringResult(vec3(0.0), vec3(1.0));
    }

    // Extract atmosphere parameters from UBO (with safety clamps to prevent division by zero)
    vec3 rayleighScatteringBase = ubo.atmosRayleighScattering.xyz;
    float rayleighScaleHeight = max(ubo.atmosRayleighScattering.w, 0.1);
    float mieScatteringBase = ubo.atmosMieParams.x;
    float mieAbsorptionBase = ubo.atmosMieParams.y;
    float mieScaleHeight = max(ubo.atmosMieParams.z, 0.1);
    float mieAnisotropy = clamp(ubo.atmosMieParams.w, -0.99, 0.99);  // Avoid singularity at |g|=1
    vec3 ozoneAbsorption = ubo.atmosOzoneAbsorption.xyz;

    float stepSize = (end - start) / float(sampleCount);
    vec3 transmittance = vec3(1.0);
    vec3 inscatter = vec3(0.0);

    vec3 sunDir = normalize(sunDirection);
    float cosViewSun = dot(dir, sunDir);
    float rayleighP = RayleighPhase(cosViewSun);
    float mieP = CornetteShanksMiePhase(cosViewSun, mieAnisotropy);

    for (int i = 0; i < sampleCount; i++) {
        float t = start + (float(i) + 0.5) * stepSize;
        vec3 pos = origin + dir * t;
        float altitude = max(length(pos) - PLANET_RADIUS, 0.0);

        float rayleighDensity = exp(-altitude / rayleighScaleHeight);
        float mieDensity = exp(-altitude / mieScaleHeight);
        float ozone = ozoneDensity(altitude);

        vec3 rayleighScatter = rayleighDensity * rayleighScatteringBase;
        vec3 mieScatter = mieDensity * vec3(mieScatteringBase);

        vec3 extinction = rayleighScatter + mieScatter +
                          mieDensity * vec3(mieAbsorptionBase) +
                          ozone * ozoneAbsorption;

        vec3 segmentScatter = rayleighScatter * rayleighP + mieScatter * mieP;

        vec3 attenuation = exp(-extinction * stepSize);
        inscatter += transmittance * segmentScatter * stepSize;
        transmittance *= attenuation;
    }

    return ScatteringResult(inscatter, transmittance);
}

// ============================================================================
// Height Fog Functions (Phase 4.3 - Volumetric Haze)
// Uses UBO parameters for UI control
// ============================================================================

// Exponential height falloff - good for general atmospheric haze
float exponentialHeightDensity(float height) {
    float fogBaseHeight = ubo.heightFogParams.x;
    float fogScaleHeight = max(ubo.heightFogParams.y, 1.0);  // Prevent division by zero
    float fogDensity = ubo.heightFogParams.z;
    float relativeHeight = height - fogBaseHeight;
    // Clamp exponent to prevent underflow (result would be ~0 anyway for large values)
    float exponent = clamp(-max(relativeHeight, 0.0) / fogScaleHeight, -40.0, 0.0);
    return fogDensity * exp(exponent);
}

// Sigmoidal layer density - good for low-lying ground fog
float sigmoidalLayerDensity(float height) {
    float fogBaseHeight = ubo.heightFogParams.x;
    float layerThickness = max(ubo.heightFogLayerParams.x, 1.0);  // Prevent division by zero
    float layerDensity = ubo.heightFogLayerParams.y;
    float t = (height - fogBaseHeight) / layerThickness;
    // Clamp t to prevent exp overflow (exp(88) ~ FLT_MAX, so clamp well below)
    t = clamp(t, -40.0, 40.0);
    // Smooth transition from full density below to zero above
    return layerDensity / (1.0 + exp(t * 2.0));
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

    // Extract fog params from UBO
    float fogBaseHeight = ubo.heightFogParams.x;
    float fogScaleHeight = max(ubo.heightFogParams.y, 1.0);  // Prevent division by zero
    float fogDensity = ubo.heightFogParams.z;

    // Analytical integration of exponential density along ray
    float invScaleHeight = 1.0 / fogScaleHeight;

    // Clamp exponent arguments to prevent overflow/underflow
    float exp0 = clamp(-((h0 - fogBaseHeight)) * invScaleHeight, -40.0, 40.0);
    float exp1 = clamp(-((h1 - fogBaseHeight)) * invScaleHeight, -40.0, 40.0);

    // Exponential fog component - use numerically stable formulation
    // For near-horizontal rays (small deltaH), use average density * distance
    // For non-horizontal rays, use analytical integration
    // Smoothly blend to avoid discontinuity
    float avgExpDensity = (exp(exp0) + exp(exp1)) * 0.5 * fogDensity;
    float simpleIntegral = avgExpDensity * distance;

    // Analytical integral (valid when deltaH is not too small)
    float sinTheta = deltaH / distance;
    float analyticalIntegral = fogDensity * fogScaleHeight *
        abs(exp(exp0) - exp(exp1)) / max(abs(sinTheta), 0.0001);

    // Smooth blend based on how horizontal the ray is
    // Use simple method for near-horizontal, analytical for steeper rays
    float blendFactor = smoothstep(0.0, 0.02, abs(sinTheta));
    float expIntegral = mix(simpleIntegral, analyticalIntegral, blendFactor);

    // Sigmoidal component (approximate with average)
    float avgSigmoidal = (sigmoidalLayerDensity(h0) + sigmoidalLayerDensity(h1)) * 0.5;
    float sigIntegral = avgSigmoidal * distance;

    // Clamp final result to prevent extreme values
    return clamp(expIntegral + sigIntegral, 0.0, 100.0);
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

    // Convert scene units (meters) to km for atmosphere calculation
    const float SCENE_TO_KM = 0.001;
    float cameraAltitudeKm = max(cameraPos.y, 0.0) * SCENE_TO_KM;
    float viewDistanceKm = viewDistance * SCENE_TO_KM;

    // Place camera at correct altitude in atmosphere coordinate system
    vec3 origin = vec3(0.0, PLANET_RADIUS + cameraAltitudeKm, 0.0);

    // Integrate atmospheric scattering (km scale)
    ScatteringResult result = integrateAtmosphere(origin, normalize(viewDir), viewDistanceKm, 8, sunDir);

    vec3 scatterLight = result.inscatter * (sunColor + vec3(0.02));

    float night = 1.0 - smoothstep(-0.05, 0.08, sunDir.y);
    scatterLight += night * vec3(0.01, 0.015, 0.03) * (1.0 - result.transmittance);

    // Combine: atmospheric scattering adds to fogged scene
    // Scale for large world: gradual ramp over long distances (reaches 0.5 at ~5000 units)
    float atmoBlend = clamp(viewDistance * 0.0001, 0.0, 0.7);
    // Smooth the blend curve for more natural transition
    atmoBlend = atmoBlend * atmoBlend * (3.0 - 2.0 * atmoBlend);  // Smoothstep-like
    vec3 finalColor = mix(fogged, fogged * result.transmittance + scatterLight, atmoBlend);

    return finalColor;
}

// Simplified aerial perspective - calculates all parameters from fragment world position
// Use this when you don't need the intermediate values for other calculations
vec3 applyAerialPerspectiveSimple(vec3 color, vec3 fragWorldPos) {
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDist = length(cameraToFrag);
    vec3 viewDir = normalize(cameraToFrag);
    vec3 sunDir = normalize(ubo.toSunDirection.xyz);
    vec3 sunColor = ubo.sunColor.rgb * ubo.toSunDirection.w;
    return applyAerialPerspective(color, ubo.cameraPosition.xyz, viewDir, viewDist, sunDir, sunColor);
}

#endif // UBO_COMMON_GLSL

#endif // ATMOSPHERE_COMMON_GLSL
