#version 450

layout(location = 0) out vec2 fragTexCoord;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.0, 1.0);

    // Map from [-1,1] to [0,1] UV space
    // Vulkan Y is flipped compared to OpenGL
    fragTexCoord = pos * 0.5 + 0.5;
}
