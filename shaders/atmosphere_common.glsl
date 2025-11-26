// Atmosphere common functions and constants
// Based on Phase 4.1 documentation (Bruneton & Neyret, Hillaire, Ghost of Tsushima)

#ifndef ATMOSPHERE_COMMON_GLSL
#define ATMOSPHERE_COMMON_GLSL

const float PI = 3.14159265359;

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

// Atmosphere density at altitude (in meters above planet surface)
vec3 GetAtmosphereDensity(float altitude, AtmosphereParams params) {
    // Rayleigh density (exponential falloff)
    float rayleighDensity = exp(-altitude / params.rayleighScaleHeight);

    // Mie density (exponential falloff, shorter scale height)
    float mieDensity = exp(-altitude / params.mieScaleHeight);

    // Ozone absorption (peak at ozoneLayerCenter)
    float ozoneHeight = max(0.0, ozoneLayerCenter - abs(altitude - params.ozoneLayerCenter));
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

#endif // ATMOSPHERE_COMMON_GLSL
