#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "grass_constants.glsl"

// Grass system uses its own descriptor set layout with custom bindings.
// Override the UBO bindings before including the shared headers.
#define SNOW_UBO_BINDING 10
#define CLOUD_SHADOW_UBO_BINDING 11

#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"
#include "snow_common.glsl"
#include "cloud_shadow_common.glsl"
#include "ubo_snow.glsl"
#include "ubo_cloud_shadow.glsl"

// Dynamic lights - no shadow sampling for grass (performance optimization)
#include "dynamic_lights_common.glsl"

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

layout(binding = BINDING_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;  // CSM shadow map

// GPULight struct and light buffer are defined in dynamic_lights_common.glsl

// Snow mask texture (world-space coverage)
layout(binding = BINDING_GRASS_SNOW_MASK) uniform sampler2D snowMaskTexture;

// Cloud shadow map (R16F: 0=shadow, 1=no shadow)
layout(binding = BINDING_GRASS_CLOUD_SHADOW) uniform sampler2D cloudShadowMap;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragHeight;
layout(location = 3) in float fragClumpId;
layout(location = 4) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

// Material parameters and clump color variation are now defined in grass_constants.glsl:
// GRASS_ROUGHNESS, GRASS_SSS_STRENGTH, GRASS_SPECULAR_STRENGTH, GRASS_CLUMP_COLOR_INFLUENCE

// Dynamic light calculation uses shared functions from dynamic_lights_common.glsl
// calculateAllDynamicLightsVegetationNoShadow provides consistent vegetation lighting

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Apply subtle clump-based color variation
    // Different clumps have slightly different hue/saturation
    vec3 albedo = fragColor;

    // Subtle hue shift based on clumpId using unified constants
    vec3 colorShift = mix(GRASS_COLOR_SHIFT_COOL, GRASS_COLOR_SHIFT_WARM, fragClumpId);
    albedo += colorShift * GRASS_CLUMP_COLOR_INFLUENCE;

    // Subtle brightness variation per clump using unified constants
    float brightnessVar = GRASS_BRIGHTNESS_MIN + fragClumpId * (GRASS_BRIGHTNESS_MAX - GRASS_BRIGHTNESS_MIN);
    albedo *= mix(1.0, brightnessVar, GRASS_CLUMP_COLOR_INFLUENCE);

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

    // === RAIN WETNESS (Composable Material System) ===
    // Wet grass is darker and shinier
    float wetness = snow.rainWetness;
    float grassRoughness = GRASS_ROUGHNESS;
    if (wetness > 0.01) {
        // Darken albedo when wet (water absorption)
        albedo *= mix(1.0, 0.7, wetness);
        // Reduce roughness (wet surfaces are shinier)
        grassRoughness = mix(GRASS_ROUGHNESS, GRASS_ROUGHNESS * 0.3, wetness);
    }

    // === SUN LIGHTING ===
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

    // Two-sided diffuse (grass blades are thin) using unified constant
    float sunNdotL = dot(N, sunL);
    float sunDiffuse = max(sunNdotL, 0.0) + max(-sunNdotL, 0.0) * GRASS_BACKLIT_DIFFUSE;

    // Specular highlight (subtle for grass)
    vec3 H = normalize(V + sunL);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    float D = D_GGX(NoH, grassRoughness);  // Use wetness-adjusted roughness
    vec3 F0 = vec3(F0_DIELECTRIC);  // Dielectric grass
    vec3 F = F_Schlick(VoH, F0);
    vec3 specular = D * F * GRASS_SPECULAR_STRENGTH;

    // Subsurface scattering - light through grass blades when backlit
    vec3 sss = calculateSSS(sunL, V, N, ubo.sunColor.rgb, albedo, GRASS_SSS_STRENGTH);

    vec3 sunLight = (albedo * sunDiffuse + specular + sss) * ubo.sunColor.rgb * ubo.toSunDirection.w * shadow;

    // === MOON LIGHTING ===
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    float moonNdotL = dot(N, moonL);
    float moonDiffuse = max(moonNdotL, 0.0) + max(-moonNdotL, 0.0) * GRASS_BACKLIT_DIFFUSE;

    // Add subsurface scattering for moonlight - fades in smoothly during twilight
    // Smooth transition: starts at sun altitude 10°, full effect at -6°
    float twilightFactor = smoothstep(0.17, -0.1, ubo.toSunDirection.y);
    float moonVisibility = smoothstep(-0.09, 0.1, ubo.moonDirection.y);
    float moonSssFactor = twilightFactor * moonVisibility;
    vec3 moonSss = calculateSSS(moonL, V, N, ubo.moonColor.rgb, albedo, GRASS_SSS_STRENGTH) * 0.5 * moonSssFactor;

    vec3 moonLight = (albedo * moonDiffuse + moonSss) * ubo.moonColor.rgb * ubo.moonDirection.w;

    // === DYNAMIC LIGHTS (multiple point and spot lights) ===
    // Uses shared function from dynamic_lights_common.glsl for consistent lighting
    vec3 dynamicLights = calculateAllDynamicLightsVegetationNoShadow(N, V, fragWorldPos,
                                                                      albedo, grassRoughness, GRASS_SSS_STRENGTH);

    // === FRESNEL RIM LIGHTING ===
    // Grass blades catch light at grazing angles using unified constants
    // Wet grass has enhanced rim reflections (water has strong fresnel)
    float NoV = max(dot(N, V), 0.0);
    float rimFresnel = pow(1.0 - NoV, GRASS_RIM_FRESNEL_POWER);
    float wetRimBoost = mix(1.0, 1.5, wetness);  // Stronger rim when wet
    // Rim color based on sky/ambient
    vec3 rimColor = ubo.ambientColor.rgb * 0.5 + ubo.sunColor.rgb * ubo.toSunDirection.w * 0.2;
    vec3 rimLight = rimColor * rimFresnel * GRASS_RIM_INTENSITY * wetRimBoost;

    // === AMBIENT LIGHTING ===
    // Ambient occlusion - darker at base, brighter at tip using unified constants
    float ao = GRASS_AO_BASE + fragHeight * (GRASS_AO_TIP - GRASS_AO_BASE);

    // Height-based ambient color shift (base is cooler/darker, tips catch more sky)
    vec3 ambientBase = ubo.ambientColor.rgb * vec3(0.8, 0.85, 0.9);  // Cooler at base
    vec3 ambientTip = ubo.ambientColor.rgb * vec3(1.0, 1.0, 0.95);   // Warmer at tip
    vec3 ambient = albedo * mix(ambientBase, ambientTip, fragHeight);

    // === COMBINE LIGHTING ===
    vec3 finalColor = (ambient + sunLight + moonLight + dynamicLights + rimLight) * ao;

    // === AERIAL PERSPECTIVE ===
    vec3 atmosphericColor = applyAerialPerspectiveSimple(finalColor, fragWorldPos);

    outColor = vec4(atmosphericColor, 1.0);
}
