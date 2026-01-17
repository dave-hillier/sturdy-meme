#pragma once

#include <cstdint>
#include <algorithm>

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

// Bilinear sample from a float heightmap array
// u, v: normalized coordinates [0, 1]
// data: row-major float array of size resolution * resolution
// resolution: width/height of the heightmap
// Returns: interpolated normalized height value [0, 1]
inline float sampleBilinear(float u, float v, const float* data, uint32_t resolution) {
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    float fx = u * (resolution - 1);
    float fy = v * (resolution - 1);

    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, static_cast<int>(resolution - 1));
    int y1 = std::min(y0 + 1, static_cast<int>(resolution - 1));

    float tx = fx - x0;
    float ty = fy - y0;

    float h00 = data[y0 * resolution + x0];
    float h10 = data[y0 * resolution + x1];
    float h01 = data[y1 * resolution + x0];
    float h11 = data[y1 * resolution + x1];

    float h0 = h00 * (1.0f - tx) + h10 * tx;
    float h1 = h01 * (1.0f - tx) + h11 * tx;

    return h0 * (1.0f - ty) + h1 * ty;
}

// Convenience overload: sample and convert to world height in one call
inline float sampleWorldHeight(float u, float v, const float* data,
                                uint32_t resolution, float heightScale) {
    return toWorld(sampleBilinear(u, v, data, resolution), heightScale);
}

} // namespace TerrainHeight
