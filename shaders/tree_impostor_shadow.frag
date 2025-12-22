#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

layout(location = 0) in vec2 fragTexCoord;

// Impostor atlas texture for alpha testing
layout(binding = BINDING_TREE_IMPOSTOR_ALBEDO) uniform sampler2D albedoAlphaAtlas;

void main() {
    // Sample alpha from impostor atlas
    float alpha = texture(albedoAlphaAtlas, fragTexCoord).a;

    // Alpha test - discard transparent pixels
    if (alpha < 0.5) {
        discard;
    }

    // No color output needed - just depth
}
