#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"
#include "tree_lighting_common.glsl"
#include "dither_common.glsl"
#include "normal_mapping_common.glsl"

layout(binding = BINDING_TREE_GFX_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_TREE_GFX_BARK_ALBEDO) uniform sampler2D barkAlbedo;
layout(binding = BINDING_TREE_GFX_BARK_NORMAL) uniform sampler2D barkNormal;
layout(binding = BINDING_TREE_GFX_BARK_ROUGHNESS) uniform sampler2D barkRoughness;
layout(binding = BINDING_TREE_GFX_BARK_AO) uniform sampler2D barkAO;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float time;
    float lodBlendFactor;  // 0=full geometry, 1=full impostor
    vec2 _pad;             // Explicit padding to align vec3 to 16 bytes
    vec3 barkTint;
    float roughnessScale;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragTangent;
layout(location = 4) in float fragBranchLevel;

layout(location = 0) out vec4 outColor;

// Note: perturbNormal is provided by normal_mapping_common.glsl
// Note: Bayer dithering is provided by dither_common.glsl

void main() {
    // LOD dithered fade-out - discard more pixels as blend factor increases
    // (inverse of impostor fade-in)
    if (shouldDiscardForLOD(push.lodBlendFactor)) {
        discard;
    }

    // Sample bark textures
    vec4 albedo = texture(barkAlbedo, fragTexCoord);
    vec3 baseColor = albedo.rgb * push.barkTint;

    // Normal mapping
    vec3 N = normalize(fragNormal);
    if (fragTangent.xyz != vec3(0.0)) {
        N = perturbNormal(N, fragTangent, barkNormal, fragTexCoord);
    }

    // Sample PBR maps
    float roughness = texture(barkRoughness, fragTexCoord).r * push.roughnessScale;
    float ao = texture(barkAO, fragTexCoord).r;
    float softAO = mix(1.0, ao, 0.5);  // Reduce AO darkening effect

    // View and light directions
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 L = normalize(ubo.sunDirection.xyz);  // sunDirection points toward sun

    // Calculate shadow
    float shadow = calculateCascadedShadow(
        fragWorldPos, N, L,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Calculate lighting using common function
    vec3 color = calculateTreeBarkLighting(
        N, V, L,
        baseColor,
        roughness,
        softAO,
        shadow,
        ubo.sunColor.rgb,
        ubo.sunDirection.w,
        ubo.ambientColor.rgb
    );

    // Apply aerial perspective for distant trees
    color = applyAerialPerspectiveSimple(color, fragWorldPos);

    outColor = vec4(color, 1.0);
}
