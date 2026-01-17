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
#include "scene_instance_common.glsl"
#include "normal_mapping_common.glsl"

// Enable shadow sampling for dynamic lights
#define DYNAMIC_LIGHTS_ENABLE_SHADOWS
#include "dynamic_lights_common.glsl"

layout(binding = BINDING_DIFFUSE_TEX) uniform sampler2D texSampler;
layout(binding = BINDING_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_NORMAL_MAP) uniform sampler2D normalMap;
layout(binding = BINDING_EMISSIVE_MAP) uniform sampler2D emissiveMap;
layout(binding = BINDING_POINT_SHADOW_MAP) uniform samplerCubeArrayShadow pointShadowMaps;
layout(binding = BINDING_SPOT_SHADOW_MAP) uniform sampler2DArrayShadow spotShadowMaps;
layout(binding = BINDING_SNOW_MASK) uniform sampler2D snowMaskTexture;
layout(binding = BINDING_CLOUD_SHADOW_MAP) uniform sampler2D cloudShadowMap;

// Optional PBR texture maps
layout(binding = BINDING_ROUGHNESS_MAP) uniform sampler2D roughnessMap;
layout(binding = BINDING_METALLIC_MAP) uniform sampler2D metallicMap;
layout(binding = BINDING_AO_MAP) uniform sampler2D aoMap;
layout(binding = BINDING_HEIGHT_MAP) uniform sampler2D heightMap;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) in vec4 fragColor;
layout(location = 5) flat in uint fragInstanceIndex;

layout(location = 0) out vec4 outColor;

// Calculate PBR lighting for sun/moon (directional lights)
vec3 calculatePBR(vec3 N, vec3 V, vec3 L, vec3 lightColor, float lightIntensity, vec3 albedo, float shadow,
                  float roughness, float metallic) {
    vec3 H = normalize(V + L);

    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.0001);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(F0_DIELECTRIC), albedo, metallic);

    float D = D_GGX(NoH, roughness);
    float Vis = V_SmithGGX(NoV, NoL, roughness);
    vec3 F = F_Schlick(VoH, F0);

    vec3 specular = D * Vis * F;

    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * lightColor * lightIntensity * NoL * shadow;
}

void main() {
    // Get instance data
    SceneInstance inst = sceneInstances[fragInstanceIndex];

    vec4 texColor = texture(texSampler, fragTexCoord);

    // Alpha test
    float alphaTestThreshold = inst.alphaTestThreshold;
    if (alphaTestThreshold > 0.0 && texColor.a < alphaTestThreshold) {
        discard;
    }

    vec3 geometricN = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    vec3 N = perturbNormal(geometricN, fragTangent, normalMap, fragTexCoord);

    vec3 albedo = texColor.rgb * fragColor.rgb;

    // Get material properties from instance data
    float roughness = INSTANCE_ROUGHNESS(inst);
    float metallic = INSTANCE_METALLIC(inst);
    float ao = 1.0;

    uint pbrFlags = inst.pbrFlags;
    if ((pbrFlags & PBR_HAS_ROUGHNESS_MAP) != 0u) {
        roughness = texture(roughnessMap, fragTexCoord).r;
    }
    if ((pbrFlags & PBR_HAS_METALLIC_MAP) != 0u) {
        metallic = texture(metallicMap, fragTexCoord).r;
    }
    if ((pbrFlags & PBR_HAS_AO_MAP) != 0u) {
        ao = texture(aoMap, fragTexCoord).r;
    }

    // Snow layer
    float snowMaskCoverage = sampleSnowMask(snowMaskTexture, fragWorldPos,
                                             snow.snowMaskParams.xy, snow.snowMaskParams.z);
    float snowCoverage = calculateSnowCoverage(snow.snowAmount, snowMaskCoverage, N);

    if (snowCoverage > 0.01) {
        albedo = blendSnowAlbedo(albedo, snow.snowColor.rgb, snowCoverage);
    }

    // Shadow calculation
    vec3 sunL = normalize(ubo.toSunDirection.xyz);
    float terrainShadow = 1.0;
    if (ubo.shadowsEnabled > 0.5) {
        terrainShadow = calculateCascadedShadow(
            fragWorldPos, N, sunL,
            ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
            ubo.shadowMapSize, shadowMapArray
        );
    }

    float cloudShadowFactor = 1.0;
    if (cloudShadow.cloudShadowEnabled > 0.5) {
        cloudShadowFactor = sampleCloudShadowSoft(cloudShadowMap, fragWorldPos, cloudShadow.cloudShadowMatrix);
    }

    float shadow = combineShadows(terrainShadow, cloudShadowFactor);

    // Sun lighting
    float sunIntensity = ubo.toSunDirection.w * (1.0 - ubo.eclipseAmount);
    vec3 sunLight = calculatePBR(N, V, sunL, ubo.sunColor.rgb, sunIntensity, albedo, shadow,
                                  roughness, metallic);

    // Moon lighting
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonIntensity = ubo.moonDirection.w;
    vec3 moonLight = calculatePBR(N, V, moonL, ubo.moonColor.rgb, moonIntensity, albedo, 1.0,
                                   roughness, metallic);

    // Ambient lighting
    vec3 F0 = mix(vec3(F0_DIELECTRIC), albedo, metallic);
    vec3 ambientDiffuse = ubo.ambientColor.rgb * albedo * (1.0 - metallic);
    float envReflection = mix(0.3, 1.0, roughness);
    vec3 ambientSpecular = ubo.ambientColor.rgb * F0 * metallic * envReflection;

    float horizonOcc = horizonOcclusion(V, geometricN, N, roughness);
    ambientSpecular *= horizonOcc;

    vec3 ambient = (ambientDiffuse + ambientSpecular) * ao;

    // Dynamic lights
    vec3 dynamicLights = calculateAllDynamicLightsPBR(N, V, fragWorldPos, albedo, roughness, metallic,
                                                       pointShadowMaps, spotShadowMaps);

    vec3 finalColor = ambient + sunLight + moonLight + dynamicLights;

    // Emissive
    vec3 emissiveMapSample = texture(emissiveMap, fragTexCoord).rgb;
    float emissiveMapLum = dot(emissiveMapSample, vec3(0.299, 0.587, 0.114));
    vec3 emissiveColor = emissiveMapLum > 0.01
        ? emissiveMapSample * inst.emissiveColor.rgb
        : mix(albedo, inst.emissiveColor.rgb, inst.emissiveColor.a);
    vec3 emissive = emissiveColor * INSTANCE_EMISSIVE_INTENSITY(inst);
    finalColor += emissive;

    vec3 atmosphericColor = applyAerialPerspectiveSimple(finalColor, fragWorldPos);

    // Debug cascade visualization
    if (ubo.debugCascades > 0.5) {
        int cascade = getCascadeForDebug(fragWorldPos, ubo.view, ubo.cascadeSplits);
        vec3 cascadeColor = getCascadeDebugColor(cascade);
        atmosphericColor = mix(atmosphericColor, cascadeColor, 0.3);
    }

    // Apply opacity
    float finalAlpha = texColor.a * INSTANCE_OPACITY(inst);
    outColor = vec4(atmosphericColor, finalAlpha);
}
