#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain.vert - Terrain vertex shader
 * Decodes triangle vertices from CBT/LEB and samples height map
 */

#define CBT_BUFFER_BINDING 0
#include "cbt.glsl"
#include "leb.glsl"
#include "../snow_common.glsl"

// Height map
layout(binding = 3) uniform sampler2D heightMap;

// Volumetric snow cascades
layout(binding = 10) uniform sampler2D snowCascade0;
layout(binding = 11) uniform sampler2D snowCascade1;
layout(binding = 12) uniform sampler2D snowCascade2;

// Uniform buffer
layout(std140, binding = 4) uniform TerrainUniforms {
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

// Calculate normal from height map gradient
vec3 calculateNormal(vec2 uv) {
    vec2 texelSize = 1.0 / vec2(textureSize(heightMap, 0));

    float hL = texture(heightMap, uv + vec2(-texelSize.x, 0.0)).r * HEIGHT_SCALE;
    float hR = texture(heightMap, uv + vec2(texelSize.x, 0.0)).r * HEIGHT_SCALE;
    float hD = texture(heightMap, uv + vec2(0.0, -texelSize.y)).r * HEIGHT_SCALE;
    float hU = texture(heightMap, uv + vec2(0.0, texelSize.y)).r * HEIGHT_SCALE;

    // Approximate gradient
    float dx = (hR - hL) / (2.0 * texelSize.x * TERRAIN_SIZE);
    float dz = (hU - hD) / (2.0 * texelSize.y * TERRAIN_SIZE);

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

    // Sample height (center around y=0)
    float height = (texture(heightMap, uv).r - 0.5) * HEIGHT_SCALE;

    // Compute world position
    vec3 worldPos = vec3(
        (uv.x - 0.5) * TERRAIN_SIZE,
        height,
        (uv.y - 0.5) * TERRAIN_SIZE
    );

    // Calculate normal
    vec3 normal = calculateNormal(uv);

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
