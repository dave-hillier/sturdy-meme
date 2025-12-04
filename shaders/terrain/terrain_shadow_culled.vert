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

// Height map
layout(binding = BINDING_TERRAIN_HEIGHT_MAP) uniform sampler2D heightMap;

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

    // Sample height using shared function
    float height = sampleTerrainHeight(heightMap, uv, heightScale);

    // Compute world position
    vec3 worldPos = vec3(
        (uv.x - 0.5) * terrainSize,
        height,
        (uv.y - 0.5) * terrainSize
    );

    // Transform to light space
    gl_Position = lightViewProj * vec4(worldPos, 1.0);
}
