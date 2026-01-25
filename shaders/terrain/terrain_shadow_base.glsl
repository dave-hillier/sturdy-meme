/*
 * terrain_shadow_base.glsl - Shared code for terrain shadow vertex shaders
 *
 * This file contains the common height sampling, tile cache, and transformation
 * logic used by all terrain shadow vertex shaders:
 *   - terrain_shadow.vert
 *   - terrain_meshlet_shadow.vert
 *   - terrain_shadow_culled.vert
 *   - terrain_meshlet_shadow_culled.vert
 *
 * USAGE:
 *   1. Include bindings.glsl, cbt.glsl, leb.glsl first
 *   2. Include this file
 *   3. In main(), compute UV then call terrainShadowTransform(uv)
 *
 * Height convention: see terrain_height_common.glsl
 */

#ifndef TERRAIN_SHADOW_BASE_GLSL
#define TERRAIN_SHADOW_BASE_GLSL

#include "../terrain_height_common.glsl"

// Height map (global coarse LOD - fallback for distant terrain)
layout(binding = BINDING_TERRAIN_HEIGHT_MAP) uniform sampler2D heightMapGlobal;

// LOD tile array (high-res tiles near camera)
layout(binding = BINDING_TERRAIN_TILE_ARRAY) uniform sampler2DArray heightMapTiles;

// Tile info buffer - world bounds for each active tile
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

#include "../tile_cache_common.glsl"

// Push constants for per-cascade matrix
layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
    float terrainSize;
    float heightScale;
    int cascadeIndex;
    int padding;
};

// Output UV and world position for hole mask sampling in fragment shader
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec2 fragWorldXZ;

/*
 * Transform terrain UV to light-space position for shadow rendering.
 * Samples height with tile cache support and outputs to gl_Position and fragTexCoord.
 *
 * Call this from main() after computing the UV coordinates.
 */
void terrainShadowTransform(vec2 uv) {
    // Compute world XZ position (needed for tile lookup)
    vec2 worldXZ = vec2(
        (uv.x - 0.5) * terrainSize,
        (uv.y - 0.5) * terrainSize
    );

    // Sample height with tile cache support (~1m resolution near camera)
    float height = sampleHeightWithTileCache(heightMapGlobal, heightMapTiles, uv,
                                              worldXZ, heightScale, activeTileCount);

    // Compute world position
    vec3 worldPos = vec3(worldXZ.x, height, worldXZ.y);

    // Transform to light space
    gl_Position = lightViewProj * vec4(worldPos, 1.0);

    // Pass UV and world position for hole mask sampling
    fragTexCoord = uv;
    fragWorldXZ = worldXZ;
}

#endif // TERRAIN_SHADOW_BASE_GLSL
