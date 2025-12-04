// Catmull-Clark mesh accessor functions

#include "bindings.glsl"

// Mesh data structures (matching CPU-side)
struct CCVertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
};

struct CCHalfedge {
    uint vertexID;
    uint nextID;
    uint twinID;
    uint faceID;
};

struct CCFace {
    uint halfedgeID;
    uint valence;
};

// Storage buffers (to be bound)
layout(std140, binding = BINDING_CC_VERTEX_BUFFER) readonly buffer VertexBuffer {
    CCVertex vertices[];
};

layout(std140, binding = BINDING_CC_HALFEDGE_BUFFER) readonly buffer HalfedgeBuffer {
    CCHalfedge halfedges[];
};

layout(std140, binding = BINDING_CC_FACE_BUFFER) readonly buffer FaceBuffer {
    CCFace faces[];
};

// Mesh query functions
int ccm_HalfedgeCount() {
    return halfedges.length();
}

int ccm_HalfedgeNextID(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return -1;
    }
    return int(halfedges[halfedgeID].nextID);
}

int ccm_HalfedgePrevID(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return -1;
    }
    // Walk around the face to find previous
    int current = halfedgeID;
    int next = ccm_HalfedgeNextID(current);
    while (next != halfedgeID && next >= 0) {
        current = next;
        next = ccm_HalfedgeNextID(current);
    }
    return current;
}

int ccm_HalfedgeTwinID(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return -1;
    }
    uint twinID = halfedges[halfedgeID].twinID;
    if (twinID == 0xFFFFFFFFu) {
        return -1;  // Boundary edge
    }
    return int(twinID);
}

// Get vertex position from halfedge
vec3 ccm_HalfedgeVertexPosition(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return vec3(0.0);
    }
    uint vertexID = halfedges[halfedgeID].vertexID;
    if (vertexID >= vertices.length()) {
        return vec3(0.0);
    }
    return vertices[vertexID].position;
}

// Get vertex normal from halfedge
vec3 ccm_HalfedgeVertexNormal(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return vec3(0.0, 1.0, 0.0);
    }
    uint vertexID = halfedges[halfedgeID].vertexID;
    if (vertexID >= vertices.length()) {
        return vec3(0.0, 1.0, 0.0);
    }
    return vertices[vertexID].normal;
}

// Get vertex UV from halfedge
vec2 ccm_HalfedgeVertexUV(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return vec2(0.0);
    }
    uint vertexID = halfedges[halfedgeID].vertexID;
    if (vertexID >= vertices.length()) {
        return vec2(0.0);
    }
    return vertices[vertexID].uv;
}

// Catmull-Clark settings
int ccs_MaxDepth() {
    return 16;  // Will be passed as uniform/constant
}
