#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) flat in uint fragArchetypeIndex;

// Legacy impostor atlas texture array for alpha testing
layout(binding = BINDING_TREE_IMPOSTOR_ALBEDO) uniform sampler2DArray albedoAlphaAtlas;

// Octahedral impostor atlas
layout(binding = BINDING_TREE_IMPOSTOR_OCT_ALBEDO) uniform sampler2DArray octAlbedoAlphaAtlas;

// Push constants to get octahedral mode flag
layout(push_constant) uniform PushConstants {
    vec4 cameraPos;         // unused in frag
    vec4 lodParams;         // x = useOctahedral (0 or 1)
    int cascadeIndex;       // unused in frag
} push;

void main() {
    // Check if using octahedral mapping
    bool useOctahedral = push.lodParams.x > 0.5;

    // Sample alpha from appropriate impostor atlas
    float alpha;
    if (useOctahedral) {
        alpha = texture(octAlbedoAlphaAtlas, vec3(fragTexCoord, float(fragArchetypeIndex))).a;
    } else {
        alpha = texture(albedoAlphaAtlas, vec3(fragTexCoord, float(fragArchetypeIndex))).a;
    }

    // Alpha test - discard transparent pixels
    if (alpha < 0.5) {
        discard;
    }

    // No color output needed - just depth
}
