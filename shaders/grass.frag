#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    float timeOfDay;
} ubo;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragHeight;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(fragNormal);

    // Two-sided lighting (grass blades are thin)
    float sunDot = dot(normal, ubo.sunDirection.xyz);
    float sunDiffuse = max(sunDot, 0.0) + max(-sunDot, 0.0) * 0.6;  // Backface gets some light
    vec3 sunLight = ubo.sunColor.rgb * sunDiffuse * ubo.sunDirection.w;

    float moonDot = dot(normal, ubo.moonDirection.xyz);
    float moonDiffuse = max(moonDot, 0.0) + max(-moonDot, 0.0) * 0.6;
    vec3 moonLight = vec3(0.08, 0.08, 0.12) * moonDiffuse * ubo.moonDirection.w;

    // Ambient occlusion - darker at base
    float ao = 0.5 + fragHeight * 0.5;

    // Final lighting
    vec3 lighting = (ubo.ambientColor.rgb + sunLight + moonLight) * ao;

    outColor = vec4(fragColor * lighting, 1.0);
}
