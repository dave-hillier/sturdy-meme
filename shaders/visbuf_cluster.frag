#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters : require

#include "bindings.glsl"

// Cluster raster fragment shader
// Writes (instanceID, triangleID) into a 64-bit (R32G32_UINT) render target
// triangleOffset is read from per-draw SSBO via gl_DrawID

layout(location = 0) flat in uint inInstanceId;
layout(location = 1) in vec2 inTexCoord;

// Per-draw cluster metadata (same as vertex shader)
struct DrawClusterInfo {
    uint instanceId;
    uint triangleOffset;   // cluster.firstIndex / 3
};

layout(std430, binding = BINDING_CLUSTER_DRAW_INFO) readonly buffer DrawInfoBuffer {
    DrawClusterInfo drawInfos[];
};

layout(location = 0) out uvec2 outVisibility;

void main() {
    uint triangleId = uint(gl_PrimitiveID) + drawInfos[gl_DrawID].triangleOffset;

    // 64-bit V-buffer: no bit packing — full 32-bit per channel
    // +1 bias so (0,0) remains the background sentinel
    outVisibility = uvec2(inInstanceId + 1u, triangleId + 1u);
}
