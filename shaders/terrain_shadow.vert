#version 450

// CBT/LEB Terrain Shadow Vertex Shader
// Simplified version for shadow map rendering

layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
    float terrainSize;
    float heightScale;
    float maxDepth;
    int cascadeIndex;
} pc;

layout(std430, binding = 0) readonly buffer CBTBuffer {
    uint cbtData[];
};

layout(binding = 1) uniform sampler2D heightMap;

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

    uint leftChild = heapIndex * 2u;
    uint rightChild = heapIndex * 2u + 1u;

    bool leftExists = cbt_getBit(leftChild);
    bool rightExists = cbt_getBit(rightChild);

    return !leftExists && !rightExists;
}

uint cbt_leafIndexToHeapIndex(uint leafIndex) {
    uint maxD = cbt_maxDepth();
    uint count = 0u;

    for (uint h = 1u; h < (1u << (maxD + 1u)); h++) {
        if (cbt_getBit(h) && cbt_isLeaf(h)) {
            if (count == leafIndex) {
                return h;
            }
            count++;
        }
    }

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

    if (heapIndex == 1u) {
        v0 = vec2(0.0, 0.0);
        v1 = vec2(1.0, 0.0);
        v2 = vec2(0.0, 1.0);
    } else if (heapIndex == 2u) {
        v0 = vec2(1.0, 1.0);
        v1 = vec2(0.0, 1.0);
        v2 = vec2(1.0, 0.0);
    } else {
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

        for (uint d = 1u; d < depth; d++) {
            uint bitPos = depth - 1u - d;
            uint bit = (heapIndex >> bitPos) & 1u;

            vec2 midpoint = (v1 + v2) * 0.5;

            if (bit == 0u) {
                v2 = v1;
                v1 = midpoint;
            } else {
                v1 = v2;
                v2 = midpoint;
            }

            vec2 temp = v0;
            v0 = v1;
            v1 = v2;
            v2 = temp;
        }
    }

    return LEBTriangle(v0, v1, v2);
}

float sampleHeight(vec2 uv) {
    return texture(heightMap, uv).r * pc.heightScale;
}

vec3 uvToWorldPos(vec2 uv) {
    float height = sampleHeight(uv);
    return vec3((uv.x - 0.5) * pc.terrainSize, height, (uv.y - 0.5) * pc.terrainSize);
}

void main() {
    uint triangleIndex = gl_VertexIndex / 3u;
    uint vertexInTri = gl_VertexIndex % 3u;

    uint heapIndex = cbt_leafIndexToHeapIndex(triangleIndex);
    LEBTriangle tri = leb_decodeTriangle(heapIndex);

    vec2 uv;
    if (vertexInTri == 0u) {
        uv = tri.v0;
    } else if (vertexInTri == 1u) {
        uv = tri.v1;
    } else {
        uv = tri.v2;
    }

    vec3 worldPos = uvToWorldPos(uv);

    gl_Position = pc.lightViewProj * vec4(worldPos, 1.0);
}
