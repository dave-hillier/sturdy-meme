// Common lighting functions for tree rendering
// Ensures consistent lighting between full-detail trees and impostors
#ifndef TREE_LIGHTING_COMMON_GLSL
#define TREE_LIGHTING_COMMON_GLSL

#include "constants_common.glsl"

// Simple PBR lighting for tree bark (non-metallic, moderate roughness)
// Matches the formula used in shader.frag calculatePBR
vec3 calculateTreeBarkLighting(
    vec3 N,           // Surface normal
    vec3 V,           // View direction (toward camera)
    vec3 L,           // Light direction (toward light)
    vec3 albedo,      // Base color
    float roughness,  // Surface roughness
    float ao,         // Ambient occlusion
    float shadow,     // Shadow factor
    vec3 sunColor,    // Sun light color
    float sunIntensity, // Sun light intensity
    vec3 ambientColor // Ambient light color (already includes intensity)
) {
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.001);

    // Energy-conserving diffuse (divide by PI)
    vec3 diffuse = albedo / PI;

    // Simple specular (Blinn-Phong approximation for bark)
    float shininess = (1.0 - roughness) * 64.0;
    float spec = pow(NdotH, shininess);
    vec3 specular = vec3(spec * 0.1);

    // Direct lighting
    vec3 directLight = (diffuse + specular) * sunColor * sunIntensity * NdotL * shadow;

    // Ambient lighting with AO
    vec3 ambient = ambientColor * albedo * ao;

    return directLight + ambient;
}

// GPU Gems 3 style leaf lighting with backlit transmission
// When leaves are backlit (sun behind leaf relative to viewer), light transmits
// through the leaf with a warm yellowish tint due to chlorophyll absorption
vec3 calculateTreeLeafLighting(
    vec3 N,           // Surface normal (already flipped for back-facing)
    vec3 V,           // View direction (toward camera)
    vec3 L,           // Light direction (toward light)
    vec3 albedo,      // Base color (with tint applied)
    float shadow,     // Shadow factor
    vec3 sunColor,    // Sun light color
    float sunIntensity, // Sun light intensity
    vec3 ambientColor // Ambient light color (already includes intensity)
) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // === Backlit Transmission (GPU Gems 3) ===
    // Detect backlit condition: when light and view are on same side of leaf
    // VdotL > 0 means we're looking toward the light source through the leaf
    float VdotL = dot(V, L);
    float backlitFactor = max(0.0, VdotL);

    // Transmission is strongest when:
    // 1. Light is hitting the back of the leaf (NdotLBack high)
    // 2. We're looking toward the light (VdotL > 0)
    float NdotLBack = max(dot(-N, L), 0.0);
    float transmission = NdotLBack * backlitFactor;

    // Warm color shift for transmitted light (chlorophyll absorbs blue/red, passes yellow-green)
    // The tint simulates light passing through leaf tissue
    vec3 transmissionTint = vec3(1.15, 1.1, 0.7); // Warm yellow-green shift
    vec3 transmittedColor = albedo * transmissionTint;

    // Blend between diffuse and transmitted color based on transmission amount
    // Use smoothstep for softer transition
    float transmissionBlend = smoothstep(0.0, 0.6, transmission);
    vec3 leafColor = mix(albedo, transmittedColor, transmissionBlend * 0.7);

    // === Subsurface Scattering (existing) ===
    // Light scattering through leaf thickness, visible even when not directly backlit
    float subsurface = NdotLBack * 0.25;

    // Energy-conserving diffuse (divide by PI)
    vec3 diffuse = leafColor / PI;

    // === Specular with V-shaped Normal Bias (GPU Gems 3) ===
    // Leaves have a central vein that creates a subtle V-shape
    // This biases specular highlights along the leaf axis
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    // Low-intensity, broad specular for waxy leaf surface
    float specPower = 16.0;
    float spec = pow(NdotH, specPower) * 0.08 * (1.0 - transmissionBlend);
    vec3 specular = vec3(spec);

    // Direct lighting with transmission and translucency
    // Transmitted light is less affected by shadows (light passes through other leaves too)
    float effectiveShadow = mix(shadow, 1.0, transmissionBlend * 0.5);
    vec3 directLight = (diffuse + specular) * sunColor * sunIntensity * (NdotL + subsurface + transmission * 0.4) * effectiveShadow;

    // Ambient lighting - transmitted leaves also glow slightly in ambient
    vec3 ambient = ambientColor * leafColor * (1.0 + transmissionBlend * 0.2);

    return directLight + ambient;
}

// Lighting for tree impostors (pre-baked albedo, uses stored AO)
// This is the reference implementation that full-detail trees should match
vec3 calculateTreeImpostorLighting(
    vec3 N,           // World-space normal (decoded from atlas)
    vec3 V,           // View direction (toward camera)
    vec3 L,           // Light direction (toward light)
    vec3 albedo,      // Albedo from impostor atlas
    float ao,         // AO from impostor atlas
    float shadow,     // Shadow factor
    vec3 sunColor,    // Sun light color
    float sunIntensity, // Sun light intensity
    vec3 ambientColor // Ambient light color (already includes intensity)
) {
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    // Energy-conserving diffuse
    vec3 diffuse = albedo / PI;

    // Simple specular
    float roughness = 0.7;
    float spec = pow(NdotH, 32.0) * (1.0 - roughness);
    vec3 specular = vec3(spec) * 0.1;

    // Direct lighting
    vec3 directLight = (diffuse + specular) * sunColor * sunIntensity * NdotL * shadow;

    // Ambient with AO
    vec3 ambient = ambientColor * albedo * ao;

    return directLight + ambient;
}

#endif // TREE_LIGHTING_COMMON_GLSL
