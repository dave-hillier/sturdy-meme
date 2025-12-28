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
#include "terrain_shadow_base.glsl"

// Meshlet vertex buffer (local UV coordinates in unit triangle)
layout(location = 0) in vec2 inLocalUV;

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

    // Apply common shadow transformation
    terrainShadowTransform(uv);
}
