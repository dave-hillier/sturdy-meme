#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"
#include "color_common.glsl"
#include "tree_lighting_common.glsl"

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

// 4x4 Bayer dithering matrix (matches tree.frag and tree_impostor.frag)
const float bayerMatrix[16] = float[16](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0, 4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0, 7.0/16.0, 13.0/16.0,  5.0/16.0
);

void main() {
    // LOD dithered fade-out - discard more pixels as blend factor increases
    // (matches tree.frag branch fade-out, inverse of impostor fade-in)
    if (fragLodBlendFactor > 0.01) {
        ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
        int ditherIndex = (pixelCoord.x % 4) + (pixelCoord.y % 4) * 4;
        float ditherValue = bayerMatrix[ditherIndex];
        // Discard if blend factor exceeds dither threshold
        if (fragLodBlendFactor > ditherValue) {
            discard;
        }
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
    vec3 L = normalize(-ubo.sunDirection.xyz);

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
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDist = length(cameraToFrag);
    vec3 viewDir = normalize(cameraToFrag);
    vec3 sunDir = normalize(-ubo.sunDirection.xyz);
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    color = applyAerialPerspective(color, ubo.cameraPosition.xyz, viewDir, viewDist, sunDir, sunColor);

    // Output with alpha=1.0 since we use alpha-test (discard) for transparency
    outColor = vec4(color, 1.0);
}
