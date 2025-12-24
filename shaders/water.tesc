#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * water.tesc - Tessellation Control Shader for Water Surface
 *
 * Determines tessellation levels based on:
 * 1. Distance from camera (screen-space edge length targeting)
 * 2. Each edge is tessellated independently for crack-free tessellation
 *
 * Uses 3 control points per patch (triangles).
 */

#include "ubo_common.glsl"
#include "bindings.glsl"

// Input: 3 vertices per patch (triangle)
layout(vertices = 3) out;

// Input from vertex shader
layout(location = 0) in vec3 tcsWorldPos[];
layout(location = 1) in vec3 tcsNormal[];
layout(location = 2) in vec2 tcsTexCoord[];

// Output to tessellation evaluation shader
layout(location = 0) out vec3 tesWorldPos[];
layout(location = 1) out vec3 tesNormal[];
layout(location = 2) out vec2 tesTexCoord[];

// Tessellation parameters
const float MIN_TESS_LEVEL = 1.0;
const float MAX_TESS_LEVEL = 32.0;
const float NEAR_DISTANCE = 50.0;    // Full tessellation within this distance
const float FAR_DISTANCE = 2000.0;   // Minimum tessellation beyond this distance
const float TARGET_EDGE_PIXELS = 20.0;  // Target screen-space edge length in pixels

// Calculate tessellation level based on distance from camera
// Uses a simple distance-based LOD approach for robustness
float calcEdgeTessLevel(vec3 p0, vec3 p1) {
    vec3 edgeMidpoint = (p0 + p1) * 0.5;
    float edgeLength = length(p1 - p0);

    // Distance from camera to edge midpoint
    float dist = length(edgeMidpoint - ubo.cameraPosition.xyz);

    // Distance-based tessellation factor
    // Full tessellation at NEAR_DISTANCE, minimum at FAR_DISTANCE
    float distanceFactor = 1.0 - smoothstep(NEAR_DISTANCE, FAR_DISTANCE, dist);

    // Scale tessellation by edge length - longer edges get more tessellation
    // This helps maintain consistent triangle sizes after tessellation
    float edgeFactor = clamp(edgeLength / 10.0, 0.5, 2.0);

    float tessLevel = mix(MIN_TESS_LEVEL, MAX_TESS_LEVEL, distanceFactor) * edgeFactor;

    return clamp(tessLevel, MIN_TESS_LEVEL, MAX_TESS_LEVEL);
}

// Frustum culling: returns true if triangle is potentially visible
bool isTriangleVisible(vec3 p0, vec3 p1, vec3 p2) {
    // Check if all three vertices are behind the near plane
    // We use a simple sphere test around the triangle center
    vec3 center = (p0 + p1 + p2) / 3.0;
    float radius = max(max(length(p0 - center), length(p1 - center)), length(p2 - center));

    // Transform to view space
    vec4 viewCenter = ubo.view * vec4(center, 1.0);

    // Simple near/far plane culling with some margin for wave displacement
    float waveMargin = 10.0;  // Account for wave height
    if (viewCenter.z > waveMargin) return false;  // Behind camera (positive z is behind in view space)
    if (-viewCenter.z > ubo.cameraFar + radius) return false;  // Beyond far plane

    return true;
}

void main() {
    // Pass through control point data
    tesWorldPos[gl_InvocationID] = tcsWorldPos[gl_InvocationID];
    tesNormal[gl_InvocationID] = tcsNormal[gl_InvocationID];
    tesTexCoord[gl_InvocationID] = tcsTexCoord[gl_InvocationID];

    // Only compute tessellation levels for the first invocation
    if (gl_InvocationID == 0) {
        vec3 p0 = tcsWorldPos[0];
        vec3 p1 = tcsWorldPos[1];
        vec3 p2 = tcsWorldPos[2];

        // Frustum culling
        if (!isTriangleVisible(p0, p1, p2)) {
            // Cull this patch by setting tessellation levels to 0
            gl_TessLevelOuter[0] = 0.0;
            gl_TessLevelOuter[1] = 0.0;
            gl_TessLevelOuter[2] = 0.0;
            gl_TessLevelInner[0] = 0.0;
            return;
        }

        // Calculate tessellation level for each edge
        // Outer[0] is opposite vertex 0 (edge 1-2)
        // Outer[1] is opposite vertex 1 (edge 2-0)
        // Outer[2] is opposite vertex 2 (edge 0-1)
        float edge0 = calcEdgeTessLevel(p1, p2);
        float edge1 = calcEdgeTessLevel(p2, p0);
        float edge2 = calcEdgeTessLevel(p0, p1);

        gl_TessLevelOuter[0] = edge0;
        gl_TessLevelOuter[1] = edge1;
        gl_TessLevelOuter[2] = edge2;

        // Inner level: average of outer levels
        gl_TessLevelInner[0] = (edge0 + edge1 + edge2) / 3.0;
    }
}
