// Common instancing utilities for GPU-driven rendering
// Provides shared culling functions and structures for compute shaders
// that generate instance data for indirect draws.
//
// Usage: #include "instancing_common.glsl"

#ifndef INSTANCING_COMMON_GLSL
#define INSTANCING_COMMON_GLSL

// ============================================================================
// Draw Indirect Command Structure
// ============================================================================

struct DrawIndirectCommand {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

// ============================================================================
// Frustum Culling
// ============================================================================

// Check if a point is inside the frustum defined by 6 planes
// frustumPlanes: array of 6 vec4 plane equations (nx, ny, nz, d)
// pos: world-space position to test
// margin: expand the frustum by this amount (positive = more permissive)
// Returns true if the point is inside the frustum
bool isInFrustum(vec4 frustumPlanes[6], vec3 pos, float margin) {
    vec4 posW = vec4(pos, 1.0);
    for (int i = 0; i < 6; i++) {
        if (dot(posW, frustumPlanes[i]) < -margin) {
            return false;
        }
    }
    return true;
}

// Overload that takes position as vec4 (w=1 assumed)
bool isInFrustumV4(vec4 frustumPlanes[6], vec4 posW, float margin) {
    for (int i = 0; i < 6; i++) {
        if (dot(posW, frustumPlanes[i]) < -margin) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Distance Culling
// ============================================================================

// Check if a point should be culled based on distance from camera
// Returns true if the point is too far and should be culled
bool isDistanceCulled(vec3 pos, vec3 cameraPos, float maxDrawDistance) {
    return length(pos - cameraPos) > maxDrawDistance;
}

// Get distance to camera (useful when you need the value for LOD calculations)
float getDistanceToCamera(vec3 pos, vec3 cameraPos) {
    return length(pos - cameraPos);
}

// ============================================================================
// LOD (Level of Detail) Dropping
// ============================================================================

// Calculate LOD factor based on distance
// Returns 0.0 at lodStart, 1.0 at lodEnd
float calculateLodFactor(float distance, float lodStart, float lodEnd) {
    return smoothstep(lodStart, lodEnd, distance);
}

// Determine if an instance should be dropped based on LOD
// lodFactor: 0.0-1.0 from calculateLodFactor
// maxDropRate: maximum percentage of instances to drop (e.g., 0.75 = drop up to 75%)
// instanceHash: per-instance random value 0.0-1.0
// Returns true if the instance should be dropped
bool shouldLodDrop(float lodFactor, float maxDropRate, float instanceHash) {
    float dropThreshold = lodFactor * maxDropRate;
    return instanceHash < dropThreshold;
}

// Combined LOD check: returns true if instance should be dropped
bool lodCull(float distance, float lodStart, float lodEnd, float maxDropRate, float instanceHash) {
    float lodFactor = calculateLodFactor(distance, lodStart, lodEnd);
    return shouldLodDrop(lodFactor, maxDropRate, instanceHash);
}

// ============================================================================
// Hash Functions (for procedural placement and LOD randomization)
// ============================================================================

// Simple 2D hash returning float in [0, 1)
float hash2D(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Alternative 2D hash with different coefficients (for uncorrelated values)
float hash2D_alt(vec2 p) {
    return fract(sin(dot(p, vec2(269.5, 183.3))) * 43758.5453);
}

// Returns vec2 hash for a cell (useful for jittered placement)
vec2 hash2D_vec2(vec2 p) {
    return vec2(hash2D(p), hash2D(p + vec2(47.0, 13.0)));
}

// 1D hash
float hash1D(float n) {
    return fract(sin(n) * 43758.5453);
}

// 3D hash returning vec3
vec3 hash3D(vec3 p) {
    p = vec3(dot(p, vec3(127.1, 311.7, 74.7)),
             dot(p, vec3(269.5, 183.3, 246.1)),
             dot(p, vec3(113.5, 271.9, 124.6)));
    return fract(sin(p) * 43758.5453);
}

// ============================================================================
// Indirect Draw Command Helpers
// ============================================================================

// Atomically set vertex count (call once per dispatch, typically from thread 0)
// This is safe even with multiple workgroups since they all write the same value
void setVertexCount(inout DrawIndirectCommand cmd, uint vertexCount) {
    atomicMax(cmd.vertexCount, vertexCount);
}

// Atomically increment instance count and return the slot index for this instance
uint allocateInstanceSlot(inout DrawIndirectCommand cmd) {
    return atomicAdd(cmd.instanceCount, 1);
}

// Set instance count directly (use atomicMax for thread-safe single-value setting)
void setInstanceCount(inout DrawIndirectCommand cmd, uint count) {
    atomicMax(cmd.instanceCount, count);
}

#endif // INSTANCING_COMMON_GLSL
