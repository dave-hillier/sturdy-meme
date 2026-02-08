#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain.frag - Terrain fragment shader
 * Simple PBR-based terrain rendering with shadow support
 */

#include "../bindings.glsl"
#include "../constants_common.glsl"
#include "../lighting_common.glsl"
#include "../shadow_common.glsl"
#include "../snow_common.glsl"
#include "../cloud_shadow_common.glsl"
#include "../terrain_liquid_common.glsl"
#include "../material_layer_common.glsl"
#include "../ubo_material_layer.glsl"

// Virtual texturing support - define USE_VIRTUAL_TEXTURE to enable
#ifdef USE_VIRTUAL_TEXTURE
#include "../virtual_texture.glsl"
#endif

// Terrain uses binding 5 for the main UBO (different descriptor set layout)
#define UBO_BINDING 5
#include "../ubo_common.glsl"
#include "../atmosphere_common.glsl"

// Terrain uses separate bindings for Snow/CloudShadow UBOs to avoid conflicts
// with snow cascade textures at bindings 10-12
#define SNOW_UBO_BINDING BINDING_TERRAIN_SNOW_UBO
#define CLOUD_SHADOW_UBO_BINDING BINDING_TERRAIN_CLOUD_SHADOW_UBO
#include "../ubo_snow.glsl"
#include "../ubo_cloud_shadow.glsl"

// Terrain-specific uniforms
layout(std140, binding = BINDING_TERRAIN_UBO) uniform TerrainUniforms {
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 viewProjMatrix;
    vec4 frustumPlanes[6];
    vec4 terrainCameraPosition;
    vec4 terrainParams;
    vec4 lodParams;
    vec2 screenSize;
    float lodFactor;
    float terrainPadding;
};

// Textures
layout(binding = BINDING_TERRAIN_HEIGHT_MAP) uniform sampler2D heightMap;
layout(binding = BINDING_TERRAIN_ALBEDO) uniform sampler2D terrainAlbedo;
layout(binding = BINDING_TERRAIN_SHADOW_MAP) uniform sampler2DArrayShadow shadowMapArray;
layout(binding = BINDING_TERRAIN_FAR_LOD_GRASS) uniform sampler2D grassFarLODTexture;  // Far LOD grass texture
layout(binding = BINDING_TERRAIN_SNOW_MASK) uniform sampler2D snowMaskTexture;     // World-space snow coverage (legacy)

// Volumetric snow cascade textures (height in meters)
layout(binding = BINDING_TERRAIN_SNOW_CASCADE_0) uniform sampler2D snowCascade0;  // Near cascade (256m)
layout(binding = BINDING_TERRAIN_SNOW_CASCADE_1) uniform sampler2D snowCascade1;  // Mid cascade (1024m)
layout(binding = BINDING_TERRAIN_SNOW_CASCADE_2) uniform sampler2D snowCascade2;  // Far cascade (4096m)

// Cloud shadow map (R16F: 0=shadow, 1=no shadow)
layout(binding = BINDING_TERRAIN_CLOUD_SHADOW) uniform sampler2D cloudShadowMap;

// Hole mask tile array for caves/wells (R8: 0=solid, 1=hole)
layout(binding = BINDING_TERRAIN_HOLE_MASK) uniform sampler2DArray holeMaskTiles;

// Tile info buffer for hole mask lookup
struct TileInfo {
    vec4 worldBounds;    // xy = min corner, zw = max corner
    vec4 uvScaleOffset;  // xy = scale, zw = offset
    ivec4 layerIndex;    // x = layer index in tile array, yzw = padding
};
layout(std430, binding = BINDING_TERRAIN_TILE_INFO) readonly buffer TileInfoBuffer {
    uint activeTileCount;
    uint padding1;
    uint padding2;
    uint padding3;
    TileInfo tiles[];
};

// Underwater caustics texture (Phase 2)
layout(binding = BINDING_TERRAIN_CAUSTICS) uniform sampler2D causticsTexture;

