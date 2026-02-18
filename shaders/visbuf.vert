#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters : require

#include "bindings.glsl"
#include "ubo_common.glsl"

// Visibility buffer vertex shader (GPU-driven indirect draws)
// Uses gl_DrawID to index into per-draw data buffer written by cluster_cull.comp

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;     // unused but must match vertex layout
layout(location = 2) in vec2 inTexCoord;   // passed for alpha testing
layout(location = 3) in vec4 inTangent;    // unused
layout(location = 6) in vec4 inColor;      // unused

// Per-draw data from cluster culling (parallel to indirect commands)
struct DrawData {
    uint instanceId;
    uint triangleOffset;
};

layout(std430, binding = BINDING_VISBUF_RASTER_DRAW_DATA) readonly buffer DrawDataBuffer {
    DrawData drawData[];
};

// Instance transforms from GPUSceneBuffer
struct InstanceTransform {
    mat4 model;
    vec4 materialParams;
    vec4 emissiveColor;
    uint pbrFlags;
    float alphaTestThreshold;
    float hueShift;
    float _pad;
};

layout(std430, binding = BINDING_VISBUF_RASTER_INSTANCES) readonly buffer InstanceBuffer {
    InstanceTransform instances[];
};

layout(location = 0) flat out uint outInstanceId;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) flat out uint outTriangleOffset;

void main() {
    DrawData dd = drawData[gl_DrawIDARB];
    mat4 model = instances[dd.instanceId].model;

    vec4 worldPos = model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    outInstanceId = dd.instanceId;
    outTexCoord = inTexCoord;
    outTriangleOffset = dd.triangleOffset;
}
