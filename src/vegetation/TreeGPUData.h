#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// GPU structures for GPU-driven tree LOD pipeline
// These structures are designed to match GLSL std430 layout

// Per-tree static instance data for LOD pipeline (uploaded when trees are added/removed)
// Must match TreeInstanceGPU in tree_lod_common.glsl
// NOTE: This is separate from the existing TreeInstanceGPU in TreeOptions.h
struct TreeLODInstanceGPU {
    glm::vec4 positionScale;      // xyz = world position, w = uniform scale
    glm::vec4 rotationMeshInfo;   // x = Y-axis rotation (radians), y = meshIndex, z = archetypeIndex, w = flags
    glm::vec4 boundingInfo;       // xyz = bounding box half-extents, w = bounding sphere radius
};
static_assert(sizeof(TreeLODInstanceGPU) == 48, "TreeLODInstanceGPU must be 48 bytes for GPU alignment");

// Per-tree dynamic LOD state (computed on GPU each frame)
// Must match TreeLODStateGPU in tree_lod_common.glsl
struct TreeLODStateGPU {
    float distance;               // Distance from camera to tree
    float blendFactor;            // 0.0 = full detail, 1.0 = full impostor
    uint32_t lodLevel;            // 0 = FullDetail, 1 = Blending, 2 = Impostor
    uint32_t sortedIndex;         // Index in distance-sorted order (for budget checking)
};
static_assert(sizeof(TreeLODStateGPU) == 16, "TreeLODStateGPU must be 16 bytes for GPU alignment");

// Distance-index pair for GPU sorting
// Must match TreeDistanceKey in tree_lod_common.glsl
struct TreeDistanceKey {
    float distance;
    uint32_t treeIndex;
};
static_assert(sizeof(TreeDistanceKey) == 8, "TreeDistanceKey must be 8 bytes for GPU alignment");

// GPU LOD uniforms (uploaded each frame)
// Must match TreeLODUniforms in tree_lod_common.glsl
struct TreeLODUniformsGPU {
    glm::vec4 cameraPosition;     // xyz = camera pos, w = unused
    glm::vec4 frustumPlanes[6];   // Frustum planes for culling (optional future use)

    uint32_t numTrees;            // Total number of trees
    uint32_t fullDetailBudget;    // Max trees at full detail (e.g., 75)
    float fullDetailDistance;     // Base distance for full detail
    float maxFullDetailDistance;  // Hard cap distance even for budgeted trees

    float blendRange;             // Distance over which to blend LODs
    float hysteresis;             // Dead zone for LOD transitions
    float _pad0;
    float _pad1;
};
static_assert(sizeof(TreeLODUniformsGPU) == 144, "TreeLODUniformsGPU must be 144 bytes (std140 layout)");

// Atomic counters for draw command generation
struct TreeDrawCounters {
    uint32_t fullDetailCount;     // Trees needing full detail rendering
    uint32_t impostorCount;       // Trees needing impostor rendering
    uint32_t blendingCount;       // Trees in blending state (rendered as both)
    uint32_t _pad;
};
static_assert(sizeof(TreeDrawCounters) == 16, "TreeDrawCounters must be 16 bytes");

// LOD level constants (match shader)
namespace TreeLODLevel {
    constexpr uint32_t FullDetail = 0;
    constexpr uint32_t Blending = 1;
    constexpr uint32_t Impostor = 2;
}

// Tree instance flags (stored in rotationMeshInfo.w)
namespace TreeInstanceFlags {
    constexpr uint32_t None = 0;
    constexpr uint32_t Selected = 1 << 0;      // Tree is selected in editor
    constexpr uint32_t ForceLOD = 1 << 1;      // Force specific LOD level
    constexpr uint32_t NoShadow = 1 << 2;      // Skip shadow rendering
}
