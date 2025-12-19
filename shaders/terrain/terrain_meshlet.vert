#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain_meshlet.vert - Meshlet-based terrain vertex shader
 *
 * Each CBT leaf node is rendered as an instance of a pre-subdivided meshlet.
 * This provides higher resolution terrain without increasing CBT memory.
 *
 * Meshlet vertices are in local triangle space [0,1]^2:
 *   - (0,0) = apex vertex (v0)
 *   - (1,0) = edge endpoint (v1)
 *   - (0,1) = edge endpoint (v2)
 *
 * The local UV is transformed to the parent CBT triangle's UV space
 * using the LEB transformation matrix.
 */

#include "../bindings.glsl"

#define CBT_BUFFER_BINDING BINDING_TERRAIN_CBT_BUFFER
#include "cbt.glsl"
#include "leb.glsl"
#include "../terrain_height_common.glsl"
#include "../snow_common.glsl"

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
    ivec4 layerIndex;    // x = layer index in tile array, yzw = padding
};
layout(std430, binding = BINDING_TERRAIN_TILE_INFO) readonly buffer TileInfoBuffer {
    uint activeTileCount;
    uint padding1;
    uint padding2;
    uint padding3;
    TileInfo tiles[];
};

// Include tile cache common after defining prerequisites
#include "../tile_cache_common.glsl"

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
    vec4 snowCascade0Params;
    vec4 snowCascade1Params;
    vec4 snowCascade2Params;
    float useVolumetricSnow;
    float snowMaxHeight;
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

void main() {
    // gl_InstanceIndex tells us which CBT leaf node we're rendering
    uint cbtLeafIndex = uint(gl_InstanceIndex);

    // Decode CBT node from leaf index
    cbt_Node node = cbt_DecodeNode(cbtLeafIndex);

    // Get the transformation matrix for this CBT triangle
    // This transforms from unit triangle space to the terrain UV space
    mat3 xform = leb__DecodeTransformationMatrix_Square(node);

    // The meshlet vertex is in local triangle space where:
    // - (0,0) maps to triangle vertex 0 (apex)
    // - (1,0) maps to triangle vertex 1 (edge endpoint)
    // - (0,1) maps to triangle vertex 2 (edge endpoint)
    //
    // We need to convert this to barycentric-like weights for the transformation.
    // For a point (u,v) in the local triangle:
    //   weight0 = 1 - u - v  (apex weight)
    //   weight1 = u          (v1 weight)
    //   weight2 = v          (v2 weight)

    vec3 baryWeights = vec3(1.0 - inLocalUV.x - inLocalUV.y, inLocalUV.x, inLocalUV.y);

    // The base triangle coordinates that get transformed
    vec3 xPos = vec3(0.0, 0.0, 1.0);  // X coordinates of base triangle vertices
    vec3 yPos = vec3(1.0, 0.0, 0.0);  // Y coordinates of base triangle vertices

    // Apply LEB transformation and interpolate using barycentric weights
    vec3 transformedX = xform * xPos;
    vec3 transformedY = xform * yPos;

    // Compute final UV by barycentric interpolation of transformed vertices
    vec2 uv;
    uv.x = dot(baryWeights, transformedX);
    uv.y = dot(baryWeights, transformedY);

    // Compute world XZ position first (needed for tile lookup)
    vec2 worldXZ = vec2(
        (uv.x - 0.5) * TERRAIN_SIZE,
        (uv.y - 0.5) * TERRAIN_SIZE
    );

    // Sample height with LOD tile support
    float height = sampleHeightWithTileCache(heightMapGlobal, heightMapTiles, uv, worldXZ, HEIGHT_SCALE, activeTileCount);

    // Compute world position
    vec3 worldPos = vec3(worldXZ.x, height, worldXZ.y);

    // Calculate normal with LOD support
    vec3 normal = calculateNormalWithTileCache(heightMapGlobal, heightMapTiles, uv, worldXZ, TERRAIN_SIZE, HEIGHT_SCALE, activeTileCount);

    // Apply volumetric snow displacement
    if (useVolumetricSnow > 0.5) {
        float snowHeight = sampleVolumetricSnowHeight(
            snowCascade0, snowCascade1, snowCascade2,
            worldPos, cameraPosition.xyz,
            snowCascade0Params, snowCascade1Params, snowCascade2Params
        );

        float snowCoverage = snowHeightToCoverage(snowHeight, 1.0, normal);

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
