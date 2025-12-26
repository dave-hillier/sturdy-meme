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

// Simple lighting for tree leaves (translucent, double-sided)
// Includes subsurface scattering approximation
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

    // Subsurface scattering approximation (light through leaves)
    float NdotLBack = max(dot(-N, L), 0.0);
    float subsurface = NdotLBack * 0.3;

    // Energy-conserving diffuse (divide by PI)
    vec3 diffuse = albedo / PI;

    // Direct lighting with translucency
    vec3 directLight = diffuse * sunColor * sunIntensity * (NdotL + subsurface) * shadow;

    // Ambient lighting
    vec3 ambient = ambientColor * albedo;

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
