// Common instancing utilities for GPU-driven rendering
// Provides shared culling functions and structures for compute shaders
// that generate instance data for indirect draws.
//
// Usage: #include "instancing_common.glsl"

#ifndef INSTANCING_COMMON_GLSL
#define INSTANCING_COMMON_GLSL

// ============================================================================
// Draw Indirect Command Structures
// ============================================================================

// For vkCmdDrawIndirect
struct DrawIndirectCommand {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

// For vkCmdDrawIndexedIndirect - matches VkDrawIndexedIndirectCommand
struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
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

// Check if an AABB (axis-aligned bounding box) is inside the frustum
// Uses the p-vertex test: tests the vertex of the AABB that is most aligned
// with each plane's normal. If the p-vertex is behind any plane, the entire
// AABB is outside the frustum.
// frustumPlanes: array of 6 vec4 plane equations (nx, ny, nz, d)
// aabbMin: minimum corner of AABB in world space
// aabbMax: maximum corner of AABB in world space
// margin: expand the frustum by this amount (positive = more permissive)
// Returns true if the AABB is inside (or intersects) the frustum
bool isAABBInFrustum(vec4 frustumPlanes[6], vec3 aabbMin, vec3 aabbMax, float margin) {
    for (int i = 0; i < 6; i++) {
        vec4 plane = frustumPlanes[i];
        // Find the p-vertex (the vertex most in the direction of the plane normal)
        vec3 pVertex = vec3(
            plane.x > 0.0 ? aabbMax.x : aabbMin.x,
            plane.y > 0.0 ? aabbMax.y : aabbMin.y,
            plane.z > 0.0 ? aabbMax.z : aabbMin.z
        );
        // If p-vertex is behind plane (with margin), AABB is completely outside
        if (dot(vec4(pVertex, 1.0), plane) < -margin) {
            return false; // Culled
        }
    }
    return true; // Visible or intersecting
}

// Convenience overload without margin (defaults to 0.0)
bool isAABBInFrustum(vec4 frustumPlanes[6], vec3 aabbMin, vec3 aabbMax) {
    return isAABBInFrustum(frustumPlanes, aabbMin, aabbMax, 0.0);
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

// Hysteresis amount for LOD transitions (prevents flickering)
// Each instance gets a random offset of ±(LOD_HYSTERESIS/2) to its transition threshold
const float LOD_HYSTERESIS = 0.1;

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

// Determine if an instance should be dropped based on LOD with hysteresis
// lodFactor: 0.0-1.0 from calculateLodFactor
// maxDropRate: maximum percentage of instances to drop (e.g., 0.75 = drop up to 75%)
// instanceHash: per-instance random value 0.0-1.0
// hysteresisHash: second per-instance random value for staggering transitions
// Returns true if the instance should be dropped
bool shouldLodDropWithHysteresis(float lodFactor, float maxDropRate, float instanceHash, float hysteresisHash) {
    // Each instance gets a unique offset to stagger transition points
    // This prevents synchronized flickering when camera oscillates near boundaries
    float hysteresisOffset = (hysteresisHash - 0.5) * LOD_HYSTERESIS;
    float effectiveLodFactor = clamp(lodFactor + hysteresisOffset, 0.0, 1.0);
    float dropThreshold = effectiveLodFactor * maxDropRate;
    return instanceHash < dropThreshold;
}

// Combined LOD check: returns true if instance should be dropped
bool lodCull(float distance, float lodStart, float lodEnd, float maxDropRate, float instanceHash) {
    float lodFactor = calculateLodFactor(distance, lodStart, lodEnd);
    return shouldLodDrop(lodFactor, maxDropRate, instanceHash);
}

// Combined LOD check with hysteresis: returns true if instance should be dropped
bool lodCullWithHysteresis(float distance, float lodStart, float lodEnd, float maxDropRate, float instanceHash, float hysteresisHash) {
    float lodFactor = calculateLodFactor(distance, lodStart, lodEnd);
    return shouldLodDropWithHysteresis(lodFactor, maxDropRate, instanceHash, hysteresisHash);
}

// ============================================================================
// Hash Functions (for procedural placement and LOD randomization)
// ============================================================================

// Integer hash helper - based on pcg_hash
uint pcgHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Fast 2D hash using integer operations (no sin() - much faster on GPU)
float hash2D(vec2 p) {
    uvec2 ip = uvec2(floatBitsToUint(p.x), floatBitsToUint(p.y));
    uint h = pcgHash(ip.x ^ pcgHash(ip.y));
    return float(h) / 4294967295.0;
}

// Alternative 2D hash with different seed (for uncorrelated values)
float hash2D_alt(vec2 p) {
    uvec2 ip = uvec2(floatBitsToUint(p.x), floatBitsToUint(p.y));
    uint h = pcgHash(ip.x ^ pcgHash(ip.y + 0x9E3779B9u));
    return float(h) / 4294967295.0;
}

// Returns vec2 hash for a cell (useful for jittered placement)
vec2 hash2D_vec2(vec2 p) {
    uvec2 ip = uvec2(floatBitsToUint(p.x), floatBitsToUint(p.y));
    uint h1 = pcgHash(ip.x ^ pcgHash(ip.y));
    uint h2 = pcgHash(h1);
    return vec2(float(h1), float(h2)) / 4294967295.0;
}

// 1D hash
float hash1D(float n) {
    uint h = pcgHash(floatBitsToUint(n));
    return float(h) / 4294967295.0;
}

// 3D hash returning vec3
vec3 hash3D(vec3 p) {
    uvec3 ip = uvec3(floatBitsToUint(p.x), floatBitsToUint(p.y), floatBitsToUint(p.z));
    uint h1 = pcgHash(ip.x ^ pcgHash(ip.y ^ pcgHash(ip.z)));
    uint h2 = pcgHash(h1);
    uint h3 = pcgHash(h2);
    return vec3(float(h1), float(h2), float(h3)) / 4294967295.0;
}

// ============================================================================
// Indirect Draw Command Helpers
// ============================================================================
// Usage patterns for DrawIndirectCommand in compute shaders:
//
//   layout(std430, binding = X) buffer IndirectBuffer {
//       DrawIndirectCommand drawCmd;
//   };
//
//   // For constant values (vertexCount, instanceCount when known):
//   // Use a single thread to write - no atomic needed
//   if (gl_GlobalInvocationID.x == 0) {
//       drawCmd.vertexCount = 4;           // Constant value
//       drawCmd.instanceCount = totalCount; // Known count
//   }
//
//   // For dynamic instance counting (each thread adds one instance):
//   // Use atomicAdd to get unique slots
//   uint slot = atomicAdd(drawCmd.instanceCount, 1);

#endif // INSTANCING_COMMON_GLSL
