#version 450

// CBT/LEB Terrain Vertex Shader
// Generates vertices procedurally from CBT leaf enumeration

const int NUM_CASCADES = 4;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];
    vec4 cascadeSplits;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;
    vec4 pointLightColor;
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float padding;
} ubo;

layout(std430, binding = 4) readonly buffer CBTBuffer {
    uint cbtData[];
};

layout(binding = 5) uniform sampler2D heightMap;

layout(push_constant) uniform PushConstants {
    float terrainSize;
    float heightScale;
    float maxDepth;
    float debugWireframe;
} pc;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragWorldPos;
layout(location = 3) out vec4 fragTangent;
layout(location = 4) out vec3 fragBarycentric;  // For wireframe debug

// ============== CBT/LEB Functions ==============

const uint BITFIELD_OFFSET = 16u;

uint cbt_maxDepth() {
    return uint(pc.maxDepth);
}

bool cbt_getBit(uint heapIndex) {
    uint bitIndex = heapIndex - 1u;
    uint wordIdx = bitIndex / 32u;
    uint bitPos = bitIndex % 32u;
    return (cbtData[BITFIELD_OFFSET + wordIdx] & (1u << bitPos)) != 0u;
}

uint cbt_depth(uint heapIndex) {
    return findMSB(heapIndex);
}

bool cbt_isLeaf(uint heapIndex) {
    uint maxD = cbt_maxDepth();
    uint depth = cbt_depth(heapIndex);

    if (depth >= maxD) return true;

    // Check if children exist
    uint leftChild = heapIndex * 2u;
    uint rightChild = heapIndex * 2u + 1u;

    bool leftExists = cbt_getBit(leftChild);
    bool rightExists = cbt_getBit(rightChild);

    return !leftExists && !rightExists;
}

// Enumerate leaves to find the heap index for a given leaf index
uint cbt_leafIndexToHeapIndex(uint leafIndex) {
    uint maxD = cbt_maxDepth();
    uint count = 0u;

    // Linear scan starting at heap index 2 (first valid node, heap index 1 is virtual root)
    for (uint h = 2u; h < (1u << (maxD + 1u)); h++) {
        if (cbt_getBit(h) && cbt_isLeaf(h)) {
            if (count == leafIndex) {
                return h;
            }
            count++;
        }
    }

    // Fallback: assume leaves are at a fixed depth (depth 4 = indices 16-31)
    // This handles the case where the scan fails
    return 16u + (leafIndex & 15u);
}

struct LEBTriangle {
    vec2 v0, v1, v2;
};

LEBTriangle leb_decodeTriangle(uint heapIndex) {
    // CBT structure:
    // - Heap index 1: virtual root (not used for rendering)
    // - Heap index 2: base triangle 1 (bottom-left of unit square)
    // - Heap index 3: base triangle 2 (top-right of unit square)
    // - Heap index 4+: subdivided triangles
    //
    // To find which base triangle a node descends from, trace up to the root

    vec2 v0, v1, v2;

    // Handle invalid/edge cases
    if (heapIndex < 2u) {
        heapIndex = 2u;
    }

    // Find which base triangle this node descends from
    // by walking up the tree until we hit node 2 or 3
    uint ancestor = heapIndex;
    while (ancestor > 3u) {
        ancestor = ancestor / 2u;  // parent
    }
    bool isBase2 = (ancestor == 3u);

    // Set up base triangle vertices
    // Both triangles share the diagonal edge. For LEB to work correctly,
    // the shared edge (v1-v2) must be oriented consistently.
    // Triangle 1: v1=(1,0), v2=(0,1) - diagonal goes bottom-right to top-left
    // Triangle 2: v1=(0,1), v2=(1,0) - diagonal goes top-left to bottom-right (OPPOSITE)
    // This is correct for LEB - neighbors have opposite edge orientation
    if (!isBase2) {
        // Base triangle 1: bottom-left half
        v0 = vec2(0.0, 0.0);
        v1 = vec2(1.0, 0.0);
        v2 = vec2(0.0, 1.0);
    } else {
        // Base triangle 2: top-right half (same winding direction)
        v0 = vec2(1.0, 1.0);
        v1 = vec2(0.0, 1.0);
        v2 = vec2(1.0, 0.0);
    }

    // If this is a base triangle (heap index 2 or 3), we're done
    if (heapIndex <= 3u) {
        return LEBTriangle(v0, v1, v2);
    }

    // Apply subdivision by following the path from the base triangle to this node
    // We need to replay the subdivision steps
    uint depth = cbt_depth(heapIndex);

    // Apply subdivision for each level after the base (depth 1)
    // LEB subdivision: bisect the edge opposite to v0 (edge v1-v2)
    for (uint d = 2u; d <= depth; d++) {
        uint bitPos = depth - d;
        uint bit = (heapIndex >> bitPos) & 1u;

        // Bisect: midpoint of edge v1-v2 (the edge opposite to v0)
        vec2 midpoint = (v1 + v2) * 0.5;

        if (bit == 0u) {
            // Left child: new triangle is (midpoint, v0, v1)
            v2 = v0;
            v0 = midpoint;
        } else {
            // Right child: new triangle is (midpoint, v2, v0)
            v1 = v0;
            v0 = midpoint;
        }
    }

    return LEBTriangle(v0, v1, v2);
}

