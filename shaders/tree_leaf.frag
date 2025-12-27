#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"
#include "color_common.glsl"
#include "tree_lighting_common.glsl"
#include "dither_common.glsl"

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
    // LOD dithered fade-out - discard more pixels as blend factor increases
    // (matches tree.frag branch fade-out, inverse of impostor fade-in)
    if (shouldDiscardForLOD(fragLodBlendFactor)) {
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

    // Use geometry normal (leaves are flat)
    vec3 N = normalize(fragNormal);

    // Make leaves double-sided
    if (!gl_FrontFacing) {
        N = -N;
    }

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
    vec3 color = calculateTreeLeafLighting(
        N, V, L,
        baseColor,
        shadow,
        ubo.sunColor.rgb,
        ubo.sunDirection.w,
        ubo.ambientColor.rgb
    );

    // Apply aerial perspective for distant leaves
    color = applyAerialPerspectiveSimple(color, fragWorldPos);

    // Output with alpha=1.0 since we use alpha-test (discard) for transparency
    outColor = vec4(color, 1.0);
}
