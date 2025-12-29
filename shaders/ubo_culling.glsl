// Shared culling uniform buffer definition
// Include this in compute shaders that need frustum culling, distance culling, and LOD
//
// Usage:
//   #define CULLING_UBO_BINDING <your_binding>
//   #include "ubo_culling.glsl"
//
// This provides:
//   - CullingUniforms struct with common culling fields
//   - 'culling' uniform block instance
//
// Optional fields use sentinel values:
//   - lodTransitionStart/End: set to -1.0 if LOD not used
//   - maxLodDropRate: set to 0.0 if LOD dropping not used

#ifndef UBO_CULLING_GLSL
#define UBO_CULLING_GLSL

#include "bindings.glsl"

#ifndef CULLING_UBO_BINDING
#error "CULLING_UBO_BINDING must be defined before including ubo_culling.glsl"
#endif

// Common culling uniform buffer structure
// Total size: 128 bytes (std140 aligned)
layout(std140, binding = CULLING_UBO_BINDING) uniform CullingUniforms {
    vec4 cameraPosition;           // xyz = camera pos, w = unused
    vec4 frustumPlanes[6];         // 6 frustum plane equations (nx, ny, nz, d)
    float maxDrawDistance;         // Maximum draw distance for culling
    float lodTransitionStart;      // LOD transition start distance (-1.0 if unused)
    float lodTransitionEnd;        // LOD transition end distance (-1.0 if unused)
    float maxLodDropRate;          // Max instance drop rate at far LOD (0.0 if unused)
} culling;

// ============================================================================
// Helper functions for accessing culling data
// ============================================================================

// Get camera position as vec3
vec3 getCullingCameraPosition() {
    return culling.cameraPosition.xyz;
}

// Check if LOD dropping is enabled
bool isLodEnabled() {
    return culling.lodTransitionStart >= 0.0;
}

// Calculate LOD factor (0 at start, 1 at end)
float getCullingLodFactor(float distance) {
    if (!isLodEnabled()) return 0.0;
    return smoothstep(culling.lodTransitionStart, culling.lodTransitionEnd, distance);
}

// Check if instance should be dropped based on LOD
bool shouldCullingLodDrop(float distance, float instanceHash) {
    if (!isLodEnabled()) return false;
    float lodFactor = getCullingLodFactor(distance);
    float dropThreshold = lodFactor * culling.maxLodDropRate;
    return instanceHash < dropThreshold;
}

#endif // UBO_CULLING_GLSL
