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
// Horizon Occlusion (Ghost of Tsushima technique)
// ============================================================================

// Accounts for the fact that reflected rays from normal-mapped surfaces
// can be partially occluded by the macro geometry (vertex normal).
// This prevents unrealistic glowing on the backsides of normal map bumps.
//
// Based on: Petri, "Samurai Cinema", SIGGRAPH 2021
//
// Parameters:
//   V: view direction (toward eye)
//   N: vertex/geometric normal
//   Nm: normal map normal (in world space)
//   roughness: surface roughness [0,1]
//
// Returns: occlusion factor [0,1] to multiply with specular reflection

float horizonOcclusion(vec3 V, vec3 N, vec3 Nm, float roughness) {
    // Calculate cone half-angle containing 95% of GGX energy
    // Approximation: theta_c ≈ atan(alpha / sqrt(1 - alpha^2)) where alpha = roughness^2
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    // Avoid division by zero for very rough surfaces
    float thetaC = atan(alpha * 0.5 / max(sqrt(1.0 - alpha2 * 0.25), 0.001));

    // Project normal map normal into plane formed by reflection and vertex normal
    // thetaP = angle between normal map normal and vertex normal
    float cosThetaP = clamp(dot(Nm, N), 0.0, 1.0);
    float thetaP = acos(cosThetaP);

    // Calculate reflection vector and its tilt from vertex normal
    vec3 R = reflect(-V, Nm);
    float cosThetaR = clamp(dot(R, N), -1.0, 1.0);
    float thetaR = acos(cosThetaR);

    // Calculate how much the reflection cone dips below the surface
    // Since BRDF already handles occlusion when thetaP = 0,
    // and 1° change in thetaP causes 2° change in thetaR,
    // we clamp to 2*thetaP to only count extra occlusion
    float thetaO = max(thetaR - 2.0 * thetaP, 0.0);

    // Smooth occlusion based on how much of the cone is below horizon
    // Never occlude more than 95% (the fraction of energy in the cone)
    return 1.0 - smoothstep(0.0, thetaC, thetaO) * 0.95;
}

// Simplified version when vertex normal equals normal map normal
// (for non-normal-mapped surfaces, always returns 1.0)
float horizonOcclusionSimple(vec3 V, vec3 N, float roughness) {
    vec3 R = reflect(-V, N);
    float NoR = dot(N, R);
    // If reflection points away from surface, occlude based on roughness
    if (NoR < 0.0) {
        float alpha = roughness * roughness;
        return smoothstep(-alpha, 0.0, NoR);
    }
    return 1.0;
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
