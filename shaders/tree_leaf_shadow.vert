#version 450

#extension GL_GOOGLE_include_directive : require

const int NUM_CASCADES = 4;

#include "bindings.glsl"
#include "ubo_common.glsl"

// Leaf vertex data from compute shader
struct LeafVertex {
    vec4 position;         // xyz = position, w = size
    vec4 normal;           // xyz = normal, w = unused
    vec2 uv;
    vec2 padding;
};

layout(std430, set = 0, binding = BINDING_TREE_GFX_VERTICES) readonly buffer VertexBuffer {
    LeafVertex vertices[];
};

layout(push_constant) uniform PushConstants {
    mat4 model;
    int cascadeIndex;
} push;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    LeafVertex vert = vertices[gl_VertexIndex];
    vec3 localPos = vert.position.xyz;

    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * push.model * vec4(localPos, 1.0);
    fragTexCoord = vert.uv;
}
