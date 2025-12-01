#version 450

// Shadow pass fragment shader for tree leaves
// Uses dithered alpha for soft canopy shadows

layout(location = 0) in vec2 fragUV;
layout(location = 1) in float fragHash;

// 4x4 Bayer dithering matrix for soft shadows
const float bayerMatrix[16] = float[16](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0, 4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0, 7.0/16.0, 13.0/16.0,  5.0/16.0
);

float bayerDither(vec2 screenPos) {
    ivec2 index = ivec2(mod(screenPos, 4.0));
    return bayerMatrix[index.y * 4 + index.x];
}

void main() {
    // Create ellipse leaf shape
    vec2 centered = fragUV * 2.0 - 1.0;
    float leafShape = 1.0 - length(centered * vec2(1.0, 0.7));
    leafShape = smoothstep(0.0, 0.3, leafShape);

    // Discard pixels outside leaf shape
    if (leafShape < 0.1) {
        discard;
    }

    // Dithered alpha for soft shadow edges
    // This creates a semi-transparent shadow effect in the shadow map
    float dither = bayerDither(gl_FragCoord.xy);

    // Soften shadow based on leaf shape edge and per-leaf hash
    float shadowAlpha = leafShape * 0.8 + fragHash * 0.2;

    // Dither test for soft shadows
    if (dither > shadowAlpha) {
        discard;
    }

    // Depth is written automatically
}
