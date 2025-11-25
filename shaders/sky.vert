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

layout(location = 0) out vec3 rayDir;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.9999, 1.0);

    mat4 invProj = inverse(ubo.proj);
    mat4 invView = inverse(mat4(mat3(ubo.view)));

    vec4 clipPos = vec4(pos, 1.0, 1.0);
    vec4 viewPos = invProj * clipPos;
    viewPos.w = 0.0;
    vec4 worldDir = invView * viewPos;
    rayDir = normalize(worldDir.xyz);
}
