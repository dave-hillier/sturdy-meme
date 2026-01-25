#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain_shadow.frag - Terrain shadow pass fragment shader
 * Discards fragments in terrain holes, depth is written automatically
 */

#include "../bindings.glsl"

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

// Input from vertex shader
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec2 fragWorldXZ;

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
    float holeMaskValue = sampleHoleMaskTiled(fragWorldXZ);
    if (holeMaskValue > 0.5) {
        discard;
    }

    // Depth is written automatically for shadow mapping
    // No color output needed
}
