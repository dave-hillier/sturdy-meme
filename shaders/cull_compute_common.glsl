// Common culling compute shader utilities
// Provides shared frustum culling, distance culling, and subgroup-batched atomic patterns
// for GPU-driven culling passes (tree cells, tree filtering, shadow culling, etc.)
//
// Usage: #include "cull_compute_common.glsl"
//
// Requires extensions:
//   GL_KHR_shader_subgroup_ballot
//   GL_KHR_shader_subgroup_arithmetic

#ifndef CULL_COMPUTE_COMMON_GLSL
#define CULL_COMPUTE_COMMON_GLSL

// ============================================================================
// AABB Frustum Culling
// ============================================================================

// Check if an AABB is at least partially inside the frustum
// boundsMin, boundsMax: AABB extents in world space
// planes: array of 6 frustum plane equations (nx, ny, nz, d)
// Returns true if the AABB is at least partially inside the frustum
bool isAABBInFrustum(vec3 boundsMin, vec3 boundsMax, vec4 planes[6]) {
    // For each frustum plane, find the vertex of the AABB that is most
    // in the direction of the plane normal. If this vertex is behind the plane,
    // the AABB is completely outside the frustum.
    for (int i = 0; i < 6; i++) {
        vec3 normal = planes[i].xyz;

        // Find the positive vertex (vertex most in direction of normal)
        vec3 positiveVertex;
        positiveVertex.x = (normal.x >= 0.0) ? boundsMax.x : boundsMin.x;
        positiveVertex.y = (normal.y >= 0.0) ? boundsMax.y : boundsMin.y;
        positiveVertex.z = (normal.z >= 0.0) ? boundsMax.z : boundsMin.z;

        // Test if positive vertex is behind the plane
        if (dot(vec4(positiveVertex, 1.0), planes[i]) < 0.0) {
            return false;  // Completely outside this plane
        }
    }
    return true;  // At least partially inside all planes
}

// ============================================================================
// Sphere Frustum Culling
// ============================================================================

// Check if a bounding sphere is at least partially inside the frustum
// center: sphere center in world space
// radius: sphere radius
// planes: array of 6 frustum plane equations (nx, ny, nz, d)
// Returns true if the sphere is at least partially inside the frustum
bool isSphereInFrustum(vec3 center, float radius, vec4 planes[6]) {
    for (int i = 0; i < 6; i++) {
        float dist = dot(planes[i].xyz, center) + planes[i].w;
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// AABB Distance Culling
// ============================================================================

// Check if an AABB's closest point is within max distance from camera
// boundsMin, boundsMax: AABB extents in world space
// cameraPos: camera position in world space
// maxDistance: maximum draw distance
// Returns true if the AABB is within range
bool isAABBInRange(vec3 boundsMin, vec3 boundsMax, vec3 cameraPos, float maxDistance) {
    // Find closest point on AABB to camera
    vec3 closestPoint;
    closestPoint.x = clamp(cameraPos.x, boundsMin.x, boundsMax.x);
    closestPoint.y = clamp(cameraPos.y, boundsMin.y, boundsMax.y);
    closestPoint.z = clamp(cameraPos.z, boundsMin.z, boundsMax.z);

    // Check if closest point is within range
    float dist = length(closestPoint - cameraPos);
    return dist <= maxDistance;
}

// ============================================================================
// Distance Bucketing
// ============================================================================

// Number of distance buckets for prioritized processing
const uint NUM_DISTANCE_BUCKETS = 8;

// Calculate which distance bucket a cell belongs to based on distance
// Bucket boundaries aligned with typical cell sizes:
// 0-64m, 64-128m, 128-192m, 192-256m, 256-320m, 320-400m, 400-500m, 500m+
uint getDistanceBucket(float dist) {
    if (dist < 64.0) return 0;
    if (dist < 128.0) return 1;
    if (dist < 192.0) return 2;
    if (dist < 256.0) return 3;
    if (dist < 320.0) return 4;
    if (dist < 400.0) return 5;
    if (dist < 500.0) return 6;
    return 7;
}

// Encode cell index with distance bucket (high 3 bits = bucket, low 29 bits = index)
uint encodeCellWithBucket(uint cellIndex, uint bucket) {
    return (bucket << 29) | (cellIndex & 0x1FFFFFFF);
}

// Decode cell index from encoded value
uint decodeCellIndex(uint encoded) {
    return encoded & 0x1FFFFFFF;
}

// Decode bucket from encoded value
uint decodeBucket(uint encoded) {
    return encoded >> 29;
}

// ============================================================================
// Subgroup Batched Atomic Pattern (Documentation)
// ============================================================================
//
// Use subgroup operations to batch atomic updates, reducing contention.
// This pattern cannot be abstracted into a function because GLSL atomics
// only work on buffer/shared memory l-values, not function parameters.
//
// Basic pattern for allocating slots from a counter:
//
//   uvec4 activeMask = subgroupBallot(true);
//   uint activeCount = subgroupBallotBitCount(activeMask);
//   uint laneOffset = subgroupBallotExclusiveBitCount(activeMask);
//
//   uint baseIdx = 0;
//   if (subgroupElect()) {
//       baseIdx = atomicAdd(myCounter, activeCount);
//   }
//   baseIdx = subgroupBroadcastFirst(baseIdx);
//   uint slot = baseIdx + laneOffset;
//
// Per-category batching (for bucketing by type/group):
//
//   for (uint c = 0; c < NUM_CATEGORIES; c++) {
//       uvec4 categoryMask = subgroupBallot(myCategory == c);
//       uint categoryCount = subgroupBallotBitCount(categoryMask);
//       if (categoryCount > 0) {
//           uint electedLane = subgroupBallotFindLSB(categoryMask);
//           uint baseSlot = 0;
//           if (gl_SubgroupInvocationID == electedLane) {
//               baseSlot = atomicAdd(counters[c], categoryCount);
//           }
//           baseSlot = subgroupBroadcast(baseSlot, electedLane);
//           if (myCategory == c) {
//               slot = baseSlot + subgroupBallotExclusiveBitCount(categoryMask);
//           }
//       }
//   }

#endif // CULL_COMPUTE_COMMON_GLSL
