#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"

layout(binding = BINDING_TREE_GFX_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_TREE_GFX_BARK_ALBEDO) uniform sampler2D barkAlbedo;
layout(binding = BINDING_TREE_GFX_BARK_NORMAL) uniform sampler2D barkNormal;
layout(binding = BINDING_TREE_GFX_BARK_ROUGHNESS) uniform sampler2D barkRoughness;
layout(binding = BINDING_TREE_GFX_BARK_AO) uniform sampler2D barkAO;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float time;
    vec3 barkTint;
    float roughnessScale;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) in float fragBranchLevel;

layout(location = 0) out vec4 outColor;

// Apply normal map using vertex tangent to build TBN matrix
vec3 perturbNormal(vec3 N, vec4 tangent, vec2 texcoord) {
    vec3 T = normalize(tangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * tangent.w;
    mat3 TBN = mat3(T, B, N);

    vec3 normalSample = texture(barkNormal, texcoord).rgb;
    normalSample = normalSample * 2.0 - 1.0;

    return normalize(TBN * normalSample);
}

void main() {
    // Sample bark textures
    vec4 albedo = texture(barkAlbedo, fragTexCoord);
    vec3 baseColor = albedo.rgb * push.barkTint;

    // Normal mapping
    vec3 N = normalize(fragNormal);
    if (fragTangent.xyz != vec3(0.0)) {
        N = perturbNormal(N, fragTangent, fragTexCoord);
    }

    // Sample PBR maps
    float roughness = texture(barkRoughness, fragTexCoord).r * push.roughnessScale;
    float ao = texture(barkAO, fragTexCoord).r;

    // View and light directions
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 L = normalize(-ubo.sunDirection.xyz);

    // Calculate shadow
    float shadow = calculateCascadedShadow(
        fragWorldPos, N, L,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Simple PBR-ish lighting
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.01);

    // Diffuse
    vec3 diffuse = baseColor * NdotL;

    // Specular (simple Blinn-Phong approximation)
    float shininess = (1.0 - roughness) * 64.0;
    float spec = pow(NdotH, shininess);
    vec3 specular = vec3(spec * 0.2);

    // Sun contribution
    vec3 sunColor = ubo.sunColor.rgb;
    float sunIntensity = ubo.sunDirection.w;
    vec3 sunLighting = sunColor * sunIntensity * shadow;

    vec3 directLight = (diffuse + specular) * sunLighting;

    // Ambient/sky contribution
    vec3 ambient = baseColor * 0.15 * ao;

    // Final color
    vec3 color = directLight + ambient;

    // Apply aerial perspective for distant trees
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDist = length(cameraToFrag);
    vec3 viewDir = normalize(cameraToFrag);
    vec3 sunDir = normalize(-ubo.sunDirection.xyz);
    color = applyAerialPerspective(color, ubo.cameraPosition.xyz, viewDir, viewDist, sunDir, sunColor);

    outColor = vec4(color, 1.0);
}
