#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain_meshlet_shadow.vert - Meshlet-based terrain shadow pass vertex shader
 *
 * Simplified version for shadow map rendering using meshlet instancing.
 * Each CBT leaf node is rendered as an instance of a pre-subdivided meshlet.
 */

#include "../bindings.glsl"

#define CBT_BUFFER_BINDING BINDING_TERRAIN_CBT_BUFFER
#include "cbt.glsl"
#include "leb.glsl"
#include "../terrain_height_common.glsl"

// Meshlet vertex buffer (local UV coordinates in unit triangle)
layout(location = 0) in vec2 inLocalUV;

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
        float h = texture(heightMapTiles, vec3(tileUV, float(tileIdx))).r;
        return terrainHeightToWorld(h, heightScale);
    }
    // Fall back to global coarse texture
    return sampleTerrainHeight(heightMapGlobal, uv, heightScale);
}

void main() {
    // gl_InstanceIndex tells us which CBT leaf node we're rendering
    uint cbtLeafIndex = uint(gl_InstanceIndex);

    // Decode CBT node from leaf index
    cbt_Node node = cbt_DecodeNode(cbtLeafIndex);

    // Get the transformation matrix for this CBT triangle
    mat3 xform = leb__DecodeTransformationMatrix_Square(node);

    // Convert local meshlet UV to barycentric weights
    vec3 baryWeights = vec3(1.0 - inLocalUV.x - inLocalUV.y, inLocalUV.x, inLocalUV.y);

    // Base triangle coordinates
    vec3 xPos = vec3(0.0, 0.0, 1.0);
    vec3 yPos = vec3(1.0, 0.0, 0.0);

    // Apply LEB transformation and interpolate
    vec3 transformedX = xform * xPos;
    vec3 transformedY = xform * yPos;

    // Compute final UV
    vec2 uv;
    uv.x = dot(baryWeights, transformedX);
    uv.y = dot(baryWeights, transformedY);

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
