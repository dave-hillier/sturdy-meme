#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain_shadow.vert - Terrain shadow pass vertex shader
 * Simplified version for shadow map rendering
 */

#define CBT_BUFFER_BINDING 0
#include "cbt.glsl"
#include "leb.glsl"

// Height map
layout(binding = 3) uniform sampler2D heightMap;

// Push constants for per-cascade matrix
layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
    float terrainSize;
    float heightScale;
    int cascadeIndex;
    int padding;
};

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
    float height = (texture(heightMap, uv).r - 0.5) * heightScale;

    // Compute world position
    vec3 worldPos = vec3(
        (uv.x - 0.5) * terrainSize,
        height,
        (uv.y - 0.5) * terrainSize
    );

    // Transform to light space
    gl_Position = lightViewProj * vec4(worldPos, 1.0);
}