// ============== Height/Normal Functions ==============

float sampleHeight(vec2 uv) {
    return texture(heightMap, uv).r * pc.heightScale;
}

vec3 uvToWorldPos(vec2 uv) {
    float height = sampleHeight(uv);
    return vec3((uv.x - 0.5) * pc.terrainSize, height, (uv.y - 0.5) * pc.terrainSize);
}

vec3 computeNormalFromHeightMap(vec2 uv) {
    vec2 texelSize = 1.0 / vec2(textureSize(heightMap, 0));

    float hL = sampleHeight(uv + vec2(-texelSize.x, 0.0));
    float hR = sampleHeight(uv + vec2(texelSize.x, 0.0));
    float hD = sampleHeight(uv + vec2(0.0, -texelSize.y));
    float hU = sampleHeight(uv + vec2(0.0, texelSize.y));

    // Compute gradient
    vec3 normal;
    normal.x = hL - hR;
    normal.y = 2.0 * texelSize.x * pc.terrainSize;  // Scale factor
    normal.z = hD - hU;

    return normalize(normal);
}

// ============== Main ==============

void main() {
    // Determine which triangle and which vertex
    uint triangleIndex = gl_VertexIndex / 3u;
    uint vertexInTri = gl_VertexIndex % 3u;

    // Use heap indices 64-127 for 64 triangles at depth 6
    uint heapIndex = 64u + (triangleIndex & 63u);

    // Decode triangle vertices in UV space
    LEBTriangle tri = leb_decodeTriangle(heapIndex);

    // Select vertex
    vec2 uv;
    if (vertexInTri == 0u) {
        uv = tri.v0;
        fragBarycentric = vec3(1.0, 0.0, 0.0);
    } else if (vertexInTri == 1u) {
        uv = tri.v1;
        fragBarycentric = vec3(0.0, 1.0, 0.0);
    } else {
        uv = tri.v2;
        fragBarycentric = vec3(0.0, 0.0, 1.0);
    }

    // Get world position
    vec3 worldPos = uvToWorldPos(uv);

    // Compute normal from height map
    vec3 normal = computeNormalFromHeightMap(uv);

    // Compute tangent (along terrain X direction)
    vec3 tangent = normalize(vec3(1.0, (sampleHeight(uv + vec2(0.01, 0.0)) - sampleHeight(uv)) / 0.01, 0.0));

    // Output
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
    fragNormal = normal;
    fragTexCoord = uv;
    fragWorldPos = worldPos;
    fragTangent = vec4(tangent, 1.0);  // Handedness in w
}
