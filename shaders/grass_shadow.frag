#version 450

#extension GL_GOOGLE_include_directive : require

#include "grass_constants.glsl"

layout(location = 0) in float fragHeight;
layout(location = 1) in float fragHash;

void main() {
    // Render solid shadows - no dithering for now
    // Just discard the very tip for a softer look using unified constant
    if (fragHeight > GRASS_SHADOW_TIP_THRESHOLD) {
        discard;
    }

    // Depth is written automatically
}
