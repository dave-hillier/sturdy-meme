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

    // Linear scan - works but O(n). For production, use sum reduction tree
    for (uint h = 1u; h < (1u << (maxD + 1u)); h++) {
        if (cbt_getBit(h) && cbt_isLeaf(h)) {
            if (count == leafIndex) {
                return h;
            }
            count++;
        }
    }

    // Fallback to base triangles
    return leafIndex < 1u ? 1u : 2u;
}

struct LEBTriangle {
    vec2 v0, v1, v2;
};

LEBTriangle leb_decodeTriangle(uint heapIndex) {
    uint depth = cbt_depth(heapIndex);

    vec2 v0, v1, v2;

    if (heapIndex == 0u) {
        heapIndex = 1u;
        depth = 1u;
    }

    // Determine base triangle based on path from root
    // Base triangles cover the unit square [0,1]^2
    if (heapIndex == 1u) {
        // First base triangle: bottom-left
        v0 = vec2(0.0, 0.0);
        v1 = vec2(1.0, 0.0);
        v2 = vec2(0.0, 1.0);
    } else if (heapIndex == 2u) {
        // Second base triangle: top-right
        v0 = vec2(1.0, 1.0);
        v1 = vec2(0.0, 1.0);
        v2 = vec2(1.0, 0.0);
    } else {
        // Start from base and subdivide
        uint topBit = 1u << (depth - 1u);
        bool isSecondBase = (heapIndex & topBit) != 0u;

        if (!isSecondBase) {
            v0 = vec2(0.0, 0.0);
            v1 = vec2(1.0, 0.0);
            v2 = vec2(0.0, 1.0);
        } else {
            v0 = vec2(1.0, 1.0);
            v1 = vec2(0.0, 1.0);
            v2 = vec2(1.0, 0.0);
        }

        // Apply subdivision for each level
        for (uint d = 1u; d < depth; d++) {
            uint bitPos = depth - 1u - d;
            uint bit = (heapIndex >> bitPos) & 1u;

            // Bisect: midpoint of edge v1-v2
            vec2 midpoint = (v1 + v2) * 0.5;

            if (bit == 0u) {
                // Left child
                v2 = v1;
                v1 = midpoint;
            } else {
                // Right child
                v1 = v2;
                v2 = midpoint;
            }

            // Rotate vertices
            vec2 temp = v0;
            v0 = v1;
            v1 = v2;
            v2 = temp;
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

    // Get heap index for this leaf
    uint heapIndex = cbt_leafIndexToHeapIndex(triangleIndex);

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
