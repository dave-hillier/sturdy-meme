#version 450

#extension GL_GOOGLE_include_directive : require

#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "atmosphere_common.glsl"

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];
    vec4 cascadeSplits;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;
    vec4 pointLightColor;
    vec4 windDirectionAndSpeed;
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;
} ubo;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2DArrayShadow shadowMapArray;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float roughness;
    float metallic;
    float padding[2];
} material;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragTangent;

layout(location = 0) out vec4 outColor;

// Procedural noise functions for better texturing
float hash1(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 hash2(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

// Value noise
float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash1(i);
    float b = hash1(i + vec2(1.0, 0.0));
    float c = hash1(i + vec2(0.0, 1.0));
    float d = hash1(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// FBM (Fractal Brownian Motion)
float fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * valueNoise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

// Procedural brick pattern
vec3 brickPattern(vec2 uv, out float edgeFactor) {
    // Brick dimensions
    float brickWidth = 1.0;
    float brickHeight = 0.4;
    float mortarWidth = 0.05;

    // Offset every other row
    float row = floor(uv.y / brickHeight);
    float offset = mod(row, 2.0) * 0.5 * brickWidth;

    vec2 brickUV = vec2(uv.x + offset, uv.y);
    vec2 brickPos = vec2(
        mod(brickUV.x, brickWidth),
        mod(brickUV.y, brickHeight)
    );

    // Calculate mortar (edges between bricks)
    float mortarX = step(brickPos.x, mortarWidth) + step(brickWidth - mortarWidth, brickPos.x);
    float mortarY = step(brickPos.y, mortarWidth * 0.7) + step(brickHeight - mortarWidth * 0.7, brickPos.y);
    float mortar = max(mortarX, mortarY);

    edgeFactor = 1.0 - mortar * 0.5;

    // Brick color variation per brick
    vec2 brickID = floor(brickUV / vec2(brickWidth, brickHeight));
    float brickVariation = hash1(brickID);

    // Base brick color (warm tan/brown)
    vec3 baseColor = mix(
        vec3(0.72, 0.58, 0.45),  // Light tan
        vec3(0.55, 0.42, 0.32),  // Darker brown
        brickVariation
    );

    // Add noise variation within brick
    float noiseVal = fbm(uv * 8.0, 3);
    baseColor *= 0.9 + noiseVal * 0.2;

    // Mortar color
    vec3 mortarColor = vec3(0.65, 0.62, 0.58);

    return mix(baseColor, mortarColor, mortar);
}

// Procedural roof tile pattern
vec3 roofTilePattern(vec2 uv, out float edgeFactor) {
    // Tile dimensions
    float tileWidth = 0.3;
    float tileHeight = 0.5;

    // Offset every other row for overlapping effect
    float row = floor(uv.y / tileHeight);
    float offset = mod(row, 2.0) * 0.5 * tileWidth;

    vec2 tileUV = vec2(uv.x + offset, uv.y);
    vec2 tilePos = vec2(
        mod(tileUV.x, tileWidth),
        mod(tileUV.y, tileHeight)
    );

    // Curved tile edge (rounded at bottom)
    float curve = 1.0 - smoothstep(0.0, 0.15, tilePos.y);
    float edge = smoothstep(0.0, 0.03, tilePos.x) * smoothstep(0.0, 0.03, tileWidth - tilePos.x);

    edgeFactor = mix(1.0, 0.7, curve * (1.0 - edge));

    // Tile color variation
    vec2 tileID = floor(tileUV / vec2(tileWidth, tileHeight));
    float tileVariation = hash1(tileID);

    // Terracotta roof color
    vec3 baseColor = mix(
        vec3(0.55, 0.28, 0.20),  // Dark terracotta
        vec3(0.70, 0.38, 0.25),  // Light terracotta
        tileVariation
    );

    // Add weathering
    float weathering = fbm(uv * 12.0 + tileID, 3);
    baseColor = mix(baseColor, baseColor * 0.7, weathering * 0.3);

    return baseColor;
}

// Procedural wood plank pattern
vec3 woodPattern(vec2 uv, out float edgeFactor) {
    float plankWidth = 0.25;

    float plankID = floor(uv.x / plankWidth);
    float plankPos = mod(uv.x, plankWidth);

    // Gap between planks
    float gap = smoothstep(0.0, 0.02, plankPos) * smoothstep(0.0, 0.02, plankWidth - plankPos);
    edgeFactor = gap;

    // Wood grain
    float grain = sin(uv.y * 30.0 + hash1(vec2(plankID, 0.0)) * 10.0) * 0.5 + 0.5;
    grain = pow(grain, 3.0);

    // Plank variation
    float plankVariation = hash1(vec2(plankID, 1.0));

    // Wood color
    vec3 baseColor = mix(
        vec3(0.45, 0.32, 0.20),  // Dark wood
        vec3(0.60, 0.45, 0.28),  // Light wood
        plankVariation
    );

    baseColor = mix(baseColor, baseColor * 0.85, grain * 0.3);

    // Dark gaps
    baseColor = mix(vec3(0.15, 0.10, 0.05), baseColor, gap);

    return baseColor;
}

// Triplanar mapping - sample texture based on world position and normal
vec3 triplanarSample(vec3 worldPos, vec3 normal, float scale, out float avgEdge) {
    vec3 absNormal = abs(normal);
    vec3 blend = absNormal / (absNormal.x + absNormal.y + absNormal.z + 0.001);

    // Sample coordinates for each axis
    vec2 uvX = worldPos.zy * scale;
    vec2 uvY = worldPos.xz * scale;
    vec2 uvZ = worldPos.xy * scale;

    float edgeX, edgeY, edgeZ;

    // Determine if this is more likely roof (upward-facing) or wall
    bool isRoof = normal.y > 0.7;
    bool isFloor = normal.y < -0.5;

    vec3 colorX, colorY, colorZ;

    if (isRoof) {
        colorX = roofTilePattern(uvX, edgeX);
        colorY = roofTilePattern(uvY, edgeY);
        colorZ = roofTilePattern(uvZ, edgeZ);
    } else if (isFloor) {
        // Bottom faces - use wood
        colorX = woodPattern(uvX, edgeX);
        colorY = woodPattern(uvY, edgeY);
        colorZ = woodPattern(uvZ, edgeZ);
    } else {
        // Walls - use brick
        colorX = brickPattern(uvX, edgeX);
        colorY = brickPattern(uvY, edgeY);
        colorZ = brickPattern(uvZ, edgeZ);
    }

    avgEdge = blend.x * edgeX + blend.y * edgeY + blend.z * edgeZ;

    return colorX * blend.x + colorY * blend.y + colorZ * blend.z;
}

// Calculate PBR lighting for a single light
vec3 calculatePBR(vec3 N, vec3 V, vec3 L, vec3 lightColor, float lightIntensity, vec3 albedo, float shadow) {
    vec3 H = normalize(V + L);

    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0001);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    // Dielectric F0 (0.04 is typical for non-metals)
    vec3 F0 = mix(vec3(0.04), albedo, material.metallic);

    // Specular BRDF
    float D = D_GGX(NoH, material.roughness);
    float Vis = V_SmithGGX(NoV, NoL, material.roughness);
    vec3 F = F_Schlick(VoH, F0);

    vec3 specular = D * Vis * F;

    // Energy-conserving diffuse
    vec3 kD = (1.0 - F) * (1.0 - material.metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * lightColor * lightIntensity * NoL * shadow;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Use triplanar mapping for procedural textures based on world position
    // This avoids UV stretching issues from scaled unit meshes
    float edgeFactor;
    vec3 albedo = triplanarSample(fragWorldPos, N, 1.0, edgeFactor);

    // Apply slight roughness variation based on surface detail
    float adjustedRoughness = material.roughness * edgeFactor;

    // Calculate shadow for sun
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    float shadow = calculateCascadedShadow(
        fragWorldPos, N, sunL,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Sun lighting with shadow
    float sunIntensity = ubo.sunDirection.w;
    vec3 sunLight = calculatePBR(N, V, sunL, ubo.sunColor.rgb, sunIntensity, albedo, shadow);

    // Moon lighting (soft fill light)
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonIntensity = ubo.moonDirection.w;
    vec3 moonLight = calculatePBR(N, V, moonL, ubo.moonColor.rgb, moonIntensity, albedo, 1.0);

    // Ambient lighting
    vec3 F0 = mix(vec3(0.04), albedo, material.metallic);
    vec3 ambientDiffuse = ubo.ambientColor.rgb * albedo * (1.0 - material.metallic);
    float envReflection = mix(0.3, 1.0, adjustedRoughness);
    vec3 ambientSpecular = ubo.ambientColor.rgb * F0 * material.metallic * envReflection;
    vec3 ambient = ambientDiffuse + ambientSpecular;

    vec3 finalColor = ambient + sunLight + moonLight;

    // Apply aerial perspective
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 atmosphericColor = applyAerialPerspective(finalColor, ubo.cameraPosition.xyz, normalize(cameraToFrag), length(cameraToFrag), sunDir, sunColor);

    // Debug cascade visualization
    if (ubo.debugCascades > 0.5) {
        int cascade = getCascadeForDebug(fragWorldPos, ubo.view, ubo.cascadeSplits);
        vec3 cascadeColor = getCascadeDebugColor(cascade);
        atmosphericColor = mix(atmosphericColor, cascadeColor, 0.3);
    }

    outColor = vec4(atmosphericColor, 1.0);
}
