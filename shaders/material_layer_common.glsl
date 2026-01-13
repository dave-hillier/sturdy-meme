// Material Layer Blending - shader-side layer evaluation
// Part of the composable material system
#ifndef MATERIAL_LAYER_COMMON_GLSL
#define MATERIAL_LAYER_COMMON_GLSL

// Blend modes (must match BlendMode enum in MaterialLayer.h)
#define BLEND_MODE_HEIGHT      0
#define BLEND_MODE_SLOPE       1
#define BLEND_MODE_MASK        2
#define BLEND_MODE_NOISE       3
#define BLEND_MODE_DISTANCE    4
#define BLEND_MODE_DIRECTIONAL 5
#define BLEND_MODE_ALTITUDE    6

// Maximum layers supported in GPU
#define MAX_MATERIAL_LAYERS 4

// Layer data structure (matches MaterialLayerUBO::LayerData)
struct MaterialLayerData {
    vec4 params0;   // mode, heightMin, heightMax, heightSoftness
    vec4 params1;   // slopeMin, slopeMax, slopeSoftness, opacity
    vec4 params2;   // noiseScale, noiseThreshold, noiseSoftness, invertBlend
    vec4 center;    // distanceCenter.xyz, distanceMin
    vec4 direction; // direction.xyz, distanceMax/directionalScale
};

// ============================================================================
// Noise Functions for Procedural Blending
// ============================================================================

