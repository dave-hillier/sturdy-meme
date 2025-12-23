#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "constants_common.glsl"
#include "lighting_common.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"
#include "color_common.glsl"

layout(binding = BINDING_TREE_GFX_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_TREE_GFX_LEAF_ALBEDO) uniform sampler2D leafAlbedo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float time;
    float lodBlendFactor;  // 0=full geometry, 1=full impostor
    vec3 leafTint;
    float alphaTest;
    int firstInstance;
    float autumnHueShift;  // 0=summer green, 1=full autumn colors
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragLeafSize;

layout(location = 0) out vec4 outColor;

// 4x4 Bayer dither matrix for LOD transition
const float bayerMatrix[16] = float[16](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0, 4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0, 7.0/16.0, 13.0/16.0,  5.0/16.0
);

void main() {
    // Sample leaf texture
    vec4 albedo = texture(leafAlbedo, fragTexCoord);

    // Alpha test for leaf transparency
    if (albedo.a < push.alphaTest) {
        discard;
    }

    // LOD dithered fade-out - discard more pixels as blend factor increases
    if (push.lodBlendFactor > 0.01) {
        ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
        int ditherIndex = (pixelCoord.x % 4) + (pixelCoord.y % 4) * 4;
        float ditherValue = bayerMatrix[ditherIndex];
        if (push.lodBlendFactor > ditherValue) {
            discard;
        }
    }

    // Apply tint and autumn hue shift
    vec3 baseColor = albedo.rgb * push.leafTint;
    baseColor = applyAutumnHueShift(baseColor, push.autumnHueShift);

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

    // Output with alpha=1.0 since we use alpha-test (discard) for transparency
    outColor = vec4(color, 1.0);
}
