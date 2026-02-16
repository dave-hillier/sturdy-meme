#version 460

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

// Cluster raster fragment shader
// Writes (instanceID, triangleID) into a 64-bit (R32G32_UINT) render target
// triangleOffset is passed from vertex shader (originally from per-draw SSBO)

layout(location = 0) flat in uint inInstanceId;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) flat in uint inTriangleOffset;

layout(location = 0) out uvec2 outVisibility;

void main() {
    uint triangleId = uint(gl_PrimitiveID) + inTriangleOffset;

    // 64-bit V-buffer: no bit packing — full 32-bit per channel
    // +1 bias so (0,0) remains the background sentinel
    outVisibility = uvec2(inInstanceId + 1u, triangleId + 1u);
}
