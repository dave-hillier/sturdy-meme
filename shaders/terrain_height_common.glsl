#ifndef TERRAIN_HEIGHT_COMMON_GLSL
#define TERRAIN_HEIGHT_COMMON_GLSL

// ============================================================================
// AUTHORITATIVE TERRAIN HEIGHT FUNCTIONS
// ============================================================================
// Terrain height formula: worldY = h * heightScale
//
// Where:
//   h = normalized heightmap sample in range [0, 1]
//   heightScale = maximum terrain height in world units (meters)
//
// This means:
//   h = 0.0  ->  worldY = 0 (ground level)
//   h = 1.0  ->  worldY = heightScale (maximum height)
//
// DO NOT duplicate this formula elsewhere - use these functions instead.
// ============================================================================

// Convert normalized height [0,1] to world-space height
float terrainHeightToWorld(float h, float heightScale) {
    return h * heightScale;
}

// Sample terrain height from heightmap and convert to world units
float sampleTerrainHeight(sampler2D heightMap, vec2 uv, float heightScale) {
    return texture(heightMap, uv).r * heightScale;
}

// Convert world XZ position to heightmap UV coordinates
vec2 worldPosToTerrainUV(vec2 worldXZ, float terrainSize) {
    return worldXZ / terrainSize + 0.5;
}

// Sample terrain height at world XZ position (with bounds check)
// Returns 0.0 if outside terrain bounds
float sampleTerrainHeightAtWorldPos(sampler2D heightMap, vec2 worldXZ,
                                     float terrainSize, float heightScale) {
    vec2 uv = worldPosToTerrainUV(worldXZ, terrainSize);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 0.0;
    }

    return sampleTerrainHeight(heightMap, uv, heightScale);
}

// Calculate terrain normal from height gradient
vec3 calculateTerrainNormalFromHeightmap(sampler2D heightMap, vec2 uv,
                                          float terrainSize, float heightScale) {
    vec2 texelSize = 1.0 / vec2(textureSize(heightMap, 0));

    float hL = sampleTerrainHeight(heightMap, uv + vec2(-texelSize.x, 0.0), heightScale);
    float hR = sampleTerrainHeight(heightMap, uv + vec2(texelSize.x, 0.0), heightScale);
    float hD = sampleTerrainHeight(heightMap, uv + vec2(0.0, -texelSize.y), heightScale);
    float hU = sampleTerrainHeight(heightMap, uv + vec2(0.0, texelSize.y), heightScale);

    float worldTexelSize = terrainSize / float(textureSize(heightMap, 0).x);
    float dx = (hR - hL) / (2.0 * worldTexelSize);
    float dz = (hU - hD) / (2.0 * worldTexelSize);

    return normalize(vec3(-dx, 1.0, -dz));
}

#endif // TERRAIN_HEIGHT_COMMON_GLSL
