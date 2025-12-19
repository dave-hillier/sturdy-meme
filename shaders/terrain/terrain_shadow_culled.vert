#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain_shadow_culled.vert - Culled terrain shadow pass vertex shader
 *
 * Reads from a visible indices buffer produced by shadow frustum culling.
 * Only renders triangles that are visible in the shadow cascade's frustum.
 *
 * This optimization avoids rendering terrain triangles that are outside
 * the light's view, reducing vertex shader invocations significantly.
 */

#include "../bindings.glsl"

#define CBT_BUFFER_BINDING BINDING_TERRAIN_CBT_BUFFER
#include "cbt.glsl"
#include "leb.glsl"
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

// Shadow visible indices buffer: [count, index0, index1, ...]
layout(std430, binding = BINDING_TERRAIN_SHADOW_VISIBLE) readonly buffer ShadowVisibleIndices {
    uint shadowVisibleCount;
    uint shadowIndices[];
};

// Push constants for per-cascade matrix
layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
    float terrainSize;
    float heightScale;
    int cascadeIndex;
    int padding;
};

// Output UV for hole mask sampling in fragment shader
layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Determine which visible triangle and vertex within it
    uint visibleTriangleIndex = gl_VertexIndex / 3u;
    uint vertexInTri = gl_VertexIndex % 3u;

    // Look up the actual CBT leaf index from the visible indices buffer
    uint cbtLeafIndex = shadowIndices[visibleTriangleIndex];

    // Map leaf index to CBT node
    cbt_Node node = cbt_DecodeNode(cbtLeafIndex);

    // Decode triangle vertices in UV space
    vec2 v0, v1, v2;
    leb_DecodeTriangleVertices(node, v0, v1, v2);

    // Select vertex based on position in triangle
    vec2 uv;
    if (vertexInTri == 0u) {
        uv = v0;
    } else if (vertexInTri == 1u) {
        uv = v1;
    } else {
        uv = v2;
    }

    // Compute world XZ position first (needed for tile lookup)
    vec2 worldXZ = vec2(
        (uv.x - 0.5) * terrainSize,
        (uv.y - 0.5) * terrainSize
    );

    // Sample height with tile cache support for ~1m resolution near camera
    float height = sampleHeightWithTileCache(heightMapGlobal, heightMapTiles, uv,
                                              worldXZ, heightScale, activeTileCount);

    // Compute world position
    vec3 worldPos = vec3(worldXZ.x, height, worldXZ.y);

    // Transform to light space
    gl_Position = lightViewProj * vec4(worldPos, 1.0);

    // Pass UV for hole mask sampling
    fragTexCoord = uv;
}