// Caustics parameters UBO (Phase 2)
layout(std140, binding = BINDING_TERRAIN_CAUSTICS_UBO) uniform CausticsUniforms {
    float causticsWaterLevel;      // Water surface height (Y)
    float causticsScale;           // Pattern scale (world units)
    float causticsSpeed;           // Animation speed
    float causticsIntensity;       // Brightness multiplier
    float causticsMaxDepth;        // Maximum depth for caustics visibility
    float causticsTime;            // Animation time
    float causticsEnabled;         // 0 = disabled, 1 = enabled
    float causticsPadding;         // Alignment padding
};

// Terrain liquid effects UBO (composable material system)
layout(std140, binding = BINDING_TERRAIN_LIQUID_UBO) uniform TerrainLiquidUniforms {
    // Global wetness
    float liquidGlobalWetness;
    float liquidPuddleThreshold;
    float liquidMaxPuddleDepth;
    float liquidPuddleEdgeSoftness;

    // Puddle appearance
    vec4 liquidPuddleWaterColor;
    float liquidPuddleRoughness;
    float liquidPuddleReflectivity;
    float liquidPuddleRippleStrength;
    float liquidPuddleRippleScale;

    // Stream parameters
    vec4 liquidStreamWaterColor;
    vec2 liquidStreamFlowDirection;
    float liquidStreamFlowSpeed;
    float liquidStreamWidth;
    float liquidStreamDepth;
    float liquidStreamFoamIntensity;
    float liquidStreamTurbulence;
    float liquidStreamEnabled;

    // Shore wetness
    float liquidShoreWetnessRange;
    float liquidShoreWaveHeight;
    float liquidWaterLevel;
    float liquidPadding1;

    // Animation
    float liquidTime;
    // Note: Use individual floats instead of float[3] array because std140
    // layout gives arrays a 16-byte stride per element, creating a size
    // mismatch with the C++ struct where floats are packed contiguously.
    float liquidPadding2a;
    float liquidPadding2b;
    float liquidPadding2c;
};

// Far LOD grass parameters (where to start/end grass-to-terrain transition)
const float GRASS_RENDER_DISTANCE = 60.0;     // Should match grass system maxDrawDistance
const float FAR_LOD_TRANSITION_START = 50.0;  // Start blending far LOD
const float FAR_LOD_TRANSITION_END = 70.0;    // Full far LOD (terrain with grass tint)

// Triplanar mapping parameters
const float TRIPLANAR_SCALE = 0.1;            // World-space texture scale (larger = bigger texture)
const float TRIPLANAR_SHARPNESS = 4.0;        // Higher = sharper transitions between projections

// Virtual texture world size (should match tile generator output)
// The VT maps from (0,0) to (VT_WORLD_SIZE, VT_WORLD_SIZE) in world XZ coordinates
#ifdef USE_VIRTUAL_TEXTURE
const float VT_WORLD_SIZE = 16384.0;          // World size covered by virtual texture
#endif

// Triplanar texture sampling - samples texture from 3 axis-aligned projections
// and blends based on surface normal to avoid stretching on steep surfaces
vec3 sampleTriplanar(sampler2D tex, vec3 worldPos, vec3 normal, float scale) {
    // Use absolute normal for blend weights
    vec3 blendWeights = abs(normal);

    // Raise to power for sharper transitions
    blendWeights = pow(blendWeights, vec3(TRIPLANAR_SHARPNESS));

    // Normalize so weights sum to 1
    blendWeights /= (blendWeights.x + blendWeights.y + blendWeights.z);

    // Sample texture from each axis projection
    // X-axis projection: use YZ plane
    vec3 xProj = texture(tex, worldPos.yz * scale).rgb;
    // Y-axis projection: use XZ plane (top-down view, most common for terrain)
    vec3 yProj = texture(tex, worldPos.xz * scale).rgb;
    // Z-axis projection: use XY plane
    vec3 zProj = texture(tex, worldPos.xy * scale).rgb;

    // Blend based on weights
    return xProj * blendWeights.x + yProj * blendWeights.y + zProj * blendWeights.z;
}

