#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"
#include "atmosphere_common.glsl"
#include "shadow_common.glsl"
#include "color_common.glsl"
#include "octahedral_mapping.glsl"
#include "tree_lighting_common.glsl"
#include "dither_common.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in float fragBlendFactor;
layout(location = 3) flat in int fragCellIndex;
layout(location = 4) in mat3 fragImpostorToWorld;
layout(location = 7) flat in uint fragArchetypeIndex;

// Octahedral mode inputs
layout(location = 8) in vec2 fragOctaUV;
layout(location = 9) in vec3 fragViewDir;
layout(location = 10) flat in int fragUseOctahedral;
layout(location = 11) in vec2 fragLocalUV;   // Billboard local UV for frame sampling

layout(location = 0) out vec4 outColor;

// Impostor atlas texture arrays (one layer per archetype)
layout(binding = BINDING_TREE_IMPOSTOR_ALBEDO) uniform sampler2DArray albedoAlphaAtlas;
layout(binding = BINDING_TREE_IMPOSTOR_NORMAL) uniform sampler2DArray normalDepthAOAtlas;

// Shadow map
layout(binding = BINDING_TREE_IMPOSTOR_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;

layout(push_constant) uniform PushConstants {
    vec4 cameraPos;     // xyz = camera position, w = autumnHueShift
    vec4 lodParams;     // x = useOctahedral, y = brightness, z = normal strength, w = unused
    vec4 atlasParams;   // x = enableFrameBlending, y = unused, z = unused, w = unused
} push;


// Octahedral normal decoding
vec3 decodeOctahedral(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        vec2 signNotZero = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * signNotZero;
    }
    return normalize(n);
}

// Note: Bayer dithering is provided by dither_common.glsl

// Sample octahedral atlas at a specific frame
vec4 sampleOctahedralFrame(vec2 frameUV, uint archetype) {
    return texture(albedoAlphaAtlas, vec3(frameUV, float(archetype)));
}

vec4 sampleOctahedralNormal(vec2 frameUV, uint archetype) {
    return texture(normalDepthAOAtlas, vec3(frameUV, float(archetype)));
}

