// World-space tree leaf instance data for GPU-driven single-draw rendering
// Shared between compute culling shader and vertex shader
//
// Usage in shaders: #include "tree_leaf_world.glsl"

#ifndef TREE_LEAF_WORLD_GLSL
#define TREE_LEAF_WORLD_GLSL

// Per-leaf instance data in WORLD space - 48 bytes per leaf (std430)
// After compute culling, leaves are output in world-space with tree index
struct WorldLeafInstance {
    vec4 worldPosition;      // xyz = world position, w = size
    vec4 worldOrientation;   // world-space quaternion (x, y, z, w)
    uint treeIndex;          // Index into tree data SSBO
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

// Per-tree render data - stored in SSBO, indexed by treeIndex
// Contains everything needed for rendering that was previously in push constants
struct TreeRenderData {
    mat4 model;              // Tree model matrix (for normal transform, wind pivot)
    vec4 tintAndParams;      // rgb = leaf tint, a = autumn hue shift
    vec4 windPhaseAndLOD;    // x = wind phase offset, y = LOD blend factor, zw = reserved
};

// Rotate vector by quaternion (same as tree_leaf_instance.glsl)
vec3 rotateByQuatWorld(vec3 v, vec4 q) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Quaternion multiplication: q1 * q2
vec4 quatMul(vec4 q1, vec4 q2) {
    return vec4(
        q1.w * q2.xyz + q2.w * q1.xyz + cross(q1.xyz, q2.xyz),
        q1.w * q2.w - dot(q1.xyz, q2.xyz)
    );
}

// Convert 3x3 rotation matrix to quaternion
vec4 mat3ToQuat(mat3 m) {
    float trace = m[0][0] + m[1][1] + m[2][2];
    vec4 q;

    if (trace > 0.0) {
        float s = 0.5 / sqrt(trace + 1.0);
        q.w = 0.25 / s;
        q.x = (m[2][1] - m[1][2]) * s;
        q.y = (m[0][2] - m[2][0]) * s;
        q.z = (m[1][0] - m[0][1]) * s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = 2.0 * sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]);
        q.w = (m[2][1] - m[1][2]) / s;
        q.x = 0.25 * s;
        q.y = (m[0][1] + m[1][0]) / s;
        q.z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        float s = 2.0 * sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]);
        q.w = (m[0][2] - m[2][0]) / s;
        q.x = (m[0][1] + m[1][0]) / s;
        q.y = 0.25 * s;
        q.z = (m[1][2] + m[2][1]) / s;
    } else {
        float s = 2.0 * sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]);
        q.w = (m[1][0] - m[0][1]) / s;
        q.x = (m[0][2] + m[2][0]) / s;
        q.y = (m[1][2] + m[2][1]) / s;
        q.z = 0.25 * s;
    }

    return normalize(q);
}

#endif // TREE_LEAF_WORLD_GLSL
