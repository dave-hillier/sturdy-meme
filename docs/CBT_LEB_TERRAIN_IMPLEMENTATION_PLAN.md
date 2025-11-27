# Concurrent Binary Tree / Longest Edge Bisection Terrain Implementation Plan

A comprehensive implementation plan for GPU-driven adaptive terrain tessellation using Concurrent Binary Trees (CBT) and Longest Edge Bisection (LEB), based on the Unity Labs research and HPG paper by Jonathan Dupuy.

Use this as a reference implementation:
https://github.com/jdupuy/LongestEdgeBisection2D

## Table of Contents

1. [Overview](#overview)
2. [Algorithm Summary](#algorithm-summary)
3. [Vulkan/MoltenVK Constraints](#vulkanmoltenvk-constraints)
4. [Phase 1: CBT Data Structure](#phase-1-cbt-data-structure)
5. [Phase 2: LEB Subdivision Logic](#phase-2-leb-subdivision-logic)
6. [Phase 3: Sum Reduction Pipeline](#phase-3-sum-reduction-pipeline)
7. [Phase 4: Subdivision Update](#phase-4-subdivision-update)
8. [Phase 5: Rendering Pipeline](#phase-5-rendering-pipeline)
9. [Phase 6: LOD Criteria](#phase-6-lod-criteria)
10. [Phase 7: Meshlet Enhancement](#phase-7-meshlet-enhancement)
11. [Phase 8: Height Map Integration](#phase-8-height-map-integration)
12. [Phase 9: Material and Shading](#phase-9-material-and-shading)
13. [Phase 10: Performance Optimization](#phase-10-performance-optimization)
14. [Phase 11: Shadow Integration](#phase-11-shadow-integration)
15. [Phase 12: Integration with Existing Systems](#phase-12-integration-with-existing-systems)
16. [Implementation Milestones](#implementation-milestones)
17. [Appendix A: Vulkan Resource Checklist](#appendix-a-vulkan-resource-checklist)
18. [Appendix B: Memory Layout](#appendix-b-memory-layout)
19. [References](#references)

---

## Overview

### What is CBT/LEB?

**Longest Edge Bisection (LEB)** is a subdivision scheme that recursively divides triangles along their longest edge, producing an adaptive tessellation free of T-junctions (cracks). This property makes it ideal for terrain rendering.

**Concurrent Binary Trees (CBT)** is a data structure that enables parallel (GPU) manipulation of binary trees. Since LEB produces a binary tree structure (each triangle subdivides into two children), CBT allows the entire subdivision process to run on the GPU via compute shaders.

### Why CBT/LEB for This Project?

Current terrain: A simple disc ground mesh with no LOD system.

Benefits of CBT/LEB:
- **Adaptive resolution**: High detail near camera, coarse detail at distance
- **Crack-free by construction**: No T-junctions, no post-processing needed
- **GPU-driven**: No CPU bottleneck for subdivision decisions
- **Memory efficient**: 64MB for depth-27 tree covering kilometres
- **Complements existing systems**: Works with the existing grass compute shader, wind system, and lighting pipeline

### Target Specifications

Based on Unity's implementation:
- **Terrain coverage**: 2km × 2km (scalable)
- **Maximum CBT depth**: 28 (quarter-meter leaf nodes)
- **Meshlet subdivision**: 256 triangles per leaf (1.5cm resolution with displacement)
- **Target leaf nodes**: ~10,000 active (varies with view)
- **Memory budget**: ~64MB for CBT buffer

---

## Algorithm Summary

### The Core Loop (Per Frame)

```
1. DISPATCH: Read root node → get leaf count → set indirect args
2. SUBDIVISION: For each leaf triangle:
   - Evaluate LOD criteria
   - Split or merge based on screen-space edge length
   - Update CBT bitfield with atomic operations
3. SUM REDUCTION: Rebuild sum reduction tree (multiple passes)
4. RENDER: Draw triangles using leaf node enumeration
```

### CBT Data Structure

The CBT is stored as a flat integer array containing:
1. **Sum reduction tree**: Stores cumulative leaf counts for fast enumeration
2. **Bitfield**: Encodes the binary tree structure (1 = leaf node present)

```
Memory layout:
[Sum reduction levels 0..N-6] [Packed bitfield]
     ↑                              ↑
  Progressive sums            Actual tree structure
```

### LEB Encoding

Each triangle in the subdivision is identified by a **heap index**:
- Root triangles have indices 1 and 2
- Child triangles of index `i` have indices `2i` (left) and `2i+1` (right)
- The depth of a node is `floor(log2(index))`

Triangle vertices are computed procedurally from the heap index using the LEB library.

---

## Vulkan/MoltenVK Constraints

### Fully Supported Features (Safe to Use)

| Feature | Notes |
|---------|-------|
| Compute shaders | Core of the algorithm |
| Storage buffers | CBT data, indirect args |
| Atomic `uint` operations | `atomicOr`, `atomicAnd`, `atomicAdd` on storage buffers |
| Indirect draw | `vkCmdDrawIndirect` for GPU-driven rendering |
| Shared memory | Critical for sum reduction optimization |
| Push constants | Per-dispatch parameters |
| 2D texture sampling | Height maps, material maps |

### Constraints to Work Around

#### 1. No Geometry Shaders
**Impact**: Cannot use `gl_Layer` for multi-cascade shadow rendering.
**Solution**: Use separate render passes per cascade (already the pattern in this codebase).

#### 2. No Float Atomics on Images
**Impact**: Not needed for CBT/LEB - we only use integer atomics.
**Status**: Non-issue.

#### 3. Subgroup Operations (Query at Runtime)
**Impact**: Can optimize sum reduction with subgroup operations.
**Solution**: Query support, provide fallback using shared memory.

```cpp
bool hasSubgroupArithmetic = (subgroupProps.supportedOperations &
                              VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0;
```

#### 4. Async Compute (May Serialize on Intel Macs)
**Impact**: Sum reduction passes may not overlap with graphics.
**Solution**: Use proper synchronization; design works correctly either way.

#### 5. Workgroup Size
**Requirement**: Use multiples of 32 for Apple GPU SIMD width.
**Plan**: Use 64 threads (8×8 or 64×1) for all compute shaders.

### Memory Alignment Requirements

- Storage buffer offsets must be aligned to `minStorageBufferOffsetAlignment`
- Typical value: 256 bytes on most GPUs
- CBT buffer layout accounts for this

---

## Phase 1: CBT Data Structure

### 1.1 Understanding the CBT Memory Layout

The CBT uses an optimized packed representation:

```
For max depth D:
- Bitfield size: 2^(D-1) bits = 2^(D-4) bytes
- Sum reduction levels: D-5 levels (levels 6+ store full node counts)
- Level 1 (root): 1 node, stores total leaf count
- Level 2: 2 nodes
- ...
- Level 5: 16 nodes (each can hold values 0-32)
- Below level 5: Packed into the bitfield with implicit structure
```

### 1.2 CBT Buffer Structure

```cpp
struct CBTBuffer {
    // Total size = 2^(maxDepth-1) bits for bitfield
    //            + sum reduction tree overhead

    // For depth 28: ~64MB total
    // For depth 20: ~256KB (good for initial testing)

    uint32_t maxDepth;
    std::vector<uint32_t> data;  // Flat array on GPU

    VkBuffer buffer;
    VmaAllocation allocation;
};
```

### 1.3 Initialization

The CBT starts with two root triangles covering the terrain quad:

```glsl
// Initialize CBT to have exactly 2 leaf nodes (the two base triangles)
// Bit positions for heap indices 1 and 2 are set to 1
// All sum reduction nodes are initialized accordingly

void initializeCBT(uint maxDepth) {
    // Clear all bits
    memset(cbtData, 0, cbtSize);

    // Set leaf nodes for base triangles (heap indices 1 and 2)
    // These map to specific bit positions in the bitfield
    cbt_setBit(1, true);  // Base triangle 1
    cbt_setBit(2, true);  // Base triangle 2

    // Rebuild sum reduction tree
    rebuildSumReduction();
}
```

### 1.4 CBT Library Functions (GLSL)

Port Jonathan Dupuy's CBT library to GLSL:

```glsl
// Core CBT operations
uint cbt_heapRead(uint heapIndex);           // Read node value
void cbt_heapWrite(uint heapIndex, uint v);  // Write node value (atomic)
uint cbt_leafCount();                         // Total leaves (root node value)
uint cbt_leafIndexToHeapIndex(uint leafIdx); // Map leaf to heap index

// Bit manipulation
bool cbt_isBitSet(uint bitIndex);
void cbt_setBit(uint bitIndex, bool value);

// Tree navigation
uint cbt_parent(uint heapIndex);
uint cbt_leftChild(uint heapIndex);
uint cbt_rightChild(uint heapIndex);
uint cbt_sibling(uint heapIndex);
uint cbt_depth(uint heapIndex);
```

### 1.5 Vulkan Resource Setup

```cpp
void createCBTBuffer(uint32_t maxDepth) {
    // Calculate buffer size
    uint64_t bitfieldBits = 1ull << (maxDepth - 1);
    uint64_t bitfieldBytes = bitfieldBits / 8;
    uint64_t sumReductionBytes = calculateSumReductionSize(maxDepth);
    uint64_t totalBytes = bitfieldBytes + sumReductionBytes;

    // Create storage buffer
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.size = totalBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                    &cbtBuffer, &cbtAllocation, nullptr);
}
```

---

## Phase 2: LEB Subdivision Logic

### 2.1 LEB Library Functions (GLSL)

The LEB library computes triangle vertices from heap indices:

```glsl
// Core LEB operations
struct LEBTriangle {
    vec2 v0, v1, v2;  // Triangle vertices in [0,1]² space
};

LEBTriangle leb_decodeTriangle(uint heapIndex);
uint leb_splitNode(uint heapIndex);  // Returns left child index
uint leb_mergeNode(uint heapIndex);  // Returns parent index

// Neighbor finding (for crack-free subdivision)
uint leb_edgeNeighbor(uint heapIndex);     // Across longest edge
uint leb_diamondParent(uint heapIndex);    // Diamond ancestor for merge
```

### 2.2 Triangle Vertex Computation

LEB encodes triangles procedurally. The key insight: reading the heap index bit-by-bit from MSB to LSB traces the subdivision path from root to leaf.

```glsl
LEBTriangle leb_decodeTriangle(uint heapIndex) {
    // Start with base triangle covering unit square
    vec2 v0 = vec2(0, 0);
    vec2 v1 = vec2(1, 0);
    vec2 v2 = vec2(0, 1);

    // Determine if this is base triangle 1 or 2
    bool isTriangle2 = (heapIndex & highestSetBit(heapIndex)) != 0;
    if (isTriangle2) {
        v0 = vec2(1, 1);
        v1 = vec2(0, 1);
        v2 = vec2(1, 0);
    }

    // Trace subdivision path
    uint depth = cbt_depth(heapIndex);
    for (uint d = 1; d < depth; d++) {
        uint bit = (heapIndex >> (depth - 1 - d)) & 1u;

        // Bisect along longest edge
        vec2 midpoint = (v1 + v2) * 0.5;

        if (bit == 0) {
            // Left child
            v2 = v1;
            v1 = midpoint;
        } else {
            // Right child
            v1 = v2;
            v2 = midpoint;
        }

        // Rotate triangle to maintain consistent orientation
        vec2 temp = v0;
        v0 = v1;
        v1 = v2;
        v2 = temp;
    }

    return LEBTriangle(v0, v1, v2);
}
```

### 2.3 Crack-Free Splitting and Merging

The critical property of LEB: triangles can only split/merge in pairs to maintain crack-free geometry.

**Split Rule**: When splitting a triangle, its edge neighbor (across the longest edge) must also split.

**Merge Rule**: When merging, both siblings must be leaves, AND their diamond parent's children must all be leaves.

```glsl
void leb_splitNodeConforming(uint heapIndex) {
    // Split this node
    cbt_split(heapIndex);

    // Find and split edge neighbor to prevent cracks
    uint neighbor = leb_edgeNeighbor(heapIndex);
    if (neighbor != 0 && cbt_isLeaf(neighbor)) {
        cbt_split(neighbor);
    }
}

bool leb_canMergeNode(uint heapIndex) {
    uint sibling = cbt_sibling(heapIndex);

    // Both siblings must be leaves
    if (!cbt_isLeaf(sibling)) return false;

    // Check diamond constraint
    uint diamondParent = leb_diamondParent(heapIndex);
    if (diamondParent != 0) {
        // All children of diamond parent must be leaves
        // (prevents creating T-junctions)
        if (!allChildrenAreLeaves(diamondParent)) return false;
    }

    return true;
}
```

---

## Phase 3: Sum Reduction Pipeline

### 3.1 The Sum Reduction Problem

After subdivision updates modify the bitfield, the sum reduction tree must be rebuilt. This is the most expensive operation.

**Naive approach**: One thread per node, atomic operations everywhere → massive contention.

**Optimized approach** (from Unity's presentation):
1. Process bottom 6 levels using `countBits()` on 32-bit chunks
2. Use shared memory to batch writes
3. Avoid atomics by having one thread write per output location

### 3.2 First Sum Reduction Pass (Levels 5-1)

```glsl
// Workgroup size: 256 threads
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

shared uint localSums[256];

void main() {
    uint threadId = gl_LocalInvocationIndex;
    uint groupId = gl_WorkGroupID.x;

    // Each thread reads 32 bits from bitfield
    uint bitfieldOffset = (groupId * 256 + threadId) * 32;
    uint bits = cbtBitfield[bitfieldOffset / 32];

    // Count set bits = number of leaves in this 32-bit chunk
    uint leafCount = bitCount(bits);

    // Store in shared memory
    localSums[threadId] = leafCount;
    barrier();

    // Parallel reduction within workgroup
    for (uint stride = 128; stride > 0; stride >>= 1) {
        if (threadId < stride) {
            localSums[threadId] += localSums[threadId + stride];
        }
        barrier();
    }

    // Thread 0 writes result for this workgroup
    if (threadId == 0) {
        // Write to appropriate level of sum reduction tree
        sumReductionTree[groupId] = localSums[0];
    }
}
```

### 3.3 Subsequent Sum Reduction Passes

```glsl
// Process one level of sum reduction
// Input: level N values
// Output: level N-1 values (half as many)

void main() {
    uint threadId = gl_LocalInvocationIndex;
    uint groupId = gl_WorkGroupID.x;

    uint inputIdx = (groupId * 256 + threadId) * 2;

    // Read two children, write sum to parent
    uint leftChild = sumReductionTree[inputLevelOffset + inputIdx];
    uint rightChild = sumReductionTree[inputLevelOffset + inputIdx + 1];

    localSums[threadId] = leftChild + rightChild;
    barrier();

    // Batch writes using shared memory to avoid conflicts
    // (Pack multiple narrow values into uint32 writes)

    if (threadId < outputCount) {
        sumReductionTree[outputLevelOffset + threadId] = localSums[threadId];
    }
}
```

### 3.4 Pipeline Structure

```cpp
void rebuildSumReduction(VkCommandBuffer cmd) {
    // Pass 1: Bitfield → Level 5 (countBits optimization)
    vkCmdDispatch(cmd, numWorkgroups_pass1, 1, 1);

    // Memory barrier
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        ...);

    // Passes 2-N: Level by level reduction
    for (int level = 5; level > 0; level--) {
        updatePushConstants(level);
        vkCmdDispatch(cmd, numWorkgroups_level[level], 1, 1);

        vkCmdPipelineBarrier(cmd, ...);
    }
}
```

---

## Phase 4: Subdivision Update

### 4.1 Dispatcher Shader

Reads leaf count and sets up indirect dispatch/draw arguments:

```glsl
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0) buffer CBT { uint cbtData[]; };
layout(binding = 1) buffer IndirectDispatch {
    uint dispatchX;
    uint dispatchY;
    uint dispatchZ;
};
layout(binding = 2) buffer IndirectDraw {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};

void main() {
    uint leafCount = cbt_leafCount();

    // Subdivision dispatch: one thread per leaf
    dispatchX = (leafCount + 63) / 64;  // 64 threads per workgroup
    dispatchY = 1;
    dispatchZ = 1;

    // Draw args: 3 vertices per triangle
    vertexCount = leafCount * 3;
    instanceCount = 1;
    firstVertex = 0;
    firstInstance = 0;
}
```

### 4.2 Subdivision Compute Shader

Each thread handles one leaf triangle:

```glsl
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec3 cameraPos;
    float targetEdgeLength;  // Pixels
    float splitThreshold;
    float mergeThreshold;
};

void main() {
    uint leafIndex = gl_GlobalInvocationID.x;

    // Check bounds
    uint totalLeaves = cbt_leafCount();
    if (leafIndex >= totalLeaves) return;

    // Map leaf index to heap index
    uint heapIndex = cbt_leafIndexToHeapIndex(leafIndex);

    // Decode triangle vertices
    LEBTriangle tri = leb_decodeTriangle(heapIndex);

    // Sample height map for 3D positions
    vec3 p0 = vec3(tri.v0.x * terrainSize, sampleHeight(tri.v0), tri.v0.y * terrainSize);
    vec3 p1 = vec3(tri.v1.x * terrainSize, sampleHeight(tri.v1), tri.v1.y * terrainSize);
    vec3 p2 = vec3(tri.v2.x * terrainSize, sampleHeight(tri.v2), tri.v2.y * terrainSize);

    // Compute longest edge length in screen space
    float edgeLength = computeLongestEdgeScreenLength(p0, p1, p2, viewProj);

    // Frustum cull (get AABB of triangle)
    vec3 minBound = min(min(p0, p1), p2);
    vec3 maxBound = max(max(p0, p1), p2);
    bool visible = frustumTestAABB(minBound, maxBound);

    // LOD decision
    uint depth = cbt_depth(heapIndex);

    if (visible && edgeLength > splitThreshold && depth < maxDepth) {
        // Split: create two child leaves
        leb_splitNodeConforming(heapIndex);
    } else if (edgeLength < mergeThreshold && depth > 1) {
        // Merge: remove this leaf and sibling, parent becomes leaf
        if (leb_canMergeNode(heapIndex)) {
            leb_mergeNodeConforming(heapIndex);
        }
    }
    // else: keep current state
}
```

### 4.3 Screen-Space Edge Length Calculation

```glsl
float computeLongestEdgeScreenLength(vec3 p0, vec3 p1, vec3 p2, mat4 viewProj) {
    // Project to clip space
    vec4 c0 = viewProj * vec4(p0, 1.0);
    vec4 c1 = viewProj * vec4(p1, 1.0);
    vec4 c2 = viewProj * vec4(p2, 1.0);

    // Perspective divide to NDC
    vec2 ndc0 = c0.xy / c0.w;
    vec2 ndc1 = c1.xy / c1.w;
    vec2 ndc2 = c2.xy / c2.w;

    // Convert to pixels (assuming screenSize uniform)
    vec2 s0 = (ndc0 * 0.5 + 0.5) * screenSize;
    vec2 s1 = (ndc1 * 0.5 + 0.5) * screenSize;
    vec2 s2 = (ndc2 * 0.5 + 0.5) * screenSize;

    // Find longest edge
    float e01 = length(s1 - s0);
    float e12 = length(s2 - s1);
    float e20 = length(s0 - s2);

    return max(max(e01, e12), e20);
}
```

---

## Phase 5: Rendering Pipeline

### 5.1 Indirect Draw Setup

The terrain is rendered using `vkCmdDrawIndirect` with vertex count from the dispatcher:

```cpp
void recordTerrainRender(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline);
    vkCmdBindDescriptorSets(cmd, ...);

    // No vertex buffer - vertices generated procedurally
    vkCmdDrawIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}
```

### 5.2 Vertex Shader

```glsl
layout(binding = 0) buffer CBT { uint cbtData[]; };
layout(binding = 1) uniform sampler2D heightMap;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 model;
    vec2 terrainSize;
    float heightScale;
};

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;

void main() {
    // Determine which triangle and vertex
    uint triangleIndex = gl_VertexIndex / 3;
    uint vertexInTri = gl_VertexIndex % 3;

    // Map triangle index to heap index (leaf enumeration)
    uint heapIndex = cbt_leafIndexToHeapIndex(triangleIndex);

    // Decode triangle
    LEBTriangle tri = leb_decodeTriangle(heapIndex);

    // Select vertex
    vec2 uv;
    if (vertexInTri == 0) uv = tri.v0;
    else if (vertexInTri == 1) uv = tri.v1;
    else uv = tri.v2;

    // Sample height
    float height = texture(heightMap, uv).r * heightScale;

    // World position
    vec3 worldPos = vec3(uv.x * terrainSize.x, height, uv.y * terrainSize.y);
    worldPos = (model * vec4(worldPos, 1.0)).xyz;

    // Calculate normal from height map gradient
    vec2 texelSize = 1.0 / textureSize(heightMap, 0);
    float hL = texture(heightMap, uv + vec2(-texelSize.x, 0)).r * heightScale;
    float hR = texture(heightMap, uv + vec2(texelSize.x, 0)).r * heightScale;
    float hD = texture(heightMap, uv + vec2(0, -texelSize.y)).r * heightScale;
    float hU = texture(heightMap, uv + vec2(0, texelSize.y)).r * heightScale;

    vec3 normal = normalize(vec3(hL - hR, 2.0 * texelSize.x * terrainSize.x, hD - hU));

    // Output
    gl_Position = viewProj * vec4(worldPos, 1.0);
    fragUV = uv;
    fragNormal = normal;
    fragWorldPos = worldPos;
}
```

### 5.3 Fragment Shader

```glsl
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(binding = 2) uniform sampler2D albedoMap;
layout(binding = 3) uniform sampler2D normalMap;
layout(binding = 4) uniform sampler2DShadow shadowMap;

layout(binding = 5) uniform LightData {
    vec3 sunDirection;
    vec3 sunColor;
    vec3 ambientColor;
    mat4 lightSpaceMatrix;
};

void main() {
    // Sample textures
    vec3 albedo = texture(albedoMap, fragUV * textureScale).rgb;

    // Normal mapping (optional)
    vec3 N = normalize(fragNormal);

    // Shadow
    vec4 lightSpacePos = lightSpaceMatrix * vec4(fragWorldPos, 1.0);
    float shadow = texture(shadowMap, lightSpacePos.xyz / lightSpacePos.w);

    // Lighting (reuse existing PBR from shader.frag)
    vec3 L = normalize(sunDirection);
    float NdotL = max(dot(N, L), 0.0);

    vec3 diffuse = albedo * sunColor * NdotL * shadow;
    vec3 ambient = albedo * ambientColor;

    outColor = vec4(diffuse + ambient, 1.0);
}
```

---

## Phase 6: LOD Criteria

### 6.1 Primary Criterion: Screen-Space Edge Length

The core LOD metric: subdivide until longest edge is below a target pixel count.

```glsl
const float TARGET_EDGE_PIXELS = 32.0;  // Artist-tunable
const float SPLIT_THRESHOLD = TARGET_EDGE_PIXELS * 1.5;  // Hysteresis
const float MERGE_THRESHOLD = TARGET_EDGE_PIXELS * 0.5;

bool shouldSplit(float edgeLengthPixels, uint currentDepth) {
    return edgeLengthPixels > SPLIT_THRESHOLD && currentDepth < MAX_DEPTH;
}

bool shouldMerge(float edgeLengthPixels, uint currentDepth) {
    return edgeLengthPixels < MERGE_THRESHOLD && currentDepth > MIN_DEPTH;
}
```

### 6.2 Frustum Culling Criterion

Don't subdivide triangles outside the view frustum:

```glsl
bool isTriangleVisible(vec3 p0, vec3 p1, vec3 p2) {
    // Compute AABB
    vec3 minB = min(min(p0, p1), p2);
    vec3 maxB = max(max(p0, p1), p2);

    // Add height margin for terrain variation
    minB.y -= heightMargin;
    maxB.y += heightMargin;

    // Test against 6 frustum planes
    for (int i = 0; i < 6; i++) {
        vec4 plane = frustumPlanes[i];

        // Find AABB corner most in direction of plane normal
        vec3 pVertex = vec3(
            plane.x > 0 ? maxB.x : minB.x,
            plane.y > 0 ? maxB.y : minB.y,
            plane.z > 0 ? maxB.z : minB.z
        );

        if (dot(vec4(pVertex, 1.0), plane) < 0) {
            return false;  // Entirely outside this plane
        }
    }

    return true;
}
```

### 6.3 Curvature-Based Refinement (Future Enhancement)

For better silhouette preservation:

```glsl
float computeCurvature(vec2 uv) {
    // Sample height map second derivatives
    vec2 texelSize = 1.0 / textureSize(heightMap, 0);

    float h = texture(heightMap, uv).r;
    float hL = texture(heightMap, uv + vec2(-texelSize.x, 0)).r;
    float hR = texture(heightMap, uv + vec2(texelSize.x, 0)).r;
    float hD = texture(heightMap, uv + vec2(0, -texelSize.y)).r;
    float hU = texture(heightMap, uv + vec2(0, texelSize.y)).r;

    // Laplacian approximation
    float laplacian = (hL + hR + hD + hU - 4.0 * h);

    return abs(laplacian);
}

bool shouldSplitWithCurvature(float edgeLength, float curvature, uint depth) {
    float adjustedThreshold = SPLIT_THRESHOLD / (1.0 + curvature * curvatureWeight);
    return edgeLength > adjustedThreshold && depth < MAX_DEPTH;
}
```

---

## Phase 7: Meshlet Enhancement

### 7.1 Why Meshlets?

A CBT depth of 28 gives ~0.25m resolution. For 1.5cm detail, we need additional subdivision. Rather than increasing CBT depth (which doubles memory), we render a pre-tessellated meshlet per leaf node.

### 7.2 Meshlet Geometry

Meshlets are small pre-subdivided LEB triangles stored in vertex/index buffers:

```cpp
struct MeshletBuffers {
    VkBuffer vertexBuffer;   // Pre-computed meshlet vertices
    VkBuffer indexBuffer;    // Triangle indices

    uint32_t trianglesPerMeshlet;  // e.g., 256
    uint32_t verticesPerMeshlet;
    uint32_t indicesPerMeshlet;
};

void createMeshletBuffers(uint32_t subdivisionLevel) {
    // Generate meshlet as an LEB subdivision of a unit triangle
    // Level 0: 1 triangle
    // Level 1: 2 triangles
    // Level 2: 4 triangles
    // Level 8: 256 triangles

    std::vector<vec2> vertices;
    std::vector<uint16_t> indices;

    // Recursively subdivide base triangle
    generateMeshletGeometry(subdivisionLevel, vertices, indices);

    // Upload to GPU
    // ...
}
```

### 7.3 Meshlet Vertex Shader

```glsl
layout(binding = 0) buffer CBT { uint cbtData[]; };
layout(binding = 1) buffer MeshletVerts { vec2 meshletVertices[]; };

void main() {
    // Which CBT leaf and which vertex within meshlet
    uint cbtLeafIndex = gl_InstanceIndex;  // One instance per CBT leaf
    uint meshletVertIndex = gl_VertexIndex;

    // Get parent triangle
    uint heapIndex = cbt_leafIndexToHeapIndex(cbtLeafIndex);
    LEBTriangle parentTri = leb_decodeTriangle(heapIndex);

    // Get meshlet vertex (in unit triangle space)
    vec2 localPos = meshletVertices[meshletVertIndex];

    // Transform to parent triangle's space
    vec2 uv = parentTri.v0 +
              localPos.x * (parentTri.v1 - parentTri.v0) +
              localPos.y * (parentTri.v2 - parentTri.v0);

    // Continue as before: sample height, compute normal, project
    float height = texture(heightMap, uv).r * heightScale;
    // ...
}
```

### 7.4 Indirect Instanced Draw

```cpp
void recordMeshletRender(VkCommandBuffer cmd) {
    // Update indirect draw buffer:
    // vertexCount = meshletVertexCount
    // instanceCount = cbtLeafCount (set by dispatcher)

    vkCmdBindVertexBuffers(cmd, 0, 1, &meshletVertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, meshletIndexBuffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexedIndirect(cmd, indirectDrawBuffer, 0, 1,
                              sizeof(VkDrawIndexedIndirectCommand));
}
```

---

## Phase 8: Height Map Integration

### 8.1 Height Map Format and Resolution

```cpp
struct TerrainHeightMap {
    VkImage image;
    VkImageView view;
    VkSampler sampler;

    uint32_t resolution;  // e.g., 4096 for 2km terrain = 0.5m per texel
    float heightScale;    // World units per normalized value

    VkFormat format = VK_FORMAT_R16_UNORM;  // 16-bit normalized
};
```

### 8.2 Virtual Texturing (Future Enhancement)

For very large terrains, a virtual texture system streams height/material data:

```cpp
struct VirtualTexture {
    // Page table texture (indirection)
    VkImage pageTable;

    // Physical texture cache (actual data)
    VkImage physicalCache;

    // Feedback buffer (which pages are needed)
    VkBuffer feedbackBuffer;

    // Page size and counts
    uint32_t pageSize = 256;  // Texels per page
    uint32_t physicalCacheSize = 4096;  // Total cache texels
};
```

### 8.3 Micro-Displacement

For sub-meter detail, blend in material displacement textures:

```glsl
float sampleHeightWithDisplacement(vec2 uv) {
    // Base height from terrain height map
    float baseHeight = texture(heightMap, uv).r * heightScale;

    // Material displacement (from virtual texture)
    vec2 detailUV = uv * detailTextureScale;
    float displacement = texture(displacementMap, detailUV).r * displacementScale;

    // Blend based on distance (less detail far away)
    float blend = smoothstep(microDisplacementFar, microDisplacementNear, distToCamera);

    return baseHeight + displacement * blend;
}
```

---

## Phase 9: Material and Shading

### 9.1 Material Splatting

Multiple terrain materials blended based on a blend map:

```glsl
struct TerrainMaterial {
    sampler2D albedoMap;
    sampler2D normalMap;
    sampler2D roughnessMap;
    float textureScale;
};

layout(binding = 6) uniform sampler2D blendMap;  // RGBA = weights for 4 materials

vec3 sampleTerrainAlbedo(vec2 uv) {
    vec4 blend = texture(blendMap, uv);

    vec3 albedo = vec3(0);
    albedo += texture(material0.albedo, uv * material0.scale).rgb * blend.r;
    albedo += texture(material1.albedo, uv * material1.scale).rgb * blend.g;
    albedo += texture(material2.albedo, uv * material2.scale).rgb * blend.b;
    albedo += texture(material3.albedo, uv * material3.scale).rgb * blend.a;

    return albedo;
}
```

### 9.2 Integration with Existing PBR Pipeline

Reuse the existing `shader.frag` PBR lighting:

```glsl
// In terrain fragment shader
void main() {
    // Sample terrain materials
    vec3 albedo = sampleTerrainAlbedo(fragUV);
    vec3 normal = sampleTerrainNormal(fragUV, fragNormal, fragTangent);
    float roughness = sampleTerrainRoughness(fragUV);
    float metallic = 0.0;  // Terrain is typically non-metallic

    // Reuse existing PBR calculation
    vec3 color = calculatePBR(albedo, normal, roughness, metallic,
                               fragWorldPos, cameraPos, sunDirection, sunColor);

    // Apply shadows
    float shadow = sampleShadowMap(fragWorldPos);
    color *= shadow;

    // Apply atmospheric effects (from existing atmosphere system)
    color = applyAtmosphere(color, fragWorldPos, cameraPos);

    outColor = vec4(color, 1.0);
}
```

---

## Phase 10: Performance Optimization

### 10.1 Sum Reduction Optimization (Critical)

The Unity team achieved 14.5× speedup with these optimizations:

**Optimization 1: Avoid GetDimensions()**
```glsl
// SLOW: Runtime query
uvec3 dims;
cbtBuffer.GetDimensions(dims.x);  // ~1.6x slower!

// FAST: Push constant
layout(push_constant) uniform PC { uint cbtMaxDepth; };
```

**Optimization 2: Batch Shared Memory Writes**
```glsl
// Instead of: each thread writes one value with atomics
// Do: accumulate in shared memory, then batch write

shared uint localBuffer[256];

void main() {
    // ... compute value ...
    localBuffer[threadId] = myValue;
    barrier();

    // Only first thread of each 16 writes to global memory
    if (threadId % 16 == 0) {
        // Pack 16 values into 3 uints and write
        // (specific packing depends on bit width at current level)
    }
}
```

**Optimization 3: Skip Unnecessary Levels**
```glsl
// First pass: Use countBits() to skip bottom 5 levels
uint bits = cbtBitfield[idx];
uint count = bitCount(bits);  // Instant sum of 32 leaves
// Write directly to level 6 of sum reduction
```

### 10.2 Subdivision Shader Optimization

**Early-out for stable triangles:**
```glsl
// If triangle hasn't changed for N frames, skip full calculation
if (triangleStableFrames[heapIndex] > STABLE_THRESHOLD) {
    // Quick frustum check only
    if (stillInFrustum(heapIndex)) return;
}
```

**Batch LOD decisions:**
```glsl
// Subgroup optimization: count splits/merges in wave
uint splitBallot = subgroupBallot(shouldSplit);
uint splitCount = bitCount(splitBallot);

// Single atomic per subgroup instead of per thread
if (gl_SubgroupInvocationID == 0) {
    // Batch update statistics
}
```

### 10.3 Memory Access Patterns

**Coalesced reads for CBT:**
```glsl
// Ensure adjacent threads read adjacent memory
uint baseIndex = gl_WorkGroupID.x * gl_WorkGroupSize.x;
uint myIndex = baseIndex + gl_LocalInvocationIndex;  // Coalesced!
```

**Texture cache efficiency:**
```glsl
// Sample height map with explicit LOD to improve cache hits
float h = textureLod(heightMap, uv, 0).r;
```

---

## Phase 11: Shadow Integration

### 11.1 Terrain Shadow Casting

Use the same CBT/LEB system but from light's perspective:

```cpp
void renderTerrainShadow(VkCommandBuffer cmd, const mat4& lightViewProj) {
    // Use existing CBT state (no separate subdivision for shadows)

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainShadowPipeline);

    // Push light space matrix
    vkCmdPushConstants(cmd, ..., &lightViewProj);

    // Draw with same indirect buffer
    vkCmdDrawIndirect(cmd, terrainIndirectDrawBuffer, ...);
}
```

### 11.2 Shadow Vertex Shader

```glsl
// Simplified - no lighting needed
void main() {
    uint heapIndex = cbt_leafIndexToHeapIndex(gl_VertexIndex / 3);
    LEBTriangle tri = leb_decodeTriangle(heapIndex);

    vec2 uv = selectVertex(tri, gl_VertexIndex % 3);
    float height = texture(heightMap, uv).r * heightScale;

    vec3 worldPos = vec3(uv.x * terrainSize.x, height, uv.y * terrainSize.y);

    gl_Position = lightViewProj * vec4(worldPos, 1.0);
}
```

### 11.3 Cascaded Shadow Map Consideration

For each cascade, render the terrain with appropriate view:

```cpp
for (uint32_t cascade = 0; cascade < NUM_CASCADES; cascade++) {
    vkCmdBeginRenderPass(cmd, &shadowRenderPassInfo[cascade], ...);

    // Push cascade-specific light matrix
    vkCmdPushConstants(cmd, ..., &cascadeLightViewProj[cascade]);

    vkCmdDrawIndirect(cmd, terrainIndirectDrawBuffer, ...);

    vkCmdEndRenderPass(cmd);
}
```

---

## Phase 12: Integration with Existing Systems

### 12.1 Grass System Integration

The grass compute shader needs terrain height:

```glsl
// In grass.comp
float getTerrainHeight(vec2 worldPos) {
    vec2 uv = worldPos / terrainSize;
    return texture(terrainHeightMap, uv).r * terrainHeightScale;
}

// Use instead of current flat Y=0
vec3 bladeWorldPos = vec3(worldPos2D.x, getTerrainHeight(worldPos2D), worldPos2D.y);
```

### 12.2 Wind System Integration

Wind already works - just ensure terrain-following grass uses correct height.

### 12.3 Post-Processing Integration

Terrain outputs to the same HDR render target:
- Tone mapping applies
- Bloom applies to bright terrain areas
- Auto-exposure includes terrain in calculations

### 12.4 Camera and Input

Camera collision with terrain:

```cpp
float Terrain::getHeightAt(float x, float z) const {
    // Sample CPU-side height map copy
    vec2 uv = vec2(x, z) / terrainSize;
    // Bilinear interpolation of height values
    return sampleHeightCPU(uv) * heightScale;
}

void Camera::update() {
    // Ensure camera doesn't go below terrain
    float terrainHeight = terrain.getHeightAt(position.x, position.z);
    if (position.y < terrainHeight + minCameraHeight) {
        position.y = terrainHeight + minCameraHeight;
    }
}
```

---

## Implementation Milestones

### Milestone 1: Adaptive Wireframe (Core System)

**Goal**: See adaptive wireframe - dense near camera, coarse far. This is the first meaningful visual that proves the entire CBT/LEB pipeline works.

**Deliverables**:
- `CBT.h/cpp` - CBT buffer management and initialization
- `cbt.glsl` - GLSL CBT library (heap operations, bit manipulation)
- `leb.glsl` - LEB library (triangle decode, conforming split/merge)
- `cbt_dispatcher.comp` - Read root node, set indirect args
- `cbt_subdivision.comp` - Per-leaf LOD evaluation, split/merge with atomics
- `cbt_sum_reduction.comp` - Rebuild sum tree (basic version first)
- `terrain.vert` - Vertex shader with LEB decode
- `terrain_wireframe.frag` - Wireframe visualization
- Full frame loop: dispatch → subdivide → sum reduce → render
- Screen-space edge length LOD criterion
- Hysteresis to prevent popping

**Verification**:
- Wireframe shows smooth LOD transitions as camera moves
- Dense triangles near camera, coarse triangles at distance
- Leaf count displayed on screen changes dynamically
- No visible cracks or T-junctions

---

### Milestone 2: Height Map Integration

**Goal**: Terrain follows a height map.

**Deliverables**:
- Height map loading/upload
- Height sampling in vertex shader
- Normal calculation from gradients

**Verification**: Hills and valleys visible. Camera can orbit around terrain.

---

### Milestone 3: Basic Texturing

**Goal**: Terrain has a texture instead of solid color.

**Deliverables**:
- Albedo texture (placeholder from opengameart.org)
- UV mapping based on world position
- Basic diffuse lighting

**Verification**: Textured terrain with shadows.

---

### Milestone 4: Meshlet Enhancement

**Goal**: Higher resolution without increasing CBT depth.

**Deliverables**:
- Meshlet buffer generation
- Instanced rendering per CBT leaf
- Meshlet vertex shader

**Verification**: Much denser wireframe without memory increase.

---

### Milestone 5: Material Splatting

**Goal**: Multiple terrain materials blended.

**Deliverables**:
- Blend map texture
- 4-material splatting shader
- Material textures (rock, grass, dirt, sand)

**Verification**: Visible material transitions.

---

### Milestone 6: Shadow Integration

**Goal**: Terrain casts and receives shadows.

**Deliverables**:
- Shadow pass using terrain geometry
- Shadow sampling in terrain fragment shader
- Integration with existing cascade system

**Verification**: Shadows from sun fall on terrain. Terrain shadows visible on other objects.

---

### Milestone 7: Grass Integration

**Goal**: Grass grows on terrain.

**Deliverables**:
- Grass compute shader samples terrain height
- Grass follows terrain surface
- Optional: grass density based on terrain slope

**Verification**: Grass field on hilly terrain.

---

### Milestone 8: Performance Optimization

**Goal**: Stable frame rate with target geometry.

**Deliverables**:
- Sum reduction optimizations (shared memory batching)
- Subgroup operations (with fallback)
- Profiling and tuning

**Verification**: Consistent performance, profiler data.

---

## Appendix A: Vulkan Resource Checklist

### Buffers

| Buffer | Size | Usage Flags |
|--------|------|-------------|
| CBT Data | 64MB (depth 28) | Storage, Transfer Dst |
| Indirect Dispatch | 12 bytes | Storage, Indirect |
| Indirect Draw | 16-20 bytes | Storage, Indirect |
| Meshlet Vertices | ~1MB | Vertex, Storage |
| Meshlet Indices | ~0.5MB | Index |

### Textures

| Texture | Format | Size | Purpose |
|---------|--------|------|---------|
| Height Map | R16_UNORM | 4096² | Terrain elevation |
| Normal Map | RGB8 | 4096² | Pre-computed normals (optional) |
| Blend Map | RGBA8 | 2048² | Material weights |
| Material Albedo (×4) | RGBA8 | 1024² | Diffuse textures |
| Material Normal (×4) | RGB8 | 1024² | Detail normals |

### Pipelines

| Pipeline | Type | Description |
|----------|------|-------------|
| CBT Dispatcher | Compute | Read root, set indirect args |
| CBT Subdivision | Compute | Per-leaf LOD update |
| CBT Sum Reduction | Compute | Rebuild sum tree (×N passes) |
| Terrain Render | Graphics | Main terrain drawing |
| Terrain Shadow | Graphics | Shadow map pass |

### Descriptor Sets

| Set | Contents |
|-----|----------|
| 0 - CBT | CBT buffer, indirect buffers |
| 1 - Terrain | Height map, blend map, materials |
| 2 - Lighting | Shadow map, light uniforms |

---

## Appendix B: Memory Layout

### CBT Buffer Layout (Depth 28)

```
Offset (bytes)    Content                    Size
────────────────────────────────────────────────────────
0                 Sum reduction level 0       4 bytes (root)
4                 Sum reduction level 1       8 bytes
12                Sum reduction level 2       16 bytes
...               ...                         ...
~128KB            Sum reduction level 22      ~16MB packed
~16MB             Bitfield                    ~48MB
────────────────────────────────────────────────────────
Total:            ~64MB
```

### Meshlet Vertex Layout (Level 8 = 256 triangles)

```
Vertex data per meshlet:
- 256 triangles × 3 vertices = 768 vertices
- Actually shared: ~400 unique vertices
- Per vertex: vec2 (8 bytes)
- Total: ~3.2KB per meshlet type
```

---

## Appendix C: Shader Include Structure

```
shaders/
├── cbt/
│   ├── cbt_common.glsl          # CBT data structure access
│   ├── cbt_sum_reduction.comp   # Sum reduction compute
│   └── cbt_dispatcher.comp      # Indirect args setup
├── leb/
│   ├── leb_common.glsl          # LEB triangle decode
│   └── leb_subdivision.comp     # Subdivision update
├── terrain/
│   ├── terrain.vert             # Main terrain vertex
│   ├── terrain.frag             # PBR terrain fragment
│   ├── terrain_shadow.vert      # Shadow pass vertex
│   └── terrain_wireframe.frag   # Debug wireframe
└── include/
    ├── frustum.glsl             # Frustum culling utilities
    └── screen_space.glsl        # Screen-space calculations
```

---

## References

1. **HPG 2020 Paper**: "GPU-Driven Rendering Pipelines" - Jonathan Dupuy, Unity Labs
2. **Unity GDC 2023 Talk**: "Concurrent Binary Trees for Terrain Rendering" - Xiao Ling Yao, Thomas Deliot
3. **Original CBT Paper**: "Concurrent Binary Trees" - Jonathan Dupuy
4. **LEB Paper**: "Longest Edge Bisection" subdivision scheme
5. **Mesh Shader Integration**: Yuri O'Donnell, SIGGRAPH presentation
6. **Vulkan Guide**: Memory management patterns for compute shaders

---

## Glossary

| Term | Definition |
|------|------------|
| **CBT** | Concurrent Binary Tree - data structure for parallel tree manipulation |
| **LEB** | Longest Edge Bisection - triangle subdivision scheme |
| **Heap Index** | Unique identifier for each node in the binary tree |
| **Leaf Node** | Active triangle in the subdivision (no children) |
| **Sum Reduction** | Tree of cumulative sums for efficient leaf enumeration |
| **Meshlet** | Pre-subdivided triangle mesh rendered per CBT leaf |
| **T-Junction** | Vertex that lies on another triangle's edge - causes cracks |
| **Conforming Split** | Split that maintains crack-free property |
