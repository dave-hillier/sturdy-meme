#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(fragNormal);
    float diffuse = max(dot(normal, lightDir), 0.3);

    vec4 texColor = texture(texSampler, fragTexCoord);
    outColor = vec4(texColor.rgb * diffuse, texColor.a);
}
