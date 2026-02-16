#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

// Visibility buffer fragment shader
// Writes (instanceID, triangleID) into a 64-bit (R32G32_UINT) render target
//
// Output format:
//   R (uint32) = instanceId + 1  (full 32-bit range)
//   G (uint32) = triangleId + 1  (full 32-bit range)
//
// +1 bias: (0, 0) is reserved as the background sentinel (no geometry).
// triangleId = gl_PrimitiveID + triangleOffset

layout(location = 0) flat in uint inInstanceId;
layout(location = 1) in vec2 inTexCoord;

layout(push_constant) uniform VisBufPushConstants {
    mat4 model;
    uint instanceId;
    uint triangleOffset;
    float alphaTestThreshold;
    float _pad;
} visbuf;

// Optional: diffuse texture for alpha testing (transparent objects)
layout(binding = BINDING_DIFFUSE_TEX) uniform sampler2D diffuseTexture;

layout(location = 0) out uvec2 outVisibility;

void main() {
    // Alpha test (for foliage/transparent objects)
    if (visbuf.alphaTestThreshold > 0.0) {
        float alpha = texture(diffuseTexture, inTexCoord).a;
        if (alpha < visbuf.alphaTestThreshold) {
            discard;
        }
    }

    uint triangleId = uint(gl_PrimitiveID) + visbuf.triangleOffset;

    // 64-bit V-buffer: no bit packing — full 32-bit per channel
    // +1 bias so (0,0) remains the background sentinel
    outVisibility = uvec2(inInstanceId + 1u, triangleId + 1u);
}
