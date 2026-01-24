#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"
#include "snow_common.glsl"
#include "cloud_shadow_common.glsl"
#include "ubo_snow.glsl"
#include "ubo_cloud_shadow.glsl"
#include "push_constants_common.glsl"
#include "normal_mapping_common.glsl"

// Enable shadow sampling for dynamic lights
#define DYNAMIC_LIGHTS_ENABLE_SHADOWS
#include "dynamic_lights_common.glsl"

layout(binding = BINDING_DIFFUSE_TEX) uniform sampler2D texSampler;
layout(binding = BINDING_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;  // Changed to array for CSM
layout(binding = BINDING_NORMAL_MAP) uniform sampler2D normalMap;
layout(binding = BINDING_EMISSIVE_MAP) uniform sampler2D emissiveMap;
layout(binding = BINDING_POINT_SHADOW_MAP) uniform samplerCubeArrayShadow pointShadowMaps;  // Point light cube shadow maps
layout(binding = BINDING_SPOT_SHADOW_MAP) uniform sampler2DArrayShadow spotShadowMaps;     // Spot light shadow maps
layout(binding = BINDING_SNOW_MASK) uniform sampler2D snowMaskTexture;               // World-space snow coverage
layout(binding = BINDING_CLOUD_SHADOW_MAP) uniform sampler2D cloudShadowMap;                // Cloud shadow map (R16F)

// Optional PBR texture maps (for Substance/PBR materials)
layout(binding = BINDING_ROUGHNESS_MAP) uniform sampler2D roughnessMap;
layout(binding = BINDING_METALLIC_MAP) uniform sampler2D metallicMap;
layout(binding = BINDING_AO_MAP) uniform sampler2D aoMap;
layout(binding = BINDING_HEIGHT_MAP) uniform sampler2D heightMap;

// GPULight struct and light buffer are defined in dynamic_lights_common.glsl

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

// Note: perturbNormal is provided by normal_mapping_common.glsl

// Helper function to create a look-at matrix
mat4 lookAt(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);

    mat4 result = mat4(1.0);
    result[0][0] = s.x;
    result[1][0] = s.y;
    result[2][0] = s.z;
    result[0][1] = u.x;
    result[1][1] = u.y;
    result[2][1] = u.z;
    result[0][2] = -f.x;
    result[1][2] = -f.y;
    result[2][2] = -f.z;
    result[3][0] = -dot(s, eye);
    result[3][1] = -dot(u, eye);
    result[3][2] = dot(f, eye);
    return result;
}

// Helper function to create a perspective matrix
mat4 perspective(float fovy, float aspect, float near, float far) {
    float tanHalfFovy = tan(fovy / 2.0);

    mat4 result = mat4(0.0);
    result[0][0] = 1.0 / (aspect * tanHalfFovy);
    result[1][1] = 1.0 / tanHalfFovy;
    result[2][2] = -(far + near) / (far - near);
    result[2][3] = -1.0;
    result[3][2] = -(2.0 * far * near) / (far - near);
    return result;
}

// Calculate PBR lighting for sun/moon (directional lights)
vec3 calculatePBR(vec3 N, vec3 V, vec3 L, vec3 lightColor, float lightIntensity, vec3 albedo, float shadow,
                  float roughness, float metallic) {
    vec3 H = normalize(V + L);

    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0001);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    // Dielectric F0 (F0_DIELECTRIC is typical for non-metals)
    vec3 F0 = mix(vec3(F0_DIELECTRIC), albedo, metallic);

    // Specular BRDF
    float D = D_GGX(NoH, roughness);
    float Vis = V_SmithGGX(NoV, NoL, roughness);
    vec3 F = F_Schlick(VoH, F0);

    vec3 specular = D * Vis * F;

    // Energy-conserving diffuse
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * lightColor * lightIntensity * NoL * shadow;
}

