#version 450

// Loading screen vertex shader - renders a rotating quad

layout(push_constant) uniform PushConstants {
    float time;      // Time in seconds
    float aspect;    // Aspect ratio (width / height)
    float progress;  // Loading progress [0, 1] (optional)
    float _pad;
} pc;

// Quad vertices (2 triangles, 6 vertices)
vec2 positions[6] = vec2[](
    vec2(-0.15, -0.15), vec2( 0.15, -0.15), vec2( 0.15,  0.15),
    vec2(-0.15, -0.15), vec2( 0.15,  0.15), vec2(-0.15,  0.15)
);

layout(location = 0) out vec2 fragTexCoord;

void main() {
    vec2 pos = positions[gl_VertexIndex];

    // Apply rotation
    float angle = pc.time * 1.5;  // Rotate 1.5 rad/sec
    float c = cos(angle);
    float s = sin(angle);
    mat2 rotation = mat2(c, s, -s, c);
    pos = rotation * pos;

    // Correct for aspect ratio
    pos.x /= pc.aspect;

    gl_Position = vec4(pos, 0.0, 1.0);

    // UV coordinates for texturing
    fragTexCoord = positions[gl_VertexIndex] + 0.5;
}
