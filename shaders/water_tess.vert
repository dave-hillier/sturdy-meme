#version 450

#extension GL_GOOGLE_include_directive : require

/*
 * water_tess.vert - Vertex shader for tessellated water surface
 *
 * This is a simplified vertex shader that passes control points to the
 * tessellation control shader. The actual wave displacement is done in
 * the tessellation evaluation shader.
 */

#include "ubo_common.glsl"
#include "bindings.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    int useFFTOcean;      // 0 = Gerstner, 1 = FFT ocean
    float oceanSize0;     // FFT cascade 0 patch size
    float oceanSize1;     // FFT cascade 1 patch size
    float oceanSize2;     // FFT cascade 2 patch size
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Output to tessellation control shader
layout(location = 0) out vec3 tcsWorldPos;
layout(location = 1) out vec3 tcsNormal;
layout(location = 2) out vec2 tcsTexCoord;

void main() {
    // Transform to world space
    vec4 worldPos = push.model * vec4(inPosition, 1.0);

    // Pass through to tessellation control shader
    tcsWorldPos = worldPos.xyz;
    tcsNormal = inNormal;
    tcsTexCoord = inTexCoord;

    // Note: gl_Position is not used when tessellation is enabled
    // The tessellation evaluation shader will compute final position
}
