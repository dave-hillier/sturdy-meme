#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain.frag - Terrain fragment shader
 * Simple PBR-based terrain rendering with shadow support
 */

#include "../bindings.glsl"
#include "../constants_common.glsl"
#include "../lighting_common.glsl"
#include "../shadow_common.glsl"

// Terrain uses binding 5 for the main UBO (different descriptor set layout)
// Must be defined before including atmosphere_common.glsl which includes ubo_common.glsl
#define UBO_BINDING 5
#include "../atmosphere_common.glsl"
#include "../snow_common.glsl"
#include "../cloud_shadow_common.glsl"
#include "../ubo_common.glsl"
#include "../ubo_snow.glsl"
#include "../ubo_cloud_shadow.glsl"

// Terrain-specific uniforms
layout(std140, binding = BINDING_TERRAIN_UBO) uniform TerrainUniforms {
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
layout(binding = BINDING_TERRAIN_HEIGHT_MAP) uniform sampler2D heightMap;
layout(binding = BINDING_TERRAIN_ALBEDO) uniform sampler2D terrainAlbedo;
layout(binding = BINDING_TERRAIN_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_TERRAIN_FAR_LOD_GRASS) uniform sampler2D grassFarLODTexture;  // Far LOD grass texture
layout(binding = BINDING_TERRAIN_SNOW_MASK) uniform sampler2D snowMaskTexture;     // World-space snow coverage (legacy)

// Volumetric snow cascade textures (height in meters)
layout(binding = BINDING_TERRAIN_SNOW_CASCADE_0) uniform sampler2D snowCascade0;  // Near cascade (256m)
layout(binding = BINDING_TERRAIN_SNOW_CASCADE_1) uniform sampler2D snowCascade1;  // Mid cascade (1024m)
layout(binding = BINDING_TERRAIN_SNOW_CASCADE_2) uniform sampler2D snowCascade2;  // Far cascade (4096m)

// Cloud shadow map (R16F: 0=shadow, 1=no shadow)
layout(binding = BINDING_TERRAIN_CLOUD_SHADOW) uniform sampler2D cloudShadowMap;

// Hole mask for caves/wells (R8: 0=solid, 1=hole)
layout(binding = BINDING_TERRAIN_HOLE_MASK) uniform sampler2D holeMask;

// Far LOD grass parameters (where to start/end grass-to-terrain transition)
const float GRASS_RENDER_DISTANCE = 60.0;     // Should match grass system maxDrawDistance
const float FAR_LOD_TRANSITION_START = 50.0;  // Start blending far LOD
const float FAR_LOD_TRANSITION_END = 70.0;    // Full far LOD (terrain with grass tint)

// Triplanar mapping parameters
const float TRIPLANAR_SCALE = 0.1;            // World-space texture scale (larger = bigger texture)
const float TRIPLANAR_SHARPNESS = 4.0;        // Higher = sharper transitions between projections

// Triplanar texture sampling - samples texture from 3 axis-aligned projections
// and blends based on surface normal to avoid stretching on steep surfaces
vec3 sampleTriplanar(sampler2D tex, vec3 worldPos, vec3 normal, float scale) {
    // Use absolute normal for blend weights
    vec3 blendWeights = abs(normal);

    // Raise to power for sharper transitions
    blendWeights = pow(blendWeights, vec3(TRIPLANAR_SHARPNESS));

    // Normalize so weights sum to 1
    blendWeights /= (blendWeights.x + blendWeights.y + blendWeights.z);

    // Sample texture from each axis projection
    // X-axis projection: use YZ plane
    vec3 xProj = texture(tex, worldPos.yz * scale).rgb;
    // Y-axis projection: use XZ plane (top-down view, most common for terrain)
    vec3 yProj = texture(tex, worldPos.xz * scale).rgb;
    // Z-axis projection: use XY plane
    vec3 zProj = texture(tex, worldPos.xy * scale).rgb;

    // Blend based on weights
    return xProj * blendWeights.x + yProj * blendWeights.y + zProj * blendWeights.z;
}

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
    // Check hole mask - discard fragment if in a hole (cave/well entrance)
    float holeMaskValue = texture(holeMask, fragTexCoord).r;
    if (holeMaskValue > 0.5) {
        discard;
    }

    // Normalize the surface normal
    vec3 normal = normalize(fragNormal);
    float slope = 1.0 - normal.y;

    // Use triplanar mapping for terrain albedo - prevents texture stretching on steep slopes
    // On flat surfaces (high normal.y), this mainly uses the Y projection (top-down XZ plane)
    // On steep cliffs, it blends in X and Z projections to avoid stretching
    vec3 albedo = sampleTriplanar(terrainAlbedo, fragWorldPos, normal, TRIPLANAR_SCALE);

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

    // === SNOW LAYER ===
    float snowCoverage = 0.0;
    float snowHeight = 0.0;

    if (snow.useVolumetricSnow > 0.5) {
        // Volumetric snow: sample from cascaded heightfield
        snowHeight = sampleVolumetricSnowHeight(
            snowCascade0, snowCascade1, snowCascade2,
            fragWorldPos, ubo.cameraPosition.xyz,
            snow.snowCascade0Params, snow.snowCascade1Params, snow.snowCascade2Params
        );

        // Convert height to coverage
        snowCoverage = snowHeightToCoverage(snowHeight, snow.snowAmount, normal);

        // Calculate snow normal from heightfield for better surface detail
        if (snowCoverage > 0.1) {
            uint cascadeIdx;
            float blendFactor;
            selectSnowCascade(distToCamera, cascadeIdx, blendFactor);

            vec3 snowNormal;
            float texelSize = 1.0 / 256.0;  // CASCADE_SIZE

            if (cascadeIdx == 0) {
                vec2 snowUV = worldToSnowCascadeUV(fragWorldPos, snow.snowCascade0Params);
                snowNormal = calculateSnowNormalFromHeight(snowCascade0, snowUV, texelSize, snow.snowMaxHeight);
            } else if (cascadeIdx == 1) {
                vec2 snowUV = worldToSnowCascadeUV(fragWorldPos, snow.snowCascade1Params);
                snowNormal = calculateSnowNormalFromHeight(snowCascade1, snowUV, texelSize, snow.snowMaxHeight);
            } else {
                vec2 snowUV = worldToSnowCascadeUV(fragWorldPos, snow.snowCascade2Params);
                snowNormal = calculateSnowNormalFromHeight(snowCascade2, snowUV, texelSize, snow.snowMaxHeight);
            }

            // Blend normals based on coverage
            normal = blendSnowNormal(normal, snowNormal, snowCoverage * 0.5);
        }
    } else {
        // Legacy: sample snow mask at world position
        float snowMaskCoverage = sampleSnowMask(snowMaskTexture, fragWorldPos,
                                                 snow.snowMaskParams.xy, snow.snowMaskParams.z);
        snowCoverage = calculateSnowCoverage(snow.snowAmount, snowMaskCoverage, normal);
    }

    // Apply snow layer to albedo and roughness
    if (snowCoverage > 0.01) {
        // Blend albedo with snow color
        albedo = blendSnowAlbedo(albedo, snow.snowColor.rgb, snowCoverage);
        // Snow is rougher than most terrain
        roughness = mix(roughness, snow.snowRoughness, snowCoverage);
    }

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

    // Shadow (terrain + cloud shadows combined)
    float terrainShadow = getShadowFactor(fragWorldPos);

    // Cloud shadows - sample from cloud shadow map
    float cloudShadowFactor = 1.0;
    if (cloudShadow.cloudShadowEnabled > 0.5) {
        cloudShadowFactor = sampleCloudShadowSoft(cloudShadowMap, fragWorldPos, cloudShadow.cloudShadowMatrix);
    }

    // Combine terrain and cloud shadows
    float shadow = combineShadows(terrainShadow, cloudShadowFactor);

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

    // Debug snow depth visualization
    if (snow.debugSnowDepth > 0.5 && snow.useVolumetricSnow > 0.5) {
        // Color code snow depth: blue (0m) -> cyan -> green -> yellow -> red (max)
        float normalizedDepth = clamp(snowHeight / snow.snowMaxHeight, 0.0, 1.0);

        // Heat map colors
        vec3 depthColor;
        if (normalizedDepth < 0.25) {
            // Blue to cyan (0-25%)
            depthColor = mix(vec3(0.0, 0.0, 0.5), vec3(0.0, 0.5, 1.0), normalizedDepth * 4.0);
        } else if (normalizedDepth < 0.5) {
            // Cyan to green (25-50%)
            depthColor = mix(vec3(0.0, 0.5, 1.0), vec3(0.0, 1.0, 0.0), (normalizedDepth - 0.25) * 4.0);
        } else if (normalizedDepth < 0.75) {
            // Green to yellow (50-75%)
            depthColor = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (normalizedDepth - 0.5) * 4.0);
        } else {
            // Yellow to red (75-100%)
            depthColor = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (normalizedDepth - 0.75) * 4.0);
        }

        // Overlay depth color (70% blend)
        atmosphericColor = mix(atmosphericColor, depthColor, 0.7);
    }

    // Output
    outColor = vec4(atmosphericColor, 1.0);
}
