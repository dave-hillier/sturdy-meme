#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"

layout(binding = BINDING_TREE_GFX_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_TREE_GFX_LEAF_ALBEDO) uniform sampler2D leafAlbedo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float time;
    vec3 leafTint;
    float alphaTest;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragLeafSize;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample leaf texture
    vec4 albedo = texture(leafAlbedo, fragTexCoord);

    // Alpha test for leaf transparency
    if (albedo.a < push.alphaTest) {
        discard;
    }

    vec3 baseColor = albedo.rgb * push.leafTint;

    // Use geometry normal (leaves are flat)
    vec3 N = normalize(fragNormal);

    // Make leaves double-sided
    if (!gl_FrontFacing) {
        N = -N;
    }

    // View and light directions
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 L = normalize(-ubo.sunDirection.xyz);

    // Calculate shadow
    float shadow = calculateCascadedShadow(
        fragWorldPos, N, L,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Simple lighting for leaves
    float NdotL = max(dot(N, L), 0.0);

    // Subsurface scattering approximation (light through leaves)
    float NdotLBack = max(dot(-N, L), 0.0);
    float subsurface = NdotLBack * 0.5;

    // Diffuse with translucency
    vec3 diffuse = baseColor * (NdotL + subsurface);

    // Sun contribution
    vec3 sunColor = ubo.sunColor.rgb;
    float sunIntensity = ubo.sunDirection.w;
    vec3 sunLighting = sunColor * sunIntensity * shadow;

    vec3 directLight = diffuse * sunLighting;

    // Ambient contribution
    vec3 ambient = baseColor * 0.2;

    // Final color
    vec3 color = directLight + ambient;

    // Apply aerial perspective for distant leaves
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDist = length(cameraToFrag);
    vec3 viewDir = normalize(cameraToFrag);
    vec3 sunDir = normalize(-ubo.sunDirection.xyz);
    color = applyAerialPerspective(color, ubo.cameraPosition.xyz, viewDir, viewDist, sunDir, sunColor);

    outColor = vec4(color, albedo.a);
}
