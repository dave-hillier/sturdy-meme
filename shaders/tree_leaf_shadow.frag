#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

layout(binding = BINDING_TREE_GFX_LEAF_ALBEDO) uniform sampler2D leafAlbedo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int cascadeIndex;
    float alphaTest;
} push;

layout(location = 0) in vec2 fragTexCoord;

void main() {
    // Sample leaf texture for alpha test
    vec4 albedo = texture(leafAlbedo, fragTexCoord);

    // Discard transparent pixels
    if (albedo.a < push.alphaTest) {
        discard;
    }

    // Shadow map only needs depth (written automatically)
}
