#pragma once

// ============================================================================
// AUTHORITATIVE TERRAIN HEIGHT FUNCTIONS (C++)
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
// For GLSL shaders, use terrain_height_common.glsl
// ============================================================================

namespace TerrainHeight {

// Convert normalized height [0,1] to world-space height
inline float toWorld(float normalizedHeight, float heightScale) {
    return normalizedHeight * heightScale;
}

// Convert world XZ position to heightmap UV coordinates
inline void worldToUV(float worldX, float worldZ, float terrainSize,
                      float& outU, float& outV) {
    outU = (worldX / terrainSize) + 0.5f;
    outV = (worldZ / terrainSize) + 0.5f;
}

// Check if UV coordinates are within terrain bounds
inline bool isUVInBounds(float u, float v) {
    return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
}

} // namespace TerrainHeight
