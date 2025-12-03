#version 450

#extension GL_GOOGLE_include_directive : require

// Grass system uses its own descriptor set layout with custom bindings.
// Override the UBO bindings before including the shared headers.
#define SNOW_UBO_BINDING 10
#define CLOUD_SHADOW_UBO_BINDING 11

#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "atmosphere_common.glsl"
#include "snow_common.glsl"
#include "cloud_shadow_common.glsl"
#include "ubo_common.glsl"
#include "ubo_snow.glsl"
#include "ubo_cloud_shadow.glsl"

// Grass system descriptor set layout:
// binding 0: UBO (main rendering uniforms)
// binding 1: instance buffer (SSBO) - vertex shader only
// binding 2: shadow map (sampler)
// binding 3: wind UBO - vertex shader only
// binding 4: light buffer (SSBO)
// binding 5: snow mask texture (sampler)
// binding 6: cloud shadow map (sampler)
// binding 10: snow UBO
// binding 11: cloud shadow UBO

layout(binding = 2) uniform sampler2DArrayShadow shadowMapArray;  // CSM shadow map

// GPU light structure (must match CPU GPULight struct)
struct GPULight {
    vec4 positionAndType;    // xyz = position, w = type (0=point, 1=spot)
    vec4 directionAndCone;   // xyz = direction (for spot), w = outer cone angle (cos)
    vec4 colorAndIntensity;  // rgb = color, a = intensity
    vec4 radiusAndInnerCone; // x = radius, y = inner cone angle (cos), z = shadow map index (-1 = no shadow), w = padding
};

// Light buffer SSBO
layout(std430, binding = 4) readonly buffer LightBuffer {
    uvec4 lightCount;        // x = active light count
    GPULight lights[MAX_LIGHTS];
} lightBuffer;

// Snow mask texture (world-space coverage)
layout(binding = 5) uniform sampler2D snowMaskTexture;

// Cloud shadow map (R16F: 0=shadow, 1=no shadow)
layout(binding = 6) uniform sampler2D cloudShadowMap;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragHeight;
layout(location = 3) in float fragClumpId;
layout(location = 4) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

// Material parameters for grass
const float GRASS_ROUGHNESS = 0.7;  // Grass is fairly rough/matte
const float GRASS_SSS_STRENGTH = 0.35;  // Subsurface scattering intensity
const float GRASS_SPECULAR_STRENGTH = 0.15;  // Subtle specular highlights

// Clump color variation parameters
const float CLUMP_COLOR_INFLUENCE = 0.15;  // Subtle color variation (0-1)

// Calculate contribution from a single dynamic light for grass
vec3 calculateDynamicLightGrass(GPULight light, vec3 N, vec3 V, vec3 worldPos, vec3 albedo) {
    vec3 lightPos = light.positionAndType.xyz;
    uint lightType = uint(light.positionAndType.w);
    vec3 lightColor = light.colorAndIntensity.rgb;
    float lightIntensity = light.colorAndIntensity.a;
    float lightRadius = light.radiusAndInnerCone.x;

    if (lightIntensity <= 0.0) return vec3(0.0);

    vec3 lightVec = lightPos - worldPos;
    float distance = length(lightVec);
    vec3 L = normalize(lightVec);

    // Early out if beyond radius
    if (lightRadius > 0.0 && distance > lightRadius) return vec3(0.0);

    // Calculate attenuation
    float attenuation = calculateAttenuation(distance, lightRadius);

    // For spot lights, apply cone falloff
    if (lightType == LIGHT_TYPE_SPOT) {
        vec3 spotDir = normalize(light.directionAndCone.xyz);
        float outerCone = light.directionAndCone.w;
        float innerCone = light.radiusAndInnerCone.y;
        float spotFalloff = calculateSpotFalloff(L, spotDir, innerCone, outerCone);
        attenuation *= spotFalloff;
    }

    // Two-sided diffuse for grass
    float NdotL = dot(N, L);
    float diffuse = max(NdotL, 0.0) + max(-NdotL, 0.0) * 0.6;

    // Add SSS for dynamic light
    vec3 sss = calculateSSS(L, V, N, lightColor, albedo, GRASS_SSS_STRENGTH);

    return (albedo * diffuse + sss) * lightColor * lightIntensity * attenuation;
}

