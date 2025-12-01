// Cloud Shadow Common Functions
// Include this file in fragment shaders that need to sample cloud shadows

#ifndef CLOUD_SHADOW_COMMON_GLSL
#define CLOUD_SHADOW_COMMON_GLSL

// Sample cloud shadow map at a world position
// cloudShadowMap: the cloud shadow texture (R16F, 0=shadow, 1=no shadow)
// worldPos: world-space position of the fragment
// worldToShadowUV: matrix that transforms world XZ to shadow map UV
// Returns: shadow factor (0 = full shadow, 1 = no shadow)
float sampleCloudShadow(sampler2D cloudShadowMap, vec3 worldPos, mat4 worldToShadowUV) {
    // Transform world position to shadow UV
    vec4 shadowCoord = worldToShadowUV * vec4(worldPos.x, 0.0, worldPos.z, 1.0);
    vec2 shadowUV = shadowCoord.xz;

    // Clamp to valid range and check bounds
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0) {
        return 1.0;  // No shadow outside map bounds
    }

    // Sample shadow map
    float shadow = texture(cloudShadowMap, shadowUV).r;

    return shadow;
}

// Sample cloud shadow with soft edge (bilinear filtering)
// This is the default function to use
float sampleCloudShadowSoft(sampler2D cloudShadowMap, vec3 worldPos, mat4 worldToShadowUV) {
    return sampleCloudShadow(cloudShadowMap, worldPos, worldToShadowUV);
}

// Sample cloud shadow with PCF-style filtering for extra soft edges
// Samples in a 3x3 pattern for smoother shadows
float sampleCloudShadowPCF(sampler2D cloudShadowMap, vec3 worldPos, mat4 worldToShadowUV, float texelSize) {
    vec4 shadowCoord = worldToShadowUV * vec4(worldPos.x, 0.0, worldPos.z, 1.0);
    vec2 shadowUV = shadowCoord.xz;

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0) {
        return 1.0;
    }

    // 3x3 PCF sampling
    float shadow = 0.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            shadow += texture(cloudShadowMap, shadowUV + offset).r;
        }
    }
    shadow /= 9.0;

    return shadow;
}

// Apply cloud shadow to sun lighting
// sunLight: the computed sun lighting contribution
// cloudShadow: cloud shadow factor from sampleCloudShadow
// Returns: modulated sun light
vec3 applyCloudShadow(vec3 sunLight, float cloudShadow) {
    return sunLight * cloudShadow;
}

// Combined shadow factor (terrain CSM shadow * cloud shadow)
// Use this when you have both types of shadows
float combineShadows(float terrainShadow, float cloudShadow) {
    // Multiply shadows together - both must be lit for light to reach
    return terrainShadow * cloudShadow;
}

#endif // CLOUD_SHADOW_COMMON_GLSL
