#version 450

layout(location = 0) in float fragHeight;
layout(location = 1) in float fragHash;

void main() {
    // Render solid shadows - no dithering for now
    // Just discard the very tip for a softer look
    if (fragHeight > 0.95) {
        discard;
    }

    // Depth is written automatically
}
