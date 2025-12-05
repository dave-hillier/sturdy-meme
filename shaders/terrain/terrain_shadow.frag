#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * terrain_shadow.frag - Terrain shadow pass fragment shader
 * Discards fragments in terrain holes, depth is written automatically
 */

#include "../bindings.glsl"

// Hole mask for caves/wells (R8: 0=solid, 1=hole)
layout(binding = BINDING_TERRAIN_HOLE_MASK) uniform sampler2D holeMask;

// Input UV from vertex shader
layout(location = 0) in vec2 fragTexCoord;

void main() {
    // Check hole mask - discard fragment if in a hole (cave/well entrance)
    float holeMaskValue = texture(holeMask, fragTexCoord).r;
    if (holeMaskValue > 0.5) {
        discard;
    }

    // Depth is written automatically for shadow mapping
    // No color output needed
}