// Dynamic light calculation uses shared functions from dynamic_lights_common.glsl
// calculateAllDynamicLightsPBR is called with shadow map samplers

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);

    // Alpha test for transparent textures (leaves, etc.)
    if (material.alphaTestThreshold > 0.0 && texColor.a < material.alphaTestThreshold) {
        discard;
    }

    vec3 geometricN = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Enable normal mapping for debugging
    vec3 N = perturbNormal(geometricN, fragTangent, normalMap, fragTexCoord);

    // Multiply texture color with vertex color (for glTF material baseColorFactor)
    // Also apply tint color for NPC hostility visualization
    vec3 albedo = texColor.rgb * fragColor.rgb * material.tintColor.rgb;

    // === PBR MATERIAL PROPERTIES ===
    // Sample optional PBR texture maps, falling back to push constant values
    float roughness = material.roughness;
    float metallic = material.metallic;
    float ao = 1.0;

    if ((material.pbrFlags & PBR_HAS_ROUGHNESS_MAP) != 0u) {
        roughness = texture(roughnessMap, fragTexCoord).r;
    }
    if ((material.pbrFlags & PBR_HAS_METALLIC_MAP) != 0u) {
        metallic = texture(metallicMap, fragTexCoord).r;
    }
    if ((material.pbrFlags & PBR_HAS_AO_MAP) != 0u) {
        ao = texture(aoMap, fragTexCoord).r;
    }
    // Height map can be used for parallax mapping (not implemented yet)
    // if ((material.pbrFlags & PBR_HAS_HEIGHT_MAP) != 0u) { ... }

    // === SNOW LAYER ===
    // Sample snow mask at world position
    float snowMaskCoverage = sampleSnowMask(snowMaskTexture, fragWorldPos,
                                             snow.snowMaskParams.xy, snow.snowMaskParams.z);

    // Calculate snow coverage based on global amount, mask, and surface orientation
    float snowCoverage = calculateSnowCoverage(snow.snowAmount, snowMaskCoverage, N);

    // Apply snow layer to albedo
    // Snow primarily affects visual appearance through color blending
    if (snowCoverage > 0.01) {
        albedo = blendSnowAlbedo(albedo, snow.snowColor.rgb, snowCoverage);
    }

    // Calculate shadow for sun (terrain + cloud shadows combined)
    vec3 sunL = normalize(ubo.toSunDirection.xyz);
    float terrainShadow = 1.0;
    if (ubo.shadowsEnabled > 0.5) {
        terrainShadow = calculateCascadedShadow(
            fragWorldPos, N, sunL,
            ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
            ubo.shadowMapSize, shadowMapArray
        );
    }

    // Cloud shadows - sample from cloud shadow map
    float cloudShadowFactor = 1.0;
    if (cloudShadow.cloudShadowEnabled > 0.5) {
        cloudShadowFactor = sampleCloudShadowSoft(cloudShadowMap, fragWorldPos, cloudShadow.cloudShadowMatrix);
    }

    // Combine terrain and cloud shadows
    float shadow = combineShadows(terrainShadow, cloudShadowFactor);

    // Sun lighting with shadow
    // Apply eclipse darkening - moon blocks sunlight during solar eclipse
    float sunIntensity = ubo.toSunDirection.w * (1.0 - ubo.eclipseAmount);
    vec3 sunLight = calculatePBR(N, V, sunL, ubo.sunColor.rgb, sunIntensity, albedo, shadow,
                                  roughness, metallic);

    // Moon lighting (no shadow - moon is soft fill light but becomes primary light at night)
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonIntensity = ubo.moonDirection.w;
    vec3 moonLight = calculatePBR(N, V, moonL, ubo.moonColor.rgb, moonIntensity, albedo, 1.0,
                                   roughness, metallic);

    // Ambient lighting
    // For dielectrics: diffuse ambient from all directions
    // For metals: specular ambient (environment reflection approximation)
    vec3 F0 = mix(vec3(F0_DIELECTRIC), albedo, metallic);
    vec3 ambientDiffuse = ubo.ambientColor.rgb * albedo * (1.0 - metallic);
    // Metals need higher ambient to simulate environment reflections
    // Rougher metals get more ambient, smoother metals rely more on direct specular
    float envReflection = mix(0.3, 1.0, roughness);
    vec3 ambientSpecular = ubo.ambientColor.rgb * F0 * metallic * envReflection;

    // Apply horizon occlusion to ambient specular (Ghost of Tsushima technique)
    // This prevents normal-mapped bumps from glowing unrealistically on their back sides
    float horizonOcc = horizonOcclusion(V, geometricN, N, roughness);
    ambientSpecular *= horizonOcc;

    vec3 ambient = (ambientDiffuse + ambientSpecular) * ao;  // Apply AO to ambient lighting

    // Dynamic lights contribution (multiple point and spot lights)
    // Uses shared function from dynamic_lights_common.glsl with shadow sampling
    vec3 dynamicLights = calculateAllDynamicLightsPBR(N, V, fragWorldPos, albedo, roughness, metallic,
                                                       pointShadowMaps, spotShadowMaps);

    vec3 finalColor = ambient + sunLight + moonLight + dynamicLights;

    // Add emissive glow from emissive map + material emissive color
    vec3 emissiveMapSample = texture(emissiveMap, fragTexCoord).rgb;
    float emissiveMapLum = dot(emissiveMapSample, vec3(0.299, 0.587, 0.114));
    // Use emissive map color if present, otherwise use material emissive color
    vec3 emissiveColor = emissiveMapLum > 0.01
        ? emissiveMapSample * material.emissiveColor.rgb
        : mix(albedo, material.emissiveColor.rgb, material.emissiveColor.a);
    vec3 emissive = emissiveColor * material.emissiveIntensity;
    finalColor += emissive;

    vec3 atmosphericColor = applyAerialPerspectiveSimple(finalColor, fragWorldPos);

    // Debug cascade visualization overlay
    if (ubo.debugCascades > 0.5) {
        int cascade = getCascadeForDebug(fragWorldPos, ubo.view, ubo.cascadeSplits);
        vec3 cascadeColor = getCascadeDebugColor(cascade);
        atmosphericColor = mix(atmosphericColor, cascadeColor, 0.3);  // 30% tint
    }

    // Apply opacity for camera occlusion fading
    float finalAlpha = texColor.a * material.opacity;
    outColor = vec4(atmosphericColor, finalAlpha);
}