void main() {
    vec4 albedoAlpha;
    vec4 normalDepthAO;

    if (fragUseOctahedral != 0) {
        // Octahedral mode with optional frame blending
        bool enableBlending = push.atlasParams.x > 0.5;
        float gridSize = float(OCTA_GRID_SIZE);

        if (enableBlending) {
            // Encode view direction to find which cells to blend
            vec2 octaUV = hemiOctaEncode(fragViewDir);
            vec2 scaledUV = octaUV * gridSize;
            ivec2 cell = ivec2(floor(scaledUV));
            cell = clamp(cell, ivec2(0), ivec2(OCTA_GRID_SIZE - 1));
            vec2 frac = fract(scaledUV);

            // Determine which triangle half we're in (for 3-frame blending)
            bool upperTriangle = (frac.x + frac.y) < 1.0;

            // Frame indices (cells)
            ivec2 frame0 = cell;
            ivec2 frame1 = clamp(cell + ivec2(1, 0), ivec2(0), ivec2(OCTA_GRID_SIZE - 1));
            ivec2 frame2 = upperTriangle
                ? clamp(cell + ivec2(0, 1), ivec2(0), ivec2(OCTA_GRID_SIZE - 1))
                : clamp(cell + ivec2(1, 1), ivec2(0), ivec2(OCTA_GRID_SIZE - 1));

            // Compute blend weights using barycentric-like interpolation
            float w0, w1, w2;
            if (upperTriangle) {
                w0 = 1.0 - frac.x - frac.y;
                w1 = frac.x;
                w2 = frac.y;
            } else {
                w0 = frac.x + frac.y - 1.0;
                w1 = 1.0 - frac.y;
                w2 = 1.0 - frac.x;
            }
            // Normalize weights
            float wSum = w0 + w1 + w2;
            w0 /= wSum; w1 /= wSum; w2 /= wSum;

            // Compute atlas UVs for each frame using the billboard's local UV
            vec2 frameUV0 = (vec2(frame0) + fragLocalUV) / gridSize;
            vec2 frameUV1 = (vec2(frame1) + fragLocalUV) / gridSize;
            vec2 frameUV2 = (vec2(frame2) + fragLocalUV) / gridSize;

            // Sample all 3 frames
            vec4 albedo0 = sampleOctahedralFrame(frameUV0, fragArchetypeIndex);
            vec4 albedo1 = sampleOctahedralFrame(frameUV1, fragArchetypeIndex);
            vec4 albedo2 = sampleOctahedralFrame(frameUV2, fragArchetypeIndex);

            vec4 normal0 = sampleOctahedralNormal(frameUV0, fragArchetypeIndex);
            vec4 normal1 = sampleOctahedralNormal(frameUV1, fragArchetypeIndex);
            vec4 normal2 = sampleOctahedralNormal(frameUV2, fragArchetypeIndex);

            // Blend based on weights
            albedoAlpha = albedo0 * w0 + albedo1 * w1 + albedo2 * w2;
            normalDepthAO = normal0 * w0 + normal1 * w1 + normal2 * w2;

            // Better alpha handling for blending - bias toward max to prevent edge flickering
            float maxAlpha = max(max(albedo0.a, albedo1.a), albedo2.a);
            float blendedAlpha = albedo0.a * w0 + albedo1.a * w1 + albedo2.a * w2;
            albedoAlpha.a = mix(blendedAlpha, maxAlpha, 0.7);
        } else {
            // Single frame lookup (no blending) - use fragTexCoord which is already computed
            vec3 atlasCoord = vec3(fragTexCoord, float(fragArchetypeIndex));
            albedoAlpha = texture(albedoAlphaAtlas, atlasCoord);
            normalDepthAO = texture(normalDepthAOAtlas, atlasCoord);
        }
    } else {
        // Legacy mode: direct texture lookup
        vec3 atlasCoord = vec3(fragTexCoord, float(fragArchetypeIndex));
        albedoAlpha = texture(albedoAlphaAtlas, atlasCoord);
        normalDepthAO = texture(normalDepthAOAtlas, atlasCoord);
    }

    // Alpha test
    if (albedoAlpha.a < 0.5) {
        discard;
    }

    // LOD dithered transition (fade-in: blend 0=invisible, 1=visible)
    if (shouldDiscardForLODFadeIn(fragBlendFactor)) {
        discard;
    }

    // Decode normal from octahedral encoding
    vec3 impostorNormal = decodeOctahedral(normalDepthAO.rg);

    // Transform normal from impostor space to world space
    float normalStrength = push.lodParams.z;
    vec3 worldNormal = normalize(fragImpostorToWorld * impostorNormal);
    worldNormal = mix(vec3(0.0, 1.0, 0.0), worldNormal, normalStrength);

    float ao = normalDepthAO.a;

    // Get albedo and apply brightness adjustment
    vec3 albedo = albedoAlpha.rgb * push.lodParams.y;

    // Apply autumn hue shift
    float autumnFactor = push.cameraPos.w;
    albedo = applyAutumnHueShift(albedo, autumnFactor);

    // Calculate lighting directions
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);
    vec3 L = normalize(ubo.sunDirection.xyz);  // sunDirection points toward sun

    // Shadow sampling
    float shadow = calculateCascadedShadow(
        fragWorldPos, worldNormal, L,
        ubo.view, ubo.cascadeSplits, ubo.cascadeViewProj,
        ubo.shadowMapSize, shadowMapArray
    );

    // Calculate lighting using common function
    vec3 color = calculateTreeImpostorLighting(
        worldNormal, V, L,
        albedo,
        ao,
        shadow,
        ubo.sunColor.rgb,
        ubo.sunDirection.w,
        ubo.ambientColor.rgb
    );

    // Apply aerial perspective for distant impostors
    color = applyAerialPerspectiveSimple(color, fragWorldPos);

    outColor = vec4(color, 1.0);
}
