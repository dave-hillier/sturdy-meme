/*
 * tile_cache_common.glsl - Shared tile cache sampling functions
 *
 * This file provides functions for sampling terrain height from the LOD tile cache.
 * The tile cache stores high-resolution 512x512 tiles near the camera with fallback
 * to the global coarse heightmap for distant terrain.
 *
 * PREREQUISITES - Define these BEFORE including this file:
 * 1. TileInfo struct with vec4 worldBounds and vec4 uvScaleOffset
 * 2. TileInfoBuffer SSBO containing:
 *    - uint activeTileCount
 *    - TileInfo tiles[]
 * 3. Include terrain_height_common.glsl (for terrainHeightToWorld, sampleTerrainHeight)
 *
 * USAGE:
 *   float height = sampleHeightWithTileCache(heightMapGlobal, heightMapTiles,
 *                                             uv, worldXZ, heightScale, activeTileCount);
 *   vec3 normal = calculateNormalWithTileCache(heightMapGlobal, heightMapTiles,
 *                                               uv, worldXZ, terrainSize, heightScale, activeTileCount);
 */

#ifndef TILE_CACHE_COMMON_GLSL
#define TILE_CACHE_COMMON_GLSL

// Find tile index covering world position, returns -1 if no tile loaded
// Requires: tiles[] array defined in TileInfoBuffer
int tileCacheFindTile(vec2 worldXZ, uint tileCount) {
    for (uint i = 0u; i < tileCount && i < 64u; i++) {
        vec4 bounds = tiles[i].worldBounds;
        if (worldXZ.x >= bounds.x && worldXZ.x < bounds.z &&
            worldXZ.y >= bounds.y && worldXZ.y < bounds.w) {
            return int(i);
        }
    }
    return -1;
}

// Sample height with LOD tile support
// Returns world-space height value
float sampleHeightWithTileCache(sampler2D heightMapGlobal, sampler2DArray heightMapTiles,
                                 vec2 uv, vec2 worldXZ, float heightScale, uint tileCount) {
    int tileIdx = tileCacheFindTile(worldXZ, tileCount);
    if (tileIdx >= 0) {
        // High-res tile available - calculate local UV within tile
        vec4 bounds = tiles[tileIdx].worldBounds;
        vec2 tileUV = (worldXZ - bounds.xy) / (bounds.zw - bounds.xy);
        float h = texture(heightMapTiles, vec3(tileUV, float(tileIdx))).r;
        return terrainHeightToWorld(h, heightScale);
    }
    // Fall back to global coarse texture
    return sampleTerrainHeight(heightMapGlobal, uv, heightScale);
}

// Calculate terrain normal from height gradient with tile cache support
// Returns normalized world-space normal vector
vec3 calculateNormalWithTileCache(sampler2D heightMapGlobal, sampler2DArray heightMapTiles,
                                   vec2 uv, vec2 worldXZ, float terrainSize, float heightScale, uint tileCount) {
    vec2 texelSize = 1.0 / vec2(textureSize(heightMapGlobal, 0));
    float worldTexelSize = terrainSize / float(textureSize(heightMapGlobal, 0).x);

    // Sample neighboring heights with LOD support
    float hL = sampleHeightWithTileCache(heightMapGlobal, heightMapTiles, uv + vec2(-texelSize.x, 0.0), worldXZ + vec2(-worldTexelSize, 0.0), heightScale, tileCount);
    float hR = sampleHeightWithTileCache(heightMapGlobal, heightMapTiles, uv + vec2(texelSize.x, 0.0), worldXZ + vec2(worldTexelSize, 0.0), heightScale, tileCount);
    float hD = sampleHeightWithTileCache(heightMapGlobal, heightMapTiles, uv + vec2(0.0, -texelSize.y), worldXZ + vec2(0.0, -worldTexelSize), heightScale, tileCount);
    float hU = sampleHeightWithTileCache(heightMapGlobal, heightMapTiles, uv + vec2(0.0, texelSize.y), worldXZ + vec2(0.0, worldTexelSize), heightScale, tileCount);

    float dx = (hR - hL) / (2.0 * worldTexelSize);
    float dz = (hU - hD) / (2.0 * worldTexelSize);

    return normalize(vec3(-dx, 1.0, -dz));
}

#endif // TILE_CACHE_COMMON_GLSL
