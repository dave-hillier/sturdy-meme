#version 450

const float PI = 3.14159265359;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;  // xyz = position, w = intensity
    vec4 pointLightColor;     // rgb = color, a = radius
    float timeOfDay;
    float shadowMapSize;
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

const float SUN_ANGULAR_RADIUS = 0.00935 / 2.0;  // radians

float hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
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

vec2 raySphereIntersect(vec3 origin, vec3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return vec2(1e9, -1e9);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

float ozoneDensity(float altitude) {
    float z = (altitude - OZONE_LAYER_CENTER) / OZONE_LAYER_WIDTH;
    return exp(-0.5 * z * z);
}

struct ScatteringResult {
    vec3 inscatter;
    vec3 transmittance;
};

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
    float cosViewSun = dot(dir, sunDir);
    float rayleighP = rayleighPhase(cosViewSun);
    float mieP = cornetteShanksPhase(cosViewSun, MIE_ANISOTROPY);

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
    vec3 origin = vec3(0.0, PLANET_RADIUS + max(ubo.cameraPosition.y, 0.0), 0.0);
    ScatteringResult result = integrateAtmosphere(origin, normalize(dir), 24);

    vec3 sunLight = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 sky = result.inscatter * sunLight;

    // Add simple multiple scattering compensation to keep horizon bright
    sky += sunLight * 0.1 * (1.0 - result.transmittance);

    // Night fallback color when sun is below the horizon
    float night = 1.0 - smoothstep(-0.05, 0.08, ubo.sunDirection.y);
    vec3 nightTint = mix(vec3(0.01, 0.015, 0.03), vec3(0.03, 0.05, 0.08), result.transmittance.y);
    sky += night * nightTint;

    // Sun and moon discs
    float sunDisc = celestialDisc(dir, ubo.sunDirection.xyz, SUN_ANGULAR_RADIUS);
    sky += sunLight * sunDisc * 20.0 * result.transmittance;

    float moonDisc = celestialDisc(dir, ubo.moonDirection.xyz, 0.012);
    sky += vec3(0.95, 0.95, 1.0) * moonDisc * 1.5 * ubo.moonDirection.w *
           clamp(result.transmittance, vec3(0.2), vec3(1.0));

    // Star field blended over the atmospheric tint
    float stars = starField(dir);
    sky += vec3(stars) * (0.5 + 0.5 * night);

    return sky;
}

void main() {
    vec3 dir = normalize(rayDir);
    vec3 sky = renderAtmosphere(dir);
    outColor = vec4(sky, 1.0);
}
