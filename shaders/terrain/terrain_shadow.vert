#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain_shadow.vert - Terrain shadow pass vertex shader
 *
 * Simplified version for shadow map rendering.
 * Uses CBT leaf nodes directly with 3 vertices per triangle.
 *
 * Height convention: see terrain_height_common.glsl
 */

#include "../bindings.glsl"

#define CBT_BUFFER_BINDING BINDING_TERRAIN_CBT_BUFFER
#include "cbt.glsl"
#include "leb.glsl"
#include "terrain_shadow_base.glsl"

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

    // Apply common shadow transformation
    terrainShadowTransform(uv);
}
