#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

// Catmull-Clark subdivision surface rendering - Vertex Shader

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;

layout(binding = BINDING_CC_SCENE_UBO) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    vec4 frustumPlanes[6];
} scene;

layout(std140, binding = BINDING_CC_CBT_BUFFER) readonly buffer CBTBuffer {
    uint cbtData[];
};

#include "cbt_common.glsl"
#include "catmullclark_mesh.glsl"
#include "catmullclark_tessellation.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

void main() {
    // For now, simple pass-through rendering of base mesh vertices
    // Full subdivision rendering will be implemented later

    uint vertexID = gl_VertexIndex;

    // Simple rendering: just draw the base mesh
    if (vertexID < vertices.length()) {
        vec3 position = vertices[vertexID].position;
        vec3 normal = vertices[vertexID].normal;
        vec2 uv = vertices[vertexID].uv;

        vec4 worldPos = push.model * vec4(position, 1.0);
        fragWorldPos = worldPos.xyz;
        fragNormal = mat3(push.model) * normal;
        fragUV = uv;

        gl_Position = scene.proj * scene.view * worldPos;
    } else {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        fragWorldPos = vec3(0.0);
        fragNormal = vec3(0.0, 1.0, 0.0);
        fragUV = vec2(0.0);
    }
}