// Calculate contribution from all dynamic lights for grass
vec3 calculateAllDynamicLightsGrass(vec3 N, vec3 V, vec3 worldPos, vec3 albedo) {
    vec3 totalLight = vec3(0.0);
    uint numLights = min(lightBuffer.lightCount.x, MAX_LIGHTS);

    for (uint i = 0; i < numLights; i++) {
        totalLight += calculateDynamicLightGrass(lightBuffer.lights[i], N, V, worldPos, albedo);
    }

    return totalLight;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Apply subtle clump-based color variation
    // Different clumps have slightly different hue/saturation
    vec3 albedo = fragColor;

    // Subtle hue shift based on clumpId (shift toward yellow or blue-green)
    vec3 warmShift = vec3(0.05, 0.03, -0.02);    // Slightly warmer/yellower
    vec3 coolShift = vec3(-0.02, 0.02, 0.03);    // Slightly cooler/bluer
    vec3 colorShift = mix(coolShift, warmShift, fragClumpId);
    albedo += colorShift * CLUMP_COLOR_INFLUENCE;

    // Subtle brightness variation per clump
    float brightnessVar = 0.9 + fragClumpId * 0.2;  // 0.9 to 1.1
    albedo *= mix(1.0, brightnessVar, CLUMP_COLOR_INFLUENCE);

    // === SNOW LAYER ===
    // Sample snow mask at world position
    float snowMaskCoverage = sampleSnowMask(snowMaskTexture, fragWorldPos,
                                             snow.snowMaskParams.xy, snow.snowMaskParams.z);

    // Calculate vegetation snow coverage - tips catch more snow than stems
    // fragHeight: 0 at base, 1 at tip
    float snowAffinity = fragHeight;  // Tips catch more snow
    float snowCoverage = calculateVegetationSnowCoverage(snow.snowAmount, snowMaskCoverage, N, snowAffinity);

    // Apply snow to grass albedo
    if (snowCoverage > 0.01) {
        albedo = snowyVegetationColor(albedo, snow.snowColor.rgb, snowCoverage);
    }

    // === SUN LIGHTING ===
    vec3 sunL = normalize(ubo.sunDirection.xyz);
    float terrainShadow = calculateCascadedShadow(
        fragWorldPos, N, sunL,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Cloud shadows - sample from cloud shadow map
    float cloudShadowFactor = 1.0;
    if (cloudShadow.cloudShadowEnabled > 0.5) {
        cloudShadowFactor = sampleCloudShadowSoft(cloudShadowMap, fragWorldPos, cloudShadow.cloudShadowMatrix);
    }

    // Combine terrain and cloud shadows
    float shadow = combineShadows(terrainShadow, cloudShadowFactor);

    // Two-sided diffuse (grass blades are thin)
    float sunNdotL = dot(N, sunL);
    float sunDiffuse = max(sunNdotL, 0.0) + max(-sunNdotL, 0.0) * 0.6;

    // Specular highlight (subtle for grass)
    vec3 H = normalize(V + sunL);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    float D = D_GGX(NoH, GRASS_ROUGHNESS);
    vec3 F0 = vec3(0.04);  // Dielectric grass
    vec3 F = F_Schlick(VoH, F0);
    vec3 specular = D * F * GRASS_SPECULAR_STRENGTH;

    // Subsurface scattering - light through grass blades when backlit
    vec3 sss = calculateSSS(sunL, V, N, ubo.sunColor.rgb, albedo, GRASS_SSS_STRENGTH);

    vec3 sunLight = (albedo * sunDiffuse + specular + sss) * ubo.sunColor.rgb * ubo.sunDirection.w * shadow;

    // === MOON LIGHTING ===
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonNdotL = dot(N, moonL);
    float moonDiffuse = max(moonNdotL, 0.0) + max(-moonNdotL, 0.0) * 0.6;

    // Add subsurface scattering for moonlight - fades in smoothly during twilight
    // Smooth transition: starts at sun altitude 10°, full effect at -6°
    float twilightFactor = smoothstep(0.17, -0.1, ubo.sunDirection.y);
    float moonVisibility = smoothstep(-0.09, 0.1, ubo.moonDirection.y);
    float moonSssFactor = twilightFactor * moonVisibility;
    vec3 moonSss = calculateSSS(moonL, V, N, ubo.moonColor.rgb, albedo, GRASS_SSS_STRENGTH) * 0.5 * moonSssFactor;

    vec3 moonLight = (albedo * moonDiffuse + moonSss) * ubo.moonColor.rgb * ubo.moonDirection.w;

    // === DYNAMIC LIGHTS (multiple point and spot lights) ===
    vec3 dynamicLights = calculateAllDynamicLightsGrass(N, V, fragWorldPos, albedo);

    // === FRESNEL RIM LIGHTING ===
    // Grass blades catch light at grazing angles
    float NoV = max(dot(N, V), 0.0);
    float rimFresnel = pow(1.0 - NoV, 4.0);
    // Rim color based on sky/ambient
    vec3 rimColor = ubo.ambientColor.rgb * 0.5 + ubo.sunColor.rgb * ubo.sunDirection.w * 0.2;
    vec3 rimLight = rimColor * rimFresnel * 0.15;

    // === AMBIENT LIGHTING ===
    // Ambient occlusion - darker at base, brighter at tip
    float ao = 0.4 + fragHeight * 0.6;

    // Height-based ambient color shift (base is cooler/darker, tips catch more sky)
    vec3 ambientBase = ubo.ambientColor.rgb * vec3(0.8, 0.85, 0.9);  // Cooler at base
    vec3 ambientTip = ubo.ambientColor.rgb * vec3(1.0, 1.0, 0.95);   // Warmer at tip
    vec3 ambient = albedo * mix(ambientBase, ambientTip, fragHeight);

    // === COMBINE LIGHTING ===
    vec3 finalColor = (ambient + sunLight + moonLight + dynamicLights + rimLight) * ao;

    // === AERIAL PERSPECTIVE ===
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDistance = length(cameraToFrag);
    vec3 sunDir = normalize(ubo.sunDirection.xyz);
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    vec3 atmosphericColor = applyAerialPerspective(finalColor, ubo.cameraPosition.xyz, normalize(cameraToFrag), viewDistance, sunDir, sunColor);

    outColor = vec4(atmosphericColor, 1.0);
}
