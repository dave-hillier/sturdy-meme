#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    float timeOfDay;
    float shadowMapSize;
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

    // Transform screen position to view-space direction
    // Use both near and far points to compute accurate ray direction
    mat4 invProj = inverse(ubo.proj);
    vec4 nearPoint = invProj * vec4(pos, 0.0, 1.0);
    vec4 farPoint = invProj * vec4(pos, 1.0, 1.0);
    vec3 nearPos = nearPoint.xyz / nearPoint.w;
    vec3 farPos = farPoint.xyz / farPoint.w;
    vec3 viewDir = normalize(farPos - nearPos);

    // Transform to world space using only rotation
    // transpose(mat3) = inverse for orthonormal rotation matrices
    mat3 invViewRot = transpose(mat3(ubo.view));
    rayDir = invViewRot * viewDir;
}
