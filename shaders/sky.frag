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
    float timeOfDay;
    float shadowMapSize;
} ubo;

layout(location = 0) in vec3 rayDir;
layout(location = 0) out vec4 outColor;

float hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

vec3 getSkyColor(vec3 dir) {
    float y = max(dir.y, 0.0);

    vec3 dayHorizon = vec3(0.7, 0.85, 1.0);
    vec3 dayZenith = vec3(0.25, 0.55, 0.95);

    vec3 nightHorizon = vec3(0.05, 0.05, 0.12);
    vec3 nightZenith = vec3(0.01, 0.01, 0.03);

    vec3 sunsetHorizon = vec3(1.0, 0.5, 0.2);

    float dayFactor = smoothstep(-0.1, 0.3, ubo.sunDirection.y);
    float sunsetFactor = smoothstep(0.0, 0.2, ubo.sunDirection.y) *
                         (1.0 - smoothstep(0.2, 0.5, ubo.sunDirection.y));

    vec3 horizon = mix(nightHorizon, dayHorizon, dayFactor);
    horizon = mix(horizon, sunsetHorizon, sunsetFactor * 0.8);

    vec3 zenith = mix(nightZenith, dayZenith, dayFactor);

    float gradientT = pow(y, 0.4);
    return mix(horizon, zenith, gradientT);
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
