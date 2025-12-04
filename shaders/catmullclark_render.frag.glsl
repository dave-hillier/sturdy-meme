#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

// Catmull-Clark subdivision surface rendering - Fragment Shader

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(binding = BINDING_CC_SCENE_UBO) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    vec4 frustumPlanes[6];
} scene;

void main() {
    // Simple shading for now
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

    // Basic diffuse lighting
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * vec3(0.8, 0.6, 0.4);

    // Ambient
    vec3 ambient = vec3(0.2, 0.2, 0.25);

    vec3 color = ambient + diffuse;

    outColor = vec4(color, 1.0);
}
