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

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(fragNormal);

    float sunDiffuse = max(dot(normal, ubo.sunDirection.xyz), 0.0);
    vec3 sunLight = ubo.sunColor.rgb * sunDiffuse * ubo.sunDirection.w;

    float moonDiffuse = max(dot(normal, ubo.moonDirection.xyz), 0.0);
    vec3 moonLight = vec3(0.1, 0.1, 0.15) * moonDiffuse * ubo.moonDirection.w;

    vec3 lighting = ubo.ambientColor.rgb + sunLight + moonLight;

    vec4 texColor = texture(texSampler, fragTexCoord);
    outColor = vec4(texColor.rgb * lighting, texColor.a);
}
