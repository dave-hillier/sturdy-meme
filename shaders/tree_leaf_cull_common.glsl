// Common leaf culling functions and constants shared between:
// - tree_leaf_cull.comp (global dispatch, one thread per leaf)
// - tree_leaf_cull_phase3.comp (per-tree dispatch, one workgroup per visible tree)

#ifndef TREE_LEAF_CULL_COMMON_GLSL
#define TREE_LEAF_CULL_COMMON_GLSL

// Number of leaf types (oak=0, ash=1, aspen=2, pine=3)
const uint NUM_LEAF_TYPES = 4;

// Per-tree culling data structure (shared layout)
struct TreeCullData {
    mat4 treeModel;                // Tree's model matrix
    uint inputFirstInstance;       // Offset into inputInstances for this tree
    uint inputInstanceCount;       // Number of input instances for this tree
    uint treeIndex;                // Index of this tree (for render data lookup)
    uint leafTypeIndex;            // Leaf type (0=oak, 1=ash, 2=aspen, 3=pine)
    float lodBlendFactor;          // LOD blend factor (0=full detail, 1=full impostor)
    uint _pad0;                    // Padding for std430 alignment
    uint _pad1;
    uint _pad2;
};

// Hash a leaf index to get a stable per-leaf random value
// Uses integer hashing for stability (unlike float position which has precision issues)
float hashLeafIndex(uint treeIndex, uint leafIndexInTree) {
    // Combine tree index and leaf index for unique hash per leaf
    uint h = pcgHash(treeIndex ^ pcgHash(leafIndexInTree));
    return float(h) / 4294967295.0;
}

// Cull a single leaf instance against frustum, distance, and LOD
// Returns true if the leaf should be CULLED (not rendered)
// leafIndexInTree: the index of this leaf within its tree (for stable hashing)
// treeIndex: the tree's index (combined with leaf index for unique hash)
bool cullLeaf(
    vec3 leafLocalPos,
    float leafSizeLocal,
    mat4 treeModel,
    float lodBlendFactor,
    vec3 cameraPos,
    vec4 frustumPlanes[6],
    float maxDrawDistance,
    uint leafIndexInTree,
    uint treeIndex,
    out vec4 worldPos,
    out float leafSize,
    out float distToCamera
) {
    // Transform leaf position to world space
    worldPos = treeModel * vec4(leafLocalPos, 1.0);

    // Extract uniform scale from model matrix (assumes uniform scaling)
    float treeScale = length(treeModel[0].xyz);
    leafSize = leafSizeLocal * treeScale;

    // Distance culling
    distToCamera = getDistanceToCamera(worldPos.xyz, cameraPos);
    if (distToCamera > maxDrawDistance) {
        return true;
    }

    // NOTE: We intentionally skip frustum culling for leaves.
    // Unlike branches which have separate shadow culling against the light frustum,
    // leaves use the same culled buffer for both main rendering and shadows.
    // Frustum culling against the camera would remove leaves that are behind the
    // camera but still need to cast shadows into the view.
    // Distance and LOD culling already limit the leaf count sufficiently.

    // LOD-based leaf dropping using lodBlendFactor (from screen-space error system)
    // lodBlendFactor: 0 = full detail (close/large), 1 = full impostor (far/small)
    // Use stable integer-based hash to ensure same leaves dropped each frame.
    // Drop rate scales with lodBlendFactor: at 0 drop nothing, at 1 drop 90%
    float instanceHash = hashLeafIndex(treeIndex, leafIndexInTree);
    float maxDropRate = 0.9;  // Drop up to 90% of leaves as we approach impostor
    float dropThreshold = lodBlendFactor * maxDropRate;
    if (instanceHash < dropThreshold) {
        return true;
    }

    return false;
}

// Transform leaf orientation to world space
vec4 transformLeafOrientation(vec4 localOrientation, mat4 treeModel) {
    mat3 rotationMatrix = mat3(treeModel);
    vec4 treeRotQuat = mat3ToQuat(rotationMatrix);
    return quatMul(treeRotQuat, localOrientation);
}

#endif // TREE_LEAF_CULL_COMMON_GLSL
