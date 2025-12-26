#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"
#include "shadow_common.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"
#include "tree_lighting_common.glsl"

layout(binding = BINDING_TREE_GFX_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_TREE_GFX_BARK_ALBEDO) uniform sampler2D barkAlbedo;
layout(binding = BINDING_TREE_GFX_BARK_NORMAL) uniform sampler2D barkNormal;
layout(binding = BINDING_TREE_GFX_BARK_ROUGHNESS) uniform sampler2D barkRoughness;
layout(binding = BINDING_TREE_GFX_BARK_AO) uniform sampler2D barkAO;

layout(push_constant) uniform PushConstants {
    mat4 model;
    float time;
    float lodBlendFactor;  // 0=full geometry, 1=full impostor
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

// 4x4 Bayer dither matrix for LOD transition
const float bayerMatrix[16] = float[16](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0, 4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0, 7.0/16.0, 13.0/16.0,  5.0/16.0
);

void main() {
    // LOD dithered fade-out - discard more pixels as blend factor increases
    // (inverse of impostor fade-in)
    if (push.lodBlendFactor > 0.01) {
        ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
        int ditherIndex = (pixelCoord.x % 4) + (pixelCoord.y % 4) * 4;
        float ditherValue = bayerMatrix[ditherIndex];
        // Discard if blend factor exceeds dither threshold
        // At blendFactor=0.5, about half the pixels are discarded
        if (push.lodBlendFactor > ditherValue) {
            discard;
        }
    }

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
    float softAO = mix(1.0, ao, 0.5);  // Reduce AO darkening effect

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
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDist = length(cameraToFrag);
    vec3 viewDir = normalize(cameraToFrag);
    vec3 sunDir = normalize(-ubo.sunDirection.xyz);
    vec3 sunColor = ubo.sunColor.rgb * ubo.sunDirection.w;
    color = applyAerialPerspective(color, ubo.cameraPosition.xyz, viewDir, viewDist, sunDir, sunColor);

    outColor = vec4(color, 1.0);
}
