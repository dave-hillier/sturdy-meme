// Common structures and utilities for GPU-driven tree LOD pipeline
// Include this file in tree_lod_*.comp shaders
//
// Usage: #include "tree_lod_common.glsl"

#ifndef TREE_LOD_COMMON_GLSL
#define TREE_LOD_COMMON_GLSL

// ============================================================================
// LOD Level Constants
// ============================================================================

#define TREE_LOD_FULL_DETAIL  0
#define TREE_LOD_BLENDING     1
#define TREE_LOD_IMPOSTOR     2

// ============================================================================
// GPU Structures (must match C++ TreeGPUData.h)
// ============================================================================

// Per-tree static instance data (48 bytes)
struct TreeInstanceGPU {
    vec4 positionScale;       // xyz = world position, w = uniform scale
    vec4 rotationMeshInfo;    // x = Y-axis rotation (radians), y = meshIndex, z = archetypeIndex, w = flags
    vec4 boundingInfo;        // xyz = bounding box half-extents, w = bounding sphere radius
};

// Per-tree dynamic LOD state (16 bytes)
struct TreeLODStateGPU {
    float distance;           // Distance from camera to tree
    float blendFactor;        // 0.0 = full detail, 1.0 = full impostor
    uint lodLevel;            // 0 = FullDetail, 1 = Blending, 2 = Impostor
    uint sortedIndex;         // Index in distance-sorted order
};

// Distance-index pair for sorting (8 bytes)
struct TreeDistanceKey {
    float distance;
    uint treeIndex;
};

// ============================================================================
// Helper Functions
// ============================================================================

// Extract tree position from instance data
vec3 getTreePosition(TreeInstanceGPU tree) {
    return tree.positionScale.xyz;
}

// Extract tree scale from instance data
float getTreeScale(TreeInstanceGPU tree) {
    return tree.positionScale.w;
}

// Extract mesh index from instance data
uint getTreeMeshIndex(TreeInstanceGPU tree) {
    return uint(tree.rotationMeshInfo.y);
}

// Extract archetype index from instance data
uint getTreeArchetypeIndex(TreeInstanceGPU tree) {
    return uint(tree.rotationMeshInfo.z);
}

// Extract Y-axis rotation from instance data
float getTreeRotation(TreeInstanceGPU tree) {
    return tree.rotationMeshInfo.x;
}

// Extract bounding sphere radius
float getTreeBoundingSphereRadius(TreeInstanceGPU tree) {
    return tree.boundingInfo.w;
}

// Calculate blend factor with smooth transition
float calculateBlendFactor(float distance, float fullDetailDist, float blendRange, float blendExponent) {
    if (distance < fullDetailDist) {
        return 0.0;
    }
    float blendEnd = fullDetailDist + blendRange;
    if (distance > blendEnd) {
        return 1.0;
    }
    float t = (distance - fullDetailDist) / blendRange;
    return pow(t, blendExponent);
}

// Determine LOD level from blend factor
uint getLODLevelFromBlend(float blendFactor) {
    if (blendFactor < 0.01) {
        return TREE_LOD_FULL_DETAIL;
    } else if (blendFactor > 0.99) {
        return TREE_LOD_IMPOSTOR;
    }
    return TREE_LOD_BLENDING;
}

#endif // TREE_LOD_COMMON_GLSL