// Inputs from vertex shader
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in float fragDepth;

layout(location = 0) out vec4 outColor;

// Shadow sampling - use common shadow functions
float getShadowFactor(vec3 worldPos) {
    if (ubo.shadowsEnabled < 0.5) {
        return 1.0;  // No shadow when disabled
    }
    vec3 sunL = normalize(ubo.toSunDirection.xyz);
    return calculateCascadedShadow(
        worldPos,
        normalize(fragNormal),
        sunL,
        ubo.view,
        ubo.cascadeSplits,
        ubo.cascadeViewProj,
        ubo.shadowMapSize,
        shadowMapArray
    );
}

// =========================================================================
// PHASE 2: Underwater Caustics
// Project animated light patterns onto terrain below water surface
// =========================================================================
vec3 calculateUnderwaterCaustics(vec3 worldPos, vec3 sunDir, vec3 sunColor, float shadow) {
    // Check if caustics are enabled and fragment is underwater
    if (causticsEnabled < 0.5) {
        return vec3(0.0);
    }

    // Calculate underwater depth (positive = below water)
    float underwaterDepth = causticsWaterLevel - worldPos.y;

    // Only apply caustics to underwater surfaces
    if (underwaterDepth <= 0.0) {
        return vec3(0.0);
    }

    // Depth-based intensity falloff
    float depthFalloff = 1.0 - smoothstep(0.0, causticsMaxDepth, underwaterDepth);
    if (depthFalloff <= 0.0) {
        return vec3(0.0);
    }

    // Sun angle influence - caustics are stronger with overhead sun
    float sunAngle = max(0.0, sunDir.y);
    float sunInfluence = sunAngle * sunAngle;  // Square for realistic falloff

    // Project caustics using world XZ position with light refraction offset
    // Snell's law approximation: light bends toward vertical when entering water
    // Offset the sampling position based on depth and sun angle
    float refractionIndex = 1.33;  // Water refraction index
    vec2 refractionOffset = sunDir.xz * underwaterDepth * (1.0 / refractionIndex - 1.0) * 0.3;

    // Two-layer caustics animation for richer look (matches water.frag approach)
    // Layer 1: Main caustics pattern
    vec2 causticsUV1 = (worldPos.xz + refractionOffset) * causticsScale
                     + vec2(causticsTime * causticsSpeed * 0.3, causticsTime * causticsSpeed * 0.2);
    float caustic1 = texture(causticsTexture, causticsUV1).r;

    // Layer 2: Secondary pattern at different scale and speed (creates shimmering)
    vec2 causticsUV2 = (worldPos.xz + refractionOffset) * causticsScale * 1.5
                     - vec2(causticsTime * causticsSpeed * 0.2, causticsTime * causticsSpeed * 0.35);
    float caustic2 = texture(causticsTexture, causticsUV2).r;

    // Combine layers - multiply for sharper caustic lines
    float causticPattern = caustic1 * caustic2 * 2.0 + (caustic1 + caustic2) * 0.25;
    causticPattern = clamp(causticPattern, 0.0, 1.0);

    // Final caustic strength
    float causticStrength = causticPattern * depthFalloff * sunInfluence * causticsIntensity;

    // Caustics are sun-colored light focused by wave refraction
    // Apply shadow to reduce caustics in shaded areas
    return sunColor * causticStrength * shadow;
}

// Sample hole mask from tile array using world position
float sampleHoleMaskTiled(vec2 worldXZ) {
    for (uint i = 0u; i < activeTileCount && i < 64u; i++) {
        vec4 bounds = tiles[i].worldBounds;
        if (worldXZ.x >= bounds.x && worldXZ.x < bounds.z &&
            worldXZ.y >= bounds.y && worldXZ.y < bounds.w) {
            vec2 tileUV = (worldXZ - bounds.xy) / (bounds.zw - bounds.xy);
            int layerIdx = tiles[i].layerIndex.x;
            if (layerIdx >= 0) {
                // Apply half-texel correction for GPU sampling
                ivec2 tileSize = textureSize(holeMaskTiles, 0).xy;
                float N = float(tileSize.x);
                vec2 correctedUV = (tileUV * (N - 1.0) + 0.5) / N;
                return texture(holeMaskTiles, vec3(correctedUV, float(layerIdx))).r;
            }
        }
    }
    return 0.0; // No tile covers this position - solid ground
}

