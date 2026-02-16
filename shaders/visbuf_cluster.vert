#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters : require

#include "bindings.glsl"
#include "ubo_common.glsl"

// Cluster raster vertex shader
// Reads instance transforms from SSBO via gl_DrawID (indirect cluster draws)

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;     // unused but must match vertex layout
layout(location = 2) in vec2 inTexCoord;   // passed for alpha testing
layout(location = 3) in vec4 inTangent;    // unused
layout(location = 6) in vec4 inColor;      // unused

// Per-instance transform data (must match GPUSceneInstanceData in C++)
struct InstanceData {
    mat4 model;
    vec4 materialParams;
    vec4 emissiveColor;
    uint pbrFlags;
    float alphaTestThreshold;
    float hueShift;
    uint materialId;
};

layout(std430, binding = BINDING_CLUSTER_INSTANCES) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

// Per-draw cluster metadata (written by CPU or cull shader, indexed by gl_DrawID)
struct DrawClusterInfo {
    uint instanceId;
    uint triangleOffset;   // cluster.firstIndex / 3
};

layout(std430, binding = BINDING_CLUSTER_DRAW_INFO) readonly buffer DrawInfoBuffer {
    DrawClusterInfo drawInfos[];
};

layout(location = 0) flat out uint outInstanceId;
layout(location = 1) out vec2 outTexCoord;

void main() {
    DrawClusterInfo info = drawInfos[gl_DrawID];
    mat4 model = instances[info.instanceId].model;

    vec4 worldPos = model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    outInstanceId = info.instanceId;
    outTexCoord = inTexCoord;
}
