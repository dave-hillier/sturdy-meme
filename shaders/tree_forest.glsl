// Common structures for GPU-driven forest rendering
// Shared between compute and graphics shaders

#ifndef TREE_FOREST_GLSL
#define TREE_FOREST_GLSL

// Source tree data (static, uploaded once)
struct TreeSource {
    vec4 positionScale;     // xyz = world position, w = uniform scale
    vec4 rotationArchetype; // x = Y rotation (radians), y = archetype index (0-3), z = random seed, w = unused
};

// Output for full-detail tree rendering
struct TreeFullDetail {
    vec4 positionScale;     // xyz = position, w = scale
    vec4 rotationBlend;     // x = rotation, y = blend factor (0 = full detail, 1 = fading out), zw = unused
    uint archetypeIndex;    // Which tree mesh to use
    uint treeIndex;         // Original index for leaf lookup
    vec2 _pad;
};

// Output for impostor rendering
struct TreeImpostor {
    vec4 position;          // xyz = world position, w = scale
    vec4 sizeParams;        // x = hSize, y = vSize, z = baseOffset, w = blend factor
    vec4 rotationAtlas;     // x = rotation, y = archetype index, zw = atlas UV offset
};

// Cluster data for hierarchical culling
struct ClusterData {
    vec4 centerRadius;      // xyz = center, w = bounding radius
    vec4 minBounds;         // xyz = AABB min, w = tree count
    vec4 maxBounds;         // xyz = AABB max, w = first tree index in cluster
};

// Forest uniforms
struct ForestUniforms {
    vec4 cameraPosition;        // xyz = camera pos, w = time
    vec4 frustumPlanes[6];      // Frustum planes for culling

    // LOD parameters
    float fullDetailDistance;   // Trees closer than this get full detail
    float impostorStartDistance;// Start fading to impostor
    float impostorEndDistance;  // Fully impostor beyond this
    float cullDistance;         // Cull entirely beyond this

    // Budget
    uint fullDetailBudget;      // Max full-detail trees
    uint totalTreeCount;        // Total trees in forest
    uint clusterCount;          // Number of clusters
    float clusterImpostorDist;  // Force impostor for clusters beyond this

    // Archetype bounds (for impostor sizing)
    vec4 archetypeBounds[4];    // xyz = half-extents, w = base offset
};

// Indirect draw commands
struct DrawIndirectCommand {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

// Frustum culling helper
bool isInFrustumSphere(vec4 frustumPlanes[6], vec3 center, float radius) {
    for (int i = 0; i < 6; i++) {
        vec3 normal = frustumPlanes[i].xyz;
        float d = frustumPlanes[i].w;
        if (dot(normal, center) + d < -radius) {
            return false;
        }
    }
    return true;
}

// Distance-based LOD level
// Returns: 0 = full detail, 1 = blending, 2 = impostor, 3 = culled
int calculateLODLevel(float distance, float fullDetailDist, float impostorStart, float impostorEnd, float cullDist) {
    if (distance > cullDist) return 3;
    if (distance > impostorEnd) return 2;
    if (distance > impostorStart) return 1;
    if (distance > fullDetailDist) return 2;  // Beyond full detail = impostor
    return 0;
}

// Blend factor for LOD transitions
float calculateBlendFactor(float distance, float blendStart, float blendEnd) {
    if (distance <= blendStart) return 0.0;
    if (distance >= blendEnd) return 1.0;
    return (distance - blendStart) / (blendEnd - blendStart);
}

#endif // TREE_FOREST_GLSL
