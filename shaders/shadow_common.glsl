// Common shadow sampling functions
// Prevent multiple inclusion
#ifndef SHADOW_COMMON_GLSL
#define SHADOW_COMMON_GLSL

#include "constants_common.glsl"

// ============================================================================
// Cascaded Shadow Mapping Functions
// ============================================================================
// Note: These functions expect the following to be available in the shader:
// - uniform UniformBufferObject with cascadeViewProj, view, cascadeSplits, shadowMapSize
// - sampler2DArrayShadow shadowMapArray

// Select cascade based on view-space depth
int selectCascade(float viewDepth, vec4 cascadeSplits) {
    int cascade = 0;
    for (int i = 0; i < NUM_CASCADES - 1; i++) {
        if (viewDepth > cascadeSplits[i]) {
            cascade = i + 1;
        }
    }
    return cascade;
}

// Sample shadow for a specific cascade
// Parameters must be passed from the calling shader's UBO
float sampleShadowForCascade(
    vec3 worldPos,
    vec3 normal,
    vec3 lightDir,
    int cascade,
    mat4 cascadeViewProj,
    float shadowMapSize,
    sampler2DArrayShadow shadowMapArray
) {
    // Transform to light space for this cascade
    vec4 lightSpacePos = cascadeViewProj * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Transform to [0,1] range for UV coordinates
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Check if outside shadow map
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;  // No shadow outside frustum
    }

    // Bias to reduce shadow acne (adjust per cascade)
    float cosTheta = max(dot(normal, lightDir), 0.0);
    float baseBias = 0.0005;
    float cascadeBias = baseBias * (1.0 + float(cascade) * 0.5);
    // Algebraic equivalent of tan(acos(x)) = sqrt(1-x²)/x, avoids trig functions
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float slopeBias = cascadeBias * sinTheta / max(cosTheta, 0.001);
    float bias = clamp(slopeBias, 0.0, 0.01);

    // PCF 3x3 sampling from array texture
    float shadow = 0.0;
    float texelSize = 1.0 / shadowMapSize;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(x, y) * texelSize;
            shadow += texture(shadowMapArray, vec4(projCoords.xy + offset, float(cascade), projCoords.z - bias));
        }
    }
    shadow /= 9.0;

    return shadow;
}

// Cascaded shadow calculation with blending between cascades
// This is a convenience function that handles cascade selection and blending
float calculateCascadedShadow(
    vec3 worldPos,
    vec3 normal,
    vec3 lightDir,
    mat4 view,
    vec4 cascadeSplits,
    mat4 cascadeViewProj[NUM_CASCADES],
    float shadowMapSize,
    sampler2DArrayShadow shadowMapArray
) {
    // Calculate view-space depth
    vec4 viewPos = view * vec4(worldPos, 1.0);
    float viewDepth = -viewPos.z;

    // Select cascade based on depth
    int cascade = selectCascade(viewDepth, cascadeSplits);

    // Sample shadow from the selected cascade
    float shadow = sampleShadowForCascade(
        worldPos, normal, lightDir, cascade,
        cascadeViewProj[cascade], shadowMapSize, shadowMapArray
    );

    // Blend near cascade boundaries to prevent visible seams
    float blendDistance = 5.0;
    if (cascade < NUM_CASCADES - 1) {
        float splitDepth = cascadeSplits[cascade];
        float distToSplit = splitDepth - viewDepth;

        if (distToSplit < blendDistance && distToSplit > 0.0) {
            float nextShadow = sampleShadowForCascade(
                worldPos, normal, lightDir, cascade + 1,
                cascadeViewProj[cascade + 1], shadowMapSize, shadowMapArray
            );
            float blendFactor = smoothstep(0.0, blendDistance, distToSplit);
            shadow = mix(nextShadow, shadow, blendFactor);
        }
    }

    return shadow;
}

// Get cascade index for debug visualization
int getCascadeForDebug(vec3 worldPos, mat4 view, vec4 cascadeSplits) {
    vec4 viewPos = view * vec4(worldPos, 1.0);
    float viewDepth = -viewPos.z;
    return selectCascade(viewDepth, cascadeSplits);
}

// Cascade debug colors
vec3 getCascadeDebugColor(int cascade) {
    const vec3 colors[4] = vec3[](
        vec3(1.0, 0.0, 0.0),  // Red - cascade 0 (closest)
        vec3(0.0, 1.0, 0.0),  // Green - cascade 1
        vec3(0.0, 0.0, 1.0),  // Blue - cascade 2
        vec3(1.0, 1.0, 0.0)   // Yellow - cascade 3 (farthest)
    );
    return colors[cascade];
}

#endif // SHADOW_COMMON_GLSL
