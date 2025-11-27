#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain.frag - Terrain fragment shader
 * Simple PBR-based terrain rendering with shadow support
 */

#include "../constants_common.glsl"
#include "../lighting_common.glsl"
#include "../shadow_common.glsl"
#include "../atmosphere_common.glsl"

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
    vec4 windDirectionAndSpeed;  // xy = direction, z = speed, w = time
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;             // Julian day for sidereal rotation
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
layout(binding = 8) uniform sampler2D grassFarLODTexture;  // Far LOD grass texture

// Far LOD grass parameters (where to start/end grass-to-terrain transition)
const float GRASS_RENDER_DISTANCE = 60.0;     // Should match grass system maxDrawDistance
const float FAR_LOD_TRANSITION_START = 50.0;  // Start blending far LOD
const float FAR_LOD_TRANSITION_END = 70.0;    // Full far LOD (terrain with grass tint)

// Inputs from vertex shader
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragDepth;

layout(location = 0) out vec4 outColor;

// Shadow sampling - use common shadow functions
float getShadowFactor(vec3 worldPos) {
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    return calculateCascadedShadow(
        worldPos,
        normalize(fragNormal),
        sunL,
        ubo.view,
        ubo.cascadeSplits,
        ubo.cascadeViewProj,
        ubo.shadowMapSize,
        shadowMapArray
    );
}

void main() {
    // Sample terrain albedo with tiling
    vec3 albedo = texture(terrainAlbedo, fragTexCoord * 50.0).rgb;

    // Add some variation based on slope
    vec3 normal = normalize(fragNormal);
    float slope = 1.0 - normal.y;

    // === FAR LOD GRASS BLENDING ===
    // At distances beyond grass render distance, blend in grass texture to maintain
    // grass coverage without rendering individual blades
    float distToCamera = length(fragWorldPos - ubo.cameraPosition.xyz);
    float farLodBlend = smoothstep(FAR_LOD_TRANSITION_START, FAR_LOD_TRANSITION_END, distToCamera);

    // Sample far LOD grass texture (tiled based on world position for consistency)
    vec2 grassUV = fragWorldPos.xz * 0.05;  // Larger scale for distant grass
    vec3 farGrassColor = texture(grassFarLODTexture, grassUV).rgb;

    // Only apply far LOD grass on flat-ish terrain (where real grass would grow)
    // Grass doesn't grow on steep slopes
    float grassGrowthFactor = 1.0 - smoothstep(0.2, 0.5, slope);

    // Blend far LOD grass into terrain albedo
    // As distance increases, replace terrain texture with grass-tinted ground
    if (farLodBlend > 0.01 && grassGrowthFactor > 0.01) {
        // Mix terrain with far LOD grass color based on distance
        albedo = mix(albedo, farGrassColor, farLodBlend * grassGrowthFactor);
    }

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

    // PBR calculations using common functions
    vec3 F0 = vec3(0.04);  // Non-metallic
    F0 = mix(F0, albedo, metallic);

    float NDF = D_GGX(max(dot(normal, H), 0.0), roughness);
    float G = geometrySmith(normal, V, L, roughness);
    vec3 F = F_Schlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(normal, L), 0.0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(normal, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 radiance = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 sunLight = (kD * albedo / PI + specular) * radiance * NdotL;

    // Shadow
    float shadow = getShadowFactor(fragWorldPos);

    // === MOON LIGHTING ===
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    vec3 moonH = normalize(V + moonL);
    float moonNdotL = max(dot(normal, moonL), 0.0);

    // Moon PBR (simplified - less specular contribution)
    float moonNDF = D_GGX(max(dot(normal, moonH), 0.0), roughness);
    float moonG = geometrySmith(normal, V, moonL, roughness);
    vec3 moonF = F_Schlick(max(dot(moonH, V), 0.0), F0);

    vec3 moonKS = moonF;
    vec3 moonKD = (vec3(1.0) - moonKS) * (1.0 - metallic);

    vec3 moonNumerator = moonNDF * moonG * moonF;
    float moonDenominator = 4.0 * max(dot(normal, V), 0.0) * moonNdotL + 0.0001;
    vec3 moonSpecular = moonNumerator / moonDenominator;

    vec3 moonRadiance = ubo.moonColor.rgb * ubo.moonDirection.w;
    vec3 moonLight = (moonKD * albedo / PI + moonSpecular * 0.5) * moonRadiance * moonNdotL;

    // Ambient
    vec3 ambient = ubo.ambientColor.rgb * albedo;

    // Combine lighting (sun with shadows, moon without for soft night light)
    vec3 color = ambient + sunLight * shadow + moonLight;

    // Debug: visualize LOD depth
    #if 0
    float maxDepth = lodParams.w;
    float t = fragDepth / maxDepth;
    color = mix(color, vec3(t, 1.0 - t, 0.0), 0.3);
    #endif

    // === AERIAL PERSPECTIVE ===
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDistance = length(cameraToFrag);
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 atmosphericColor = applyAerialPerspective(color, ubo.cameraPosition.xyz, normalize(cameraToFrag), viewDistance, sunDir, sunColor);

    // Output
    outColor = vec4(atmosphericColor, 1.0);
}
