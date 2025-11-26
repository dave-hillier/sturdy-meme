#version 450

/*
 * terrain.frag - Terrain fragment shader
 * Simple PBR-based terrain rendering with shadow support
 */

const float PI = 3.14159265359;
const int NUM_CASCADES = 4;

// Use the same UBO as main scene for consistent lighting
layout(binding = 5) uniform UniformBufferObject {
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
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float padding;
} ubo;

// Terrain-specific uniforms
layout(std140, binding = 4) uniform TerrainUniforms {
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 viewProjMatrix;
    vec4 frustumPlanes[6];
    vec4 terrainCameraPosition;
    vec4 terrainParams;
    vec4 lodParams;
    vec2 screenSize;
    float lodFactor;
    float terrainPadding;
};

// Textures
layout(binding = 3) uniform sampler2D heightMap;
layout(binding = 6) uniform sampler2D terrainAlbedo;
layout(binding = 7) uniform sampler2DArrayShadow shadowMapArray;

// Inputs from vertex shader
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragDepth;

layout(location = 0) out vec4 outColor;

// Shadow sampling
float sampleShadow(int cascade, vec3 worldPos) {
    vec4 lightSpacePos = ubo.cascadeViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Transform to [0, 1] range for sampling
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Check bounds
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }

    // PCF shadow sampling
    float shadow = 0.0;
    float texelSize = 1.0 / ubo.shadowMapSize;
    float bias = 0.002;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec4 sampleCoord = vec4(
                projCoords.xy + vec2(x, y) * texelSize,
                float(cascade),
                projCoords.z - bias
            );
            shadow += texture(shadowMapArray, sampleCoord);
        }
    }

    return shadow / 9.0;
}

float getShadowFactor(vec3 worldPos) {
    // Calculate view-space depth
    vec4 viewPos = ubo.view * vec4(worldPos, 1.0);
    float depth = -viewPos.z;

    // Determine cascade
    int cascade = 3;
    if (depth < ubo.cascadeSplits.x) cascade = 0;
    else if (depth < ubo.cascadeSplits.y) cascade = 1;
    else if (depth < ubo.cascadeSplits.z) cascade = 2;

    return sampleShadow(cascade, worldPos);
}

// GGX/Trowbridge-Reitz normal distribution
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

// Schlick-GGX geometry function
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // Sample terrain albedo with tiling
    vec3 albedo = texture(terrainAlbedo, fragTexCoord * 50.0).rgb;

    // Add some variation based on slope
    vec3 normal = normalize(fragNormal);
    float slope = 1.0 - normal.y;

    // Blend in rock color on steep slopes
    vec3 rockColor = vec3(0.4, 0.35, 0.3);
    vec3 grassColor = albedo;
    albedo = mix(grassColor, rockColor, smoothstep(0.3, 0.6, slope));

    // Material properties
    float roughness = mix(0.8, 0.95, slope);  // Rougher on slopes
    float metallic = 0.0;  // Terrain is non-metallic

    // View direction
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Calculate sun contribution
    vec3 L = normalize(ubo.sunDirection.xyz);
    vec3 H = normalize(V + L);

    // PBR calculations
    vec3 F0 = vec3(0.04);  // Non-metallic
    F0 = mix(F0, albedo, metallic);

    float NDF = distributionGGX(normal, H, roughness);
    float G = geometrySmith(normal, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(normal, L), 0.0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(normal, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 radiance = ubo.sunColor.rgb * ubo.sunColor.a;
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // Shadow
    float shadow = getShadowFactor(fragWorldPos);

    // Ambient
    vec3 ambient = ubo.ambientColor.rgb * albedo;

    // Final color
    vec3 color = ambient + Lo * shadow;

    // Debug: visualize LOD depth
    #if 0
    float maxDepth = lodParams.w;
    float t = fragDepth / maxDepth;
    color = mix(color, vec3(t, 1.0 - t, 0.0), 0.3);
    #endif

    // Output
    outColor = vec4(color, 1.0);
}
