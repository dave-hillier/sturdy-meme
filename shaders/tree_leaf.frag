#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"
#include "color_common.glsl"
#include "tree_lighting_common.glsl"
#include "dither_common.glsl"

// Define snow UBO binding for tree leaf shader (uses tree graphics descriptor set)
#define SNOW_UBO_BINDING BINDING_TREE_GFX_SNOW_UBO
#include "ubo_snow.glsl"

layout(binding = BINDING_TREE_GFX_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_TREE_GFX_LEAF_ALBEDO) uniform sampler2D leafAlbedo;

// Simplified push constants - no more per-tree data
layout(push_constant) uniform PushConstants {
    float time;
    float alphaTest;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragLeafSize;
layout(location = 4) in vec3 fragLeafTint;
layout(location = 5) in float fragAutumnHueShift;
layout(location = 6) in float fragLodBlendFactor;

layout(location = 0) out vec4 outColor;

// Note: Bayer dithering is provided by dither_common.glsl

void main() {
    // LOD dithered fade-out using staggered crossfade
    // Leaves fade out in sync with impostor fade-in for true leaf crossfade
    // This prevents the impostor disappearing while leaves are still dithering in
    if (shouldDiscardForLODLeaves(fragLodBlendFactor)) {
        discard;
    }

    // Sample leaf texture
    vec4 albedo = texture(leafAlbedo, fragTexCoord);

    // Alpha test for leaf transparency
    if (albedo.a < push.alphaTest) {
        discard;
    }

    // Apply tint and autumn hue shift (from vertex shader / tree data SSBO)
    vec3 baseColor = albedo.rgb * fragLeafTint;
    baseColor = applyAutumnHueShift(baseColor, fragAutumnHueShift);

    // === RAIN WETNESS (Composable Material System) ===
    // Wet leaves are darker and shinier due to water film
    float wetness = snow.rainWetness;
    if (wetness > 0.01) {
        // Darken wet leaves (water absorbs some light)
        baseColor *= mix(1.0, 0.7, wetness);
    }

    // Use geometry normal (leaves are flat)
    vec3 N = normalize(fragNormal);

    // Make leaves double-sided
    if (!gl_FrontFacing) {
        N = -N;
    }

    // View and light directions
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 L = normalize(ubo.toSunDirection.xyz);  // sunDirection points toward sun

    // Calculate shadow
    float shadow = 1.0;
    if (ubo.shadowsEnabled > 0.5) {
        shadow = calculateCascadedShadow(
            fragWorldPos, N, L,
            ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
            ubo.shadowMapSize, shadowMapArray
        );
    }

    // Calculate lighting using common function
    vec3 color = calculateTreeLeafLighting(
        N, V, L,
        baseColor,
        shadow,
        ubo.sunColor.rgb,
        ubo.toSunDirection.w,
        ubo.ambientColor.rgb
    );

    // === WET LEAF SPECULAR (Composable Material System) ===
    // Add extra specular reflection from water film on wet leaves
    if (wetness > 0.01) {
        vec3 H = normalize(V + L);
        float NdotH = max(dot(N, H), 0.0);
        // Water film creates sharper, brighter specular highlights
        float wetSpec = pow(NdotH, 64.0) * wetness * 0.3;
        color += vec3(wetSpec) * ubo.sunColor.rgb * ubo.toSunDirection.w * shadow;
    }

    // Apply aerial perspective for distant leaves
    color = applyAerialPerspectiveSimple(color, fragWorldPos);

    // Output with alpha=1.0 since we use alpha-test (discard) for transparency
    outColor = vec4(color, 1.0);
}
