#version 450

#extension GL_GOOGLE_include_directive : require

#include "bindings.glsl"

// Visibility buffer debug visualization fragment shader
// Colors pixels by instance ID and triangle ID for verification
// Reads R32G32_UINT V-buffer: R=instanceId+1, G=triangleId+1 (0=background)

layout(binding = BINDING_VISBUF_DEBUG_INPUT) uniform usampler2D visibilityBuffer;
layout(binding = BINDING_VISBUF_DEBUG_DEPTH_INPUT) uniform sampler2D depthBuffer;

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

// Hash function for consistent coloring
vec3 hashColor(uint id) {
    // PCG hash for good distribution
    uint state = id * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    uint hash = (word >> 22u) ^ word;

    float r = float((hash >> 0u) & 0xFFu) / 255.0;
    float g = float((hash >> 8u) & 0xFFu) / 255.0;
    float b = float((hash >> 16u) & 0xFFu) / 255.0;

    // Boost saturation for better visibility
    return mix(vec3(0.5), vec3(r, g, b), 0.8) * 0.8 + 0.2;
}

// LOD level color ramp (blue=LOD0 -> green=LOD1 -> yellow=LOD2 -> red=LOD3+)
vec3 lodColor(uint lodLevel) {
    if (lodLevel == 0u) return vec3(0.2, 0.4, 1.0);   // Blue - highest detail
    if (lodLevel == 1u) return vec3(0.2, 1.0, 0.4);   // Green
    if (lodLevel == 2u) return vec3(1.0, 1.0, 0.2);   // Yellow
    return vec3(1.0, 0.3, 0.2);                        // Red - coarsest
}

layout(push_constant) uniform DebugPushConstants {
    uint mode;  // 0=instance, 1=triangle, 2=mixed, 3=cluster, 4=cluster+instance, 5=depth
    float _pad0, _pad1, _pad2;
} debugParams;

void main() {
    uvec4 packed = texture(visibilityBuffer, inTexCoord);

    // (0, 0) means no geometry was written (background)
    if (packed.r == 0u && packed.g == 0u) {
        if (debugParams.mode == 5u) {
            // Depth mode: show depth buffer for background
            float depth = texture(depthBuffer, inTexCoord).r;
            outColor = vec4(vec3(depth), 1.0);
        } else {
            outColor = vec4(0.05, 0.05, 0.1, 1.0);
        }
        return;
    }

    // 64-bit V-buffer: separate channels, -1 to undo bias
    uint instanceId = packed.r - 1u;
    uint triangleId = packed.g - 1u;

    // Approximate cluster ID from triangle ID (clusters are ~64 triangles)
    uint clusterId = triangleId / 64u;

    if (debugParams.mode == 0u) {
        // Color by instance ID
        outColor = vec4(hashColor(instanceId), 1.0);
    } else if (debugParams.mode == 1u) {
        // Color by triangle ID
        outColor = vec4(hashColor(triangleId), 1.0);
    } else if (debugParams.mode == 2u) {
        // Mixed: blend instance and triangle coloring
        vec3 instColor = hashColor(instanceId);
        vec3 triColor = hashColor(triangleId);
        outColor = vec4(mix(instColor, triColor, 0.5), 1.0);
    } else if (debugParams.mode == 3u) {
        // Cluster coloring: hash of approximate cluster ID
        // Each cluster gets a unique color, making cluster boundaries visible
        outColor = vec4(hashColor(clusterId), 1.0);
    } else if (debugParams.mode == 4u) {
        // Cluster + instance: combine instance tint with cluster pattern
        // This shows both which object and which cluster within that object
        vec3 instColor = hashColor(instanceId);
        vec3 clusterColor = hashColor(clusterId);
        outColor = vec4(mix(instColor, clusterColor, 0.6), 1.0);
    } else if (debugParams.mode == 5u) {
        // Depth visualization (linearized)
        float depth = texture(depthBuffer, inTexCoord).r;
        // Simple linearization for visualization
        float linearDepth = 1.0 / (1.0 - depth + 0.001);
        float normalized = clamp(linearDepth / 100.0, 0.0, 1.0);
        outColor = vec4(vec3(1.0 - normalized), 1.0);
    } else {
        outColor = vec4(hashColor(instanceId ^ triangleId), 1.0);
    }
}
