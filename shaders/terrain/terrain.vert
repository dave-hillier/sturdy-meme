#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain.vert - Terrain vertex shader
 * Decodes triangle vertices from CBT/LEB and samples height map
 *
 * TERRAIN HEIGHT CONVENTION (Authoritative Source):
 * Height formula: worldY = h * heightScale
 * Where h is normalized [0,1] and heightScale is max height in meters.
 * See terrain_height_common.glsl for shared implementation.
 */

#include "../bindings.glsl"

#define CBT_BUFFER_BINDING BINDING_TERRAIN_CBT_BUFFER
#include "cbt.glsl"
#include "leb.glsl"
#include "../terrain_height_common.glsl"
#include "../snow_common.glsl"

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

// Volumetric snow cascades
layout(binding = BINDING_TERRAIN_SNOW_CASCADE_0) uniform sampler2D snowCascade0;
layout(binding = BINDING_TERRAIN_SNOW_CASCADE_1) uniform sampler2D snowCascade1;
layout(binding = BINDING_TERRAIN_SNOW_CASCADE_2) uniform sampler2D snowCascade2;

// Uniform buffer
layout(std140, binding = BINDING_TERRAIN_UBO) uniform TerrainUniforms {
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 viewProjMatrix;
    vec4 frustumPlanes[6];
    vec4 cameraPosition;
    vec4 terrainParams;   // x = size, y = height scale, z = target edge pixels, w = max depth
    vec4 lodParams;       // x = split threshold, y = merge threshold, z = min depth, w = unused
    vec2 screenSize;
    float lodFactor;
    float padding;
    // Volumetric snow parameters
    vec4 snowCascade0Params;  // xy = origin, z = size, w = texel size
    vec4 snowCascade1Params;  // xy = origin, z = size, w = texel size
    vec4 snowCascade2Params;  // xy = origin, z = size, w = texel size
    float useVolumetricSnow;  // 1.0 = enabled
    float snowMaxHeight;      // Maximum snow height in meters
    float snowPadding1;
    float snowPadding2;
};

#define TERRAIN_SIZE (terrainParams.x)
#define HEIGHT_SCALE (terrainParams.y)

// Output to fragment shader
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out float fragDepth;

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
        return texture(heightMapTiles, vec3(tileUV, float(tileIdx))).r * HEIGHT_SCALE;
    }
    // Fall back to global coarse texture
    return texture(heightMapGlobal, uv).r * HEIGHT_SCALE;
}

// Calculate normal from height map gradient with LOD support
vec3 calculateNormal(vec2 uv, vec2 worldXZ) {
    vec2 texelSize = 1.0 / vec2(textureSize(heightMapGlobal, 0));
    float worldTexelSize = TERRAIN_SIZE / float(textureSize(heightMapGlobal, 0).x);

    // Sample neighboring heights with LOD support
    float hL = sampleHeightLOD(uv + vec2(-texelSize.x, 0.0), worldXZ + vec2(-worldTexelSize, 0.0));
    float hR = sampleHeightLOD(uv + vec2(texelSize.x, 0.0), worldXZ + vec2(worldTexelSize, 0.0));
    float hD = sampleHeightLOD(uv + vec2(0.0, -texelSize.y), worldXZ + vec2(0.0, -worldTexelSize));
    float hU = sampleHeightLOD(uv + vec2(0.0, texelSize.y), worldXZ + vec2(0.0, worldTexelSize));

    float dx = (hR - hL) / (2.0 * worldTexelSize);
    float dz = (hU - hD) / (2.0 * worldTexelSize);

    return normalize(vec3(-dx, 1.0, -dz));
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
        (uv.x - 0.5) * TERRAIN_SIZE,
        (uv.y - 0.5) * TERRAIN_SIZE
    );

    // Sample height with LOD tile support
    float height = sampleHeightLOD(uv, worldXZ);

    // Compute world position
    vec3 worldPos = vec3(worldXZ.x, height, worldXZ.y);

    // Calculate normal with LOD support
    vec3 normal = calculateNormal(uv, worldXZ);

    // Apply volumetric snow displacement
    if (useVolumetricSnow > 0.5) {
        // Sample snow height from cascades
        float snowHeight = sampleVolumetricSnowHeight(
            snowCascade0, snowCascade1, snowCascade2,
            worldPos, cameraPosition.xyz,
            snowCascade0Params, snowCascade1Params, snowCascade2Params
        );

        // Calculate snow coverage based on height and slope
        // Only displace when there's actual visible snow (coverage threshold)
        float snowCoverage = snowHeightToCoverage(snowHeight, 1.0, normal);

        // Only displace if coverage is significant (threshold at ~50% coverage)
        if (snowCoverage > 0.5) {
            worldPos = displaceVertexBySnow(worldPos, snowHeight, normal);
        }
    }

    // Transform to clip space
    vec4 clipPos = viewProjMatrix * vec4(worldPos, 1.0);

    // Output
    gl_Position = clipPos;
    fragTexCoord = uv;
    fragNormal = normal;
    fragWorldPos = worldPos;
    fragDepth = float(node.depth);
}
