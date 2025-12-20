#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"

// Tree vertex data from compute shader
struct TreeVertex {
    vec4 position;         // xyz = position, w = level (for wind)
    vec4 normal;           // xyz = normal, w = unused
    vec4 tangent;          // xyz = tangent, w = sign
    vec2 uv;
    vec2 padding;
};

layout(std430, set = 0, binding = BINDING_TREE_GFX_VERTICES) readonly buffer VertexBuffer {
    TreeVertex vertices[];
};

layout(push_constant) uniform PushConstants {
    mat4 model;
    int cascadeIndex;
} push;

void main() {
    TreeVertex vert = vertices[gl_VertexIndex];
    vec3 localPos = vert.position.xyz;

    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * push.model * vec4(localPos, 1.0);
}
