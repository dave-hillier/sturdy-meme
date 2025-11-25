#version 450

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
    vec4 rayleighScattering;
    vec4 mieScattering;
    vec4 absorptionExtinction;
    vec4 atmosphereParams;
    float timeOfDay;
    float shadowMapSize;
} ubo;

layout(binding = 4) uniform sampler2D transmittanceLUT;
layout(binding = 5) uniform sampler2D multiScatterLUT;
layout(binding = 6) uniform sampler2D skyViewLUT;

layout(location = 0) in vec3 rayDir;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

vec2 directionToUV(vec3 dir) {
    dir = normalize(dir);
    float u = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    float v = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    return vec2(u, v);
}

vec3 getSkyColor(vec3 dir) {
    vec2 uv = directionToUV(dir);
    vec3 baseSky = texture(skyViewLUT, uv).rgb;

    float sunCos = clamp(dot(normalize(dir), normalize(ubo.sunDirection.xyz)) * 0.5 + 0.5, 0.0, 1.0);
    vec3 multi = texture(multiScatterLUT, vec2(sunCos, uv.y)).rgb;
    vec3 transmit = texture(transmittanceLUT, vec2(uv.y, clamp(ubo.cameraPosition.y / ubo.atmosphereParams.y, 0.0, 1.0))).rgb;

    return baseSky + multi * ubo.sunColor.rgb * transmit;
}

float starField(vec3 dir) {
    float nightFactor = 1.0 - smoothstep(-0.1, 0.2, ubo.sunDirection.y);
    if (nightFactor < 0.01) return 0.0;

    // Only show stars above the horizon
    dir = normalize(dir);
    if (dir.y < 0.0) return 0.0;
    float theta = atan(dir.z, dir.x);  // Azimuth angle [-PI, PI]
    float phi = asin(clamp(dir.y, -1.0, 1.0));  // Elevation angle [-PI/2, PI/2]

    // Create grid in spherical space - scale controls star density
    // Higher scale = smaller stars, lower threshold = more stars
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

void main() {
    vec3 dir = normalize(rayDir);

    vec3 sky = getSkyColor(dir);

    float stars = starField(dir);
    sky += vec3(stars);

    float sunDisc = celestialDisc(dir, ubo.sunDirection.xyz, 0.015);
    sky += ubo.sunColor.rgb * sunDisc * 2.0 * ubo.sunDirection.w;

    float moonDisc = celestialDisc(dir, ubo.moonDirection.xyz, 0.012);
    sky += vec3(0.95, 0.95, 1.0) * moonDisc * 1.5 * ubo.moonDirection.w;

    outColor = vec4(sky, 1.0);
}