void main() {
    // Check hole mask - discard fragment if in a hole (cave/well entrance)
    float holeMaskValue = sampleHoleMaskTiled(fragWorldPos.xz);
    if (holeMaskValue > 0.5) {
        discard;
    }

    // Normalize the surface normal
    vec3 normal = normalize(fragNormal);
    float slope = 1.0 - normal.y;

    // Sample terrain albedo
    vec3 albedo;

#ifdef USE_VIRTUAL_TEXTURE
    // Virtual Texture: Sample pre-composited megatexture (includes roads, rivers, biome materials)
    // Convert world XZ position to virtual texture UV
    vec2 vtUV = fragWorldPos.xz / VT_WORLD_SIZE;

    // Clamp UV to valid range (handles terrain edges)
    vtUV = clamp(vtUV, 0.0, 1.0);

    // Sample with automatic mip selection
    vec4 vtSample = sampleVirtualTextureAuto(vtUV);
    albedo = vtSample.rgb;

    // On steep slopes, blend with cliff material (not baked into VT for triplanar quality)
    // Use triplanar for cliffs to avoid stretching on vertical surfaces
    if (slope > 0.5) {
        vec3 cliffAlbedo = sampleTriplanar(terrainAlbedo, fragWorldPos, normal, TRIPLANAR_SCALE);
        float cliffBlend = smoothstep(0.5, 0.8, slope);
        albedo = mix(albedo, cliffAlbedo, cliffBlend);
    }
#else
    // Fallback: Use triplanar mapping for terrain albedo - prevents texture stretching on steep slopes
    // On flat surfaces (high normal.y), this mainly uses the Y projection (top-down XZ plane)
    // On steep cliffs, it blends in X and Z projections to avoid stretching
    albedo = sampleTriplanar(terrainAlbedo, fragWorldPos, normal, TRIPLANAR_SCALE);
#endif

    // === FAR LOD GRASS BLENDING ===
    // At distances beyond grass render distance, blend in grass texture to maintain
    // grass coverage without rendering individual blades
    float distToCamera = length(fragWorldPos - ubo.cameraPosition.xyz);
    float farLodBlend = smoothstep(FAR_LOD_TRANSITION_START, FAR_LOD_TRANSITION_END, distToCamera);

    // Sample far LOD grass texture (tiled based on world position for consistency)
    vec2 grassUV = fragWorldPos.xz * 0.05;  // Larger scale for distant grass
    vec3 farGrassColor = texture(grassFarLODTexture, grassUV).rgb;

    // Only apply far LOD grass on flat-ish terrain (where real grass would grow)
    // Grass doesn't grow on steep slopes
    float grassGrowthFactor = 1.0 - smoothstep(0.2, 0.5, slope);

    // Blend far LOD grass into terrain albedo
    // As distance increases, replace terrain texture with grass-tinted ground
    if (farLodBlend > 0.01 && grassGrowthFactor > 0.01) {
        // Mix terrain with far LOD grass color based on distance
        albedo = mix(albedo, farGrassColor, farLodBlend * grassGrowthFactor);
    }

#ifndef USE_VIRTUAL_TEXTURE
    // === MATERIAL LAYER BLENDING (Composable Material System) ===
    // Blend terrain materials based on configured layers or fallback to slope-based
    vec3 rockColor = vec3(0.4, 0.35, 0.3);
    float rockRoughness = 0.95;
    float grassRoughness = 0.8;

    if (materialLayerUbo.numMaterialLayers > 0) {
        // Use configured material layers from UBO
        // Layer 0 is base (grass), layer 1 is rock overlay
        float rockBlend = 0.0;

        for (int i = 1; i < materialLayerUbo.numMaterialLayers && i < MAX_MATERIAL_LAYERS; i++) {
            MaterialLayerData layerData;
            layerData.params0 = materialLayerUbo.materialLayers[i].params0;
            layerData.params1 = materialLayerUbo.materialLayers[i].params1;
            layerData.params2 = materialLayerUbo.materialLayers[i].params2;
            layerData.center = materialLayerUbo.materialLayers[i].center;
            layerData.direction = materialLayerUbo.materialLayers[i].direction;

            float layerBlend = calculateLayerBlendFactor(layerData, fragWorldPos, normal, 0.0);
            rockBlend = max(rockBlend, layerBlend);
        }

        albedo = mix(albedo, rockColor, rockBlend);
    } else {
        // Fallback: use helper functions from material_layer_common.glsl
        float rockBlend = getRockBlendFactor(normal);
        albedo = mix(albedo, rockColor, rockBlend);
    }
#endif

    // Material properties - blend roughness based on rock coverage
    float rockBlendForRoughness = getRockBlendFactor(normal);
    float roughness = mix(grassRoughness, rockRoughness, rockBlendForRoughness);
    float metallic = 0.0;  // Terrain is non-metallic

    // === SNOW LAYER ===
    float snowCoverage = 0.0;
    float snowHeight = 0.0;

    if (snow.useVolumetricSnow > 0.5) {
        // Volumetric snow: sample from cascaded heightfield
        snowHeight = sampleVolumetricSnowHeight(
            snowCascade0, snowCascade1, snowCascade2,
            fragWorldPos, ubo.cameraPosition.xyz,
            snow.snowCascade0Params, snow.snowCascade1Params, snow.snowCascade2Params
        );

        // Convert height to coverage
        snowCoverage = snowHeightToCoverage(snowHeight, snow.snowAmount, normal);

        // Calculate snow normal from heightfield for better surface detail
        if (snowCoverage > 0.1) {
            uint cascadeIdx;
            float blendFactor;
            selectSnowCascade(distToCamera, cascadeIdx, blendFactor);

            vec3 snowNormal;
            float texelSize = 1.0 / 256.0;  // CASCADE_SIZE

            if (cascadeIdx == 0) {
                vec2 snowUV = worldToSnowCascadeUV(fragWorldPos, snow.snowCascade0Params);
                snowNormal = calculateSnowNormalFromHeight(snowCascade0, snowUV, texelSize, snow.snowMaxHeight);
            } else if (cascadeIdx == 1) {
                vec2 snowUV = worldToSnowCascadeUV(fragWorldPos, snow.snowCascade1Params);
                snowNormal = calculateSnowNormalFromHeight(snowCascade1, snowUV, texelSize, snow.snowMaxHeight);
            } else {
                vec2 snowUV = worldToSnowCascadeUV(fragWorldPos, snow.snowCascade2Params);
                snowNormal = calculateSnowNormalFromHeight(snowCascade2, snowUV, texelSize, snow.snowMaxHeight);
            }

            // Blend normals based on coverage
            normal = blendSnowNormal(normal, snowNormal, snowCoverage * 0.5);
        }
    } else {
        // Legacy: sample snow mask at world position
        float snowMaskCoverage = sampleSnowMask(snowMaskTexture, fragWorldPos,
                                                 snow.snowMaskParams.xy, snow.snowMaskParams.z);
        snowCoverage = calculateSnowCoverage(snow.snowAmount, snowMaskCoverage, normal);
    }

    // Apply snow layer to albedo and roughness
    if (snowCoverage > 0.01) {
        // Blend albedo with snow color
        albedo = blendSnowAlbedo(albedo, snow.snowColor.rgb, snowCoverage);
        // Snow is rougher than most terrain
        roughness = mix(roughness, snow.snowRoughness, snowCoverage);
    }

    // === TERRAIN LIQUID EFFECTS (Composable Material System) ===
    // Apply puddles, wet surfaces, and streams from weather/environment
    vec3 liquidReflection = vec3(0.0);
    float liquidReflectionStrength = 0.0;

    if (liquidGlobalWetness > 0.01) {
        // Set up puddle parameters from UBO
        PuddleParams puddleParams;
        puddleParams.depth = liquidMaxPuddleDepth;
        puddleParams.roughness = liquidPuddleRoughness;
        puddleParams.waterColor = liquidPuddleWaterColor.rgb;
        puddleParams.reflectivity = liquidPuddleReflectivity;
        puddleParams.rippleStrength = liquidPuddleRippleStrength;
        puddleParams.edgeSoftness = liquidPuddleEdgeSoftness;

        // Set up stream parameters
        TerrainStreamParams streamParams;
        streamParams.flowDirection = liquidStreamFlowDirection;
        streamParams.flowSpeed = liquidStreamFlowSpeed;
        streamParams.flowWidth = liquidStreamWidth;
        streamParams.depth = liquidStreamDepth;
        streamParams.waterColor = liquidStreamWaterColor.rgb;
        streamParams.foamIntensity = liquidStreamFoamIntensity;
        streamParams.turbulence = liquidStreamTurbulence;

        // Calculate height variation for puddle detection (use slope as proxy)
        float heightVariation = slope;

        // Sky color for reflections (simplified - use ambient)
        vec3 skyColorForReflection = ubo.ambientColor.rgb * 2.0;

        // View direction for fresnel
        vec3 viewDir = normalize(ubo.cameraPosition.xyz - fragWorldPos);

        // Apply terrain liquid effects
        TerrainLiquidResult liquidResult = applyTerrainLiquidEffects(
            albedo,
            normal,
            roughness,
            metallic,
            fragWorldPos,
            liquidGlobalWetness,
            heightVariation,           // puddleMask from terrain features
            0.0,                        // streamMask (would come from data texture)
            streamParams,
            puddleParams,
            viewDir,
            skyColorForReflection,
            liquidTime
        );

        // Apply results
        albedo = liquidResult.albedo;
        normal = liquidResult.normal;
        roughness = liquidResult.roughness;
        metallic = liquidResult.metallic;
        liquidReflection = liquidResult.reflection;
        liquidReflectionStrength = liquidResult.reflectionStrength;
    }

    // View direction
    vec3 V = normalize(ubo.cameraPosition.xyz - fragWorldPos);

    // Calculate sun contribution
    vec3 L = normalize(ubo.toSunDirection.xyz);
    vec3 H = normalize(V + L);

    // PBR calculations using common functions
    vec3 F0 = vec3(F0_DIELECTRIC);  // Non-metallic
    F0 = mix(F0, albedo, metallic);

    float NDF = D_GGX(max(dot(normal, H), 0.0), roughness);
    float G = geometrySmith(normal, V, L, roughness);
    vec3 F = F_Schlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(normal, L), 0.0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(normal, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 radiance = ubo.sunColor.rgb * ubo.toSunDirection.w;
    vec3 sunLight = (kD * albedo / PI + specular) * radiance * NdotL;

    // Shadow (terrain + cloud shadows combined)
    float terrainShadow = getShadowFactor(fragWorldPos);

    // Cloud shadows - sample from cloud shadow map
    float cloudShadowFactor = 1.0;
    if (cloudShadow.cloudShadowEnabled > 0.5) {
        cloudShadowFactor = sampleCloudShadowSoft(cloudShadowMap, fragWorldPos, cloudShadow.cloudShadowMatrix);
    }

    // Combine terrain and cloud shadows
    float shadow = combineShadows(terrainShadow, cloudShadowFactor);

    // === MOON LIGHTING ===
    vec3 moonL = normalize(ubo.moonDirection.xyz);
    vec3 moonH = normalize(V + moonL);
    float moonNdotL = max(dot(normal, moonL), 0.0);

    // Moon PBR (simplified - less specular contribution)
    float moonNDF = D_GGX(max(dot(normal, moonH), 0.0), roughness);
    float moonG = geometrySmith(normal, V, moonL, roughness);
    vec3 moonF = F_Schlick(max(dot(moonH, V), 0.0), F0);

    vec3 moonKS = moonF;
    vec3 moonKD = (vec3(1.0) - moonKS) * (1.0 - metallic);

    vec3 moonNumerator = moonNDF * moonG * moonF;
    float moonDenominator = 4.0 * max(dot(normal, V), 0.0) * moonNdotL + 0.0001;
    vec3 moonSpecular = moonNumerator / moonDenominator;

    vec3 moonRadiance = ubo.moonColor.rgb * ubo.moonDirection.w;
    vec3 moonLight = (moonKD * albedo / PI + moonSpecular * 0.5) * moonRadiance * moonNdotL;

    // Ambient
    vec3 ambient = ubo.ambientColor.rgb * albedo;

    // Combine lighting (sun with shadows, moon without for soft night light)
    vec3 color = ambient + sunLight * shadow + moonLight;

    // === TERRAIN LIQUID REFLECTIONS ===
    // Add puddle/water reflections from composable material system
    if (liquidReflectionStrength > 0.01) {
        color = mix(color, liquidReflection, liquidReflectionStrength);
    }

    // === UNDERWATER CAUSTICS (Phase 2) ===
    // Add animated light patterns to underwater terrain
    vec3 causticsLight = calculateUnderwaterCaustics(fragWorldPos, L, radiance, shadow);
    color += causticsLight * albedo;  // Modulate by albedo for natural look

    // Debug: visualize LOD depth
    #if 0
    float maxDepth = lodParams.w;
    float t = fragDepth / maxDepth;
    color = mix(color, vec3(t, 1.0 - t, 0.0), 0.3);
    #endif

    // === AERIAL PERSPECTIVE ===
    vec3 atmosphericColor = applyAerialPerspectiveSimple(color, fragWorldPos);

    // Debug snow depth visualization
    if (snow.debugSnowDepth > 0.5 && snow.useVolumetricSnow > 0.5) {
        // Color code snow depth: blue (0m) -> cyan -> green -> yellow -> red (max)
        float normalizedDepth = clamp(snowHeight / snow.snowMaxHeight, 0.0, 1.0);

        // Heat map colors
        vec3 depthColor;
        if (normalizedDepth < 0.25) {
            // Blue to cyan (0-25%)
            depthColor = mix(vec3(0.0, 0.0, 0.5), vec3(0.0, 0.5, 1.0), normalizedDepth * 4.0);
        } else if (normalizedDepth < 0.5) {
            // Cyan to green (25-50%)
            depthColor = mix(vec3(0.0, 0.5, 1.0), vec3(0.0, 1.0, 0.0), (normalizedDepth - 0.25) * 4.0);
        } else if (normalizedDepth < 0.75) {
            // Green to yellow (50-75%)
            depthColor = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (normalizedDepth - 0.5) * 4.0);
        } else {
            // Yellow to red (75-100%)
            depthColor = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (normalizedDepth - 0.75) * 4.0);
        }

        // Overlay depth color (70% blend)
        atmosphericColor = mix(atmosphericColor, depthColor, 0.7);
    }

    // DEBUG: Test fog UBO values after alignment fix
    // To enable: change #if 0 to #if 1
    #if 0
    // R = fogDensity * 100 (0.003 -> 0.3, should show red tint)
    // G = fogScaleHeight / 1000 (300 -> 0.3, should show green tint)
    // B = layerDensity * 100 (0.008 -> 0.8, should show strong blue)
    float r = ubo.heightFogParams.z * 100.0;  // fogDensity
    float g = ubo.heightFogParams.y / 1000.0;  // fogScaleHeight
    float b = ubo.heightFogLayerParams.y * 100.0;  // layerDensity
    outColor = vec4(r, g, b, 1.0);
    return;
    #endif

    // Output
    outColor = vec4(atmosphericColor, 1.0);
}
