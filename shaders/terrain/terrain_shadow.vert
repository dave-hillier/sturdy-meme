#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain_shadow.vert - Terrain shadow pass vertex shader
 * Simplified version for shadow map rendering
 * Height convention: see terrain_height_common.glsl
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
};
layout(std430, binding = BINDING_TERRAIN_TILE_INFO) readonly buffer TileInfoBuffer {
    uint activeTileCount;
    uint padding1;
    uint padding2;
    uint padding3;
    TileInfo tiles[];
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

// Find tile index covering world position, returns -1 if no tile loaded
int findTileForWorldPos(vec2 worldXZ) {
    for (uint i = 0u; i < activeTileCount && i < 64u; i++) {
        vec4 bounds = tiles[i].worldBounds;
        if (worldXZ.x >= bounds.x && worldXZ.x < bounds.z &&
            worldXZ.y >= bounds.y && worldXZ.y < bounds.w) {
            return int(i);
        }
    }
    return -1;
}

// Sample height with LOD tile support
float sampleHeightLOD(vec2 uv, vec2 worldXZ) {
    int tileIdx = findTileForWorldPos(worldXZ);
    if (tileIdx >= 0) {
        // High-res tile available - calculate local UV within tile
        vec4 bounds = tiles[tileIdx].worldBounds;
        vec2 tileUV = (worldXZ - bounds.xy) / (bounds.zw - bounds.xy);
        return texture(heightMapTiles, vec3(tileUV, float(tileIdx))).r * heightScale;
    }
    // Fall back to global coarse texture
    return texture(heightMapGlobal, uv).r * heightScale;
}

void main() {
    // Determine which triangle and vertex
    uint triangleIndex = gl_VertexIndex / 3u;
    uint vertexInTri = gl_VertexIndex % 3u;

    // Map triangle index to heap index
    cbt_Node node = cbt_DecodeNode(triangleIndex);

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

    // Sample height with LOD tile support
    float height = sampleHeightLOD(uv, worldXZ);

    // Compute world position
    vec3 worldPos = vec3(worldXZ.x, height, worldXZ.y);

    // Transform to light space
    gl_Position = lightViewProj * vec4(worldPos, 1.0);

    // Pass UV for hole mask sampling
    fragTexCoord = uv;
}
