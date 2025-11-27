// Common PBR lighting functions
// Prevent multiple inclusion
#ifndef LIGHTING_COMMON_GLSL
#define LIGHTING_COMMON_GLSL

#include "constants_common.glsl"

// ============================================================================
// PBR Functions
// ============================================================================

// GGX/Trowbridge-Reitz normal distribution function
float D_GGX(float NoH, float roughness) {
    // Clamp minimum roughness to prevent infinitely tight highlights
    float r = max(roughness, 0.04);
    float a = r * r;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = NoH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith Visibility Function (height-correlated)
float V_SmithGGX(float NoV, float NoL, float roughness) {
    float r = max(roughness, 0.04);
    float a = r * r;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a) + a);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL + 0.0001);
}

// Schlick-GGX geometry function (alternative formulation)
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
vec3 F_Schlick(float VoH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
}

// ============================================================================
// Light Attenuation and Falloff
// ============================================================================

// Calculate attenuation for a light with windowed falloff
float calculateAttenuation(float distance, float radius) {
    if (radius > 0.0) {
        // Windowed inverse-square falloff
        float distRatio = distance / radius;
        float windowedFalloff = max(1.0 - distRatio * distRatio, 0.0);
        windowedFalloff *= windowedFalloff;
        return windowedFalloff / (distance * distance + 0.01);
    } else {
        return 1.0 / (distance * distance + 0.01);
    }
}

// Calculate spot light cone falloff
float calculateSpotFalloff(vec3 L, vec3 spotDir, float innerCone, float outerCone) {
    float cosAngle = dot(-L, spotDir);
    // Smooth falloff between inner and outer cone
    return smoothstep(outerCone, innerCone, cosAngle);
}

// ============================================================================
// Subsurface Scattering (for grass and translucent materials)
// ============================================================================

// Subsurface scattering approximation for thin translucent materials like grass
vec3 calculateSSS(vec3 lightDir, vec3 viewDir, vec3 normal, vec3 lightColor, vec3 albedo, float strength) {
    // Light passing through the blade from behind
    vec3 scatterDir = normalize(lightDir + normal * 0.5);
    float sssAmount = pow(max(dot(viewDir, -scatterDir), 0.0), 3.0);

    // Color shift - light through grass gets more yellow-green
    vec3 sssColor = albedo * vec3(1.1, 1.2, 0.8);

    return sssColor * lightColor * sssAmount * strength;
}

#endif // LIGHTING_COMMON_GLSL