// Simple hash function for noise
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Value noise
float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);

    // Smoothstep interpolation
    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(
        mix(hash(i + vec2(0.0, 0.0)), hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

// Fractal Brownian Motion noise
float fbmNoise(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * valueNoise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

// ============================================================================
// Blend Factor Calculation
// ============================================================================

// Calculate blend factor for a single layer
float calculateLayerBlendFactor(
    MaterialLayerData layer,
    vec3 worldPos,
    vec3 normalWS,
    float maskValue  // From mask texture, if using BLEND_MODE_MASK
) {
    int mode = int(layer.params0.x);
    float heightMin = layer.params0.y;
    float heightMax = layer.params0.z;
    float heightSoftness = layer.params0.w;

    float slopeMin = layer.params1.x;
    float slopeMax = layer.params1.y;
    float slopeSoftness = layer.params1.z;
    float opacity = layer.params1.w;

    float noiseScale = layer.params2.x;
    float noiseThreshold = layer.params2.y;
    float noiseSoftness = layer.params2.z;
    bool invertBlend = layer.params2.w > 0.5;

    vec3 center = layer.center.xyz;
    float distanceMin = layer.center.w;
    vec3 direction = layer.direction.xyz;
    float distanceMax = layer.direction.w;

    float blend = 0.0;

    // Height-based blending
    if (mode == BLEND_MODE_HEIGHT || mode == BLEND_MODE_ALTITUDE) {
        float height = worldPos.y;
        blend = smoothstep(heightMin - heightSoftness, heightMin + heightSoftness, height);
        if (mode == BLEND_MODE_ALTITUDE) {
            // Altitude mode also fades out at max height
            blend *= 1.0 - smoothstep(heightMax - heightSoftness, heightMax + heightSoftness, height);
        }
    }
    // Slope-based blending
    else if (mode == BLEND_MODE_SLOPE) {
        // Calculate slope angle from normal (0 = flat, π/2 = vertical)
        float slopeAngle = acos(clamp(normalWS.y, 0.0, 1.0));
        blend = smoothstep(slopeMin - slopeSoftness, slopeMin + slopeSoftness, slopeAngle);
        blend *= 1.0 - smoothstep(slopeMax - slopeSoftness, slopeMax + slopeSoftness, slopeAngle);
    }
    // Mask-based blending
    else if (mode == BLEND_MODE_MASK) {
        blend = maskValue;
    }
    // Noise-based blending
    else if (mode == BLEND_MODE_NOISE) {
        float noise = fbmNoise(worldPos.xz * noiseScale, 3);
        blend = smoothstep(noiseThreshold - noiseSoftness, noiseThreshold + noiseSoftness, noise);
    }
    // Distance-based blending
    else if (mode == BLEND_MODE_DISTANCE) {
        float dist = length(worldPos - center);
        blend = smoothstep(distanceMin, distanceMax, dist);
    }
    // Directional blending
    else if (mode == BLEND_MODE_DIRECTIONAL) {
        float projected = dot(worldPos - center, normalize(direction));
        blend = smoothstep(0.0, distanceMax, projected);
    }

    // Apply inversion
    if (invertBlend) {
        blend = 1.0 - blend;
    }

    // Apply opacity
    return blend * opacity;
}

// ============================================================================
// Material Property Blending
// ============================================================================

// Blend two material colors based on factor
vec3 blendMaterialColor(vec3 base, vec3 overlay, float factor) {
    return mix(base, overlay, factor);
}

// Blend roughness values
float blendMaterialRoughness(float base, float overlay, float factor) {
    return mix(base, overlay, factor);
}

// Blend metallic values
float blendMaterialMetallic(float base, float overlay, float factor) {
    return mix(base, overlay, factor);
}

// Blend normals (linear blend then renormalize)
vec3 blendMaterialNormal(vec3 base, vec3 overlay, float factor) {
    return normalize(mix(base, overlay, factor));
}

// Full material blend structure
struct BlendedMaterial {
    vec3 albedo;
    vec3 normal;
    float roughness;
    float metallic;
    float ao;
};

// Blend two complete materials
BlendedMaterial blendMaterials(
    BlendedMaterial base,
    BlendedMaterial overlay,
    float factor
) {
    BlendedMaterial result;
    result.albedo = blendMaterialColor(base.albedo, overlay.albedo, factor);
    result.normal = blendMaterialNormal(base.normal, overlay.normal, factor);
    result.roughness = blendMaterialRoughness(base.roughness, overlay.roughness, factor);
    result.metallic = blendMaterialMetallic(base.metallic, overlay.metallic, factor);
    result.ao = mix(base.ao, overlay.ao, factor);
    return result;
}

// ============================================================================
// Height-Based Detail Blending (for terrain)
// ============================================================================

// Common terrain layer presets
// Returns blend factor for grass (flat areas)
float getGrassBlendFactor(vec3 normalWS) {
    float upDot = max(dot(normalWS, vec3(0.0, 1.0, 0.0)), 0.0);
    return smoothstep(0.5, 0.8, upDot);
}

// Returns blend factor for rock (steep slopes)
float getRockBlendFactor(vec3 normalWS) {
    float upDot = max(dot(normalWS, vec3(0.0, 1.0, 0.0)), 0.0);
    return 1.0 - smoothstep(0.3, 0.6, upDot);
}

// Returns blend factor for snow (high altitude + flat)
float getSnowBlendFactor(float height, vec3 normalWS, float snowLine, float transitionHeight) {
    float upDot = max(dot(normalWS, vec3(0.0, 1.0, 0.0)), 0.0);
    float slopeFactor = smoothstep(0.4, 0.8, upDot);
    float heightFactor = smoothstep(snowLine - transitionHeight, snowLine + transitionHeight, height);
    return slopeFactor * heightFactor;
}

// Returns blend factor for sand/beach (low altitude near water)
float getSandBlendFactor(float height, float waterLevel, float beachWidth) {
    return 1.0 - smoothstep(waterLevel, waterLevel + beachWidth, height);
}

// ============================================================================
// Shore/Water Edge Blending
// ============================================================================

// Calculate wetness factor based on distance to water
float calculateShoreWetness(float height, float waterLevel, float wetnessRange) {
    if (height < waterLevel) {
        return 1.0; // Underwater = fully wet
    }
    float distAboveWater = height - waterLevel;
    return 1.0 - smoothstep(0.0, wetnessRange, distAboveWater);
}

// Calculate foam/splash zone factor
float calculateFoamZone(float height, float waterLevel, float foamWidth) {
    float dist = abs(height - waterLevel);
    return 1.0 - smoothstep(0.0, foamWidth, dist);
}

#endif // MATERIAL_LAYER_COMMON_GLSL
