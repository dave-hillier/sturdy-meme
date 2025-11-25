#version 450

layout(location = 0) in float fragHeight;
layout(location = 1) in float fragHash;

// 4x4 Bayer dithering matrix for soft shadow edges
// Values range from 0/16 to 15/16
const float bayerMatrix[16] = float[](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
   12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
   15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
);

void main() {
    // Get Bayer threshold from screen position
    ivec2 pixel = ivec2(gl_FragCoord.xy) % 4;
    float threshold = bayerMatrix[pixel.y * 4 + pixel.x];

    // Dithered alpha based on:
    // 1. Height along blade (tips are more transparent for softer shadows)
    // 2. Per-blade hash (adds variation to shadow density)
    float shadowDensity = mix(0.8, 0.3, fragHeight);  // Base 80% at bottom, 30% at tip
    shadowDensity *= mix(0.7, 1.0, fragHash);         // Vary by blade (70-100%)

    // Discard fragments below threshold for soft shadow effect
    if (shadowDensity < threshold) {
        discard;
    }

    // Depth is written automatically
}
