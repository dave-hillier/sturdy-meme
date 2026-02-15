# Engine vs. Nanite: Architectural Review

A comparison of this engine's rendering architecture against Unreal Engine 5's Nanite virtualized geometry system, based on actual source code review.

## Overview

Nanite decomposes all static geometry into ~128-triangle **clusters** organized in a **DAG**, then performs per-cluster LOD selection, culling, and rasterization entirely on the GPU. Materials are evaluated after visibility is determined via a **visibility buffer**.

This engine has implemented the core architectural skeleton of a Nanite-like pipeline: mesh clustering with DAG hierarchy, GPU-driven LOD selection, two-pass occlusion culling, normal cone backface culling, and a visibility buffer with compute material resolve. The comparison below evaluates how each subsystem compares to Nanite's production implementation.

---

## 1. Geometry Representation

| Aspect | This Engine | Nanite |
|---|---|---|
| **Cluster format** | `MeshCluster`: 64-128 triangles, bounding sphere, AABB, normal cone, DAG links (`MeshClusterBuilder.h:22-51`) | ~128 triangles, bounding sphere with monotonic error, DAG links |
| **Hierarchy** | DAG built via `buildWithDAG()` — spatial grouping (2-4 clusters/group), meshoptimizer simplification (`MeshClusterBuilder.h:120-122`) | DAG via METIS graph partitioning, boundary-locked edge collapse |
| **Error metric** | Per-cluster `error` and `parentError` in object space, projected to screen in `cluster_select.comp` | Per-cluster/group object-space error, projected via bounding sphere |
| **Global buffers** | `GPUClusterBuffer` — single vertex/index/cluster SSBO for all meshes (`MeshClusterBuilder.h:169-227`) | Global vertex/index pages with GPU-resident page table |
| **Vertex format** | 60 bytes/vertex (vec3 pos, vec3 normal, vec2 uv, vec4 tangent, vec4 color) | ~14.4 bytes/triangle (quantized positions, no stored tangents) |
| **Partitioning** | Spatial grouping heuristic (`groupClustersSpatially`) | METIS graph partitioning minimizing edge-cut |

**What's implemented**: The engine has the fundamental cluster+DAG representation. `MeshClusterBuilder::build()` creates flat clusters; `buildWithDAG()` iteratively groups and simplifies into coarser parents using meshoptimizer. `GPUClusterBuffer` manages global SSBOs for all clustered meshes.

**Gaps vs. Nanite**:
- **Partitioning quality**: Spatial grouping vs. METIS graph partitioning. METIS minimizes boundary edges between clusters, which directly determines crack-free LOD transition quality. Spatial grouping is a reasonable approximation but produces more boundary edges.
- **Boundary-locked simplification**: Nanite locks vertices shared between clusters during edge collapse, ensuring adjacent LOD levels share boundary vertices exactly. The engine uses meshoptimizer's generic simplification — it doesn't lock boundary vertices, which can introduce T-junctions at LOD transitions.
- **Compression**: 60 bytes/vertex vs ~14.4 bytes/triangle. Nanite quantizes positions relative to cluster bounds, derives tangents in the pixel shader, and uses specialized index encoding. The engine stores full uncompressed attributes.
- **Error monotonicity**: Nanite ensures error metrics are strictly monotonic through the DAG by taking bounding sphere unions at each level. The engine stores `error` and `parentError` but the monotonicity guarantee depends on the simplification producing increasing error at each level — not explicitly enforced.

---

## 2. LOD System

### Cluster DAG LOD Selection

**File**: `shaders/cluster_select.comp`

The engine implements Nanite's DAG-cut LOD selection on the GPU:

```
selected = myScreenError <= threshold && parentScreenError > threshold
```

Each compute thread evaluates one cluster. Screen-space error projection (`projectErrorToScreen` in `cull_compute_common.glsl:96-111`) uses the standard formula: `(objectError * screenHeight * 0.5) / clipW`. Default threshold is 1.0 pixel (`TwoPassCuller.h:228`).

Leaf handling: leaves are selected when reached (`!parentErrorOk || myErrorOk`), internal nodes only when they're on the cut (`myErrorOk && !parentErrorOk`).

**What matches Nanite**: Core selection algorithm, screen-space error projection, configurable pixel threshold.

**Gaps**:
- **Traversal strategy**: The engine dispatches one thread per cluster in the entire DAG and each thread independently evaluates its selection criterion. Nanite uses a **persistent-thread work queue** traversal — starting from root nodes and recursively enqueuing children — which avoids evaluating clusters deep in the hierarchy that will never be reached. For deep DAGs this is significantly more efficient.
- **Instance support**: The engine uses `meshId` as a crude instance index (`cluster_select.comp:91`). Nanite handles millions of instances — each (cluster, instance) pair is independently evaluated.

### Terrain LOD (CBT)

**Files**: `shaders/terrain/terrain_subdivision.comp`, `src/terrain/TerrainCBT.h`

This has no Nanite equivalent and is a strength. Concurrent Binary Tree adaptive subdivision with:
- Screen-space edge length metric with curvature adaptation
- Split/merge hysteresis (`SPLIT_THRESHOLD`, `MERGE_THRESHOLD`)
- Temporal spreading for deep triangles across frames
- Inline frustum culling
- Tile cache integration (high-res tiles near camera, coarse fallback far)

Nanite explicitly doesn't target heightmap terrain — its cluster topology assumes arbitrary meshes.

### Tree LOD

**Files**: `shaders/tree_impostor_cull.comp`, `shaders/tree_cell_cull.comp`

Two-tier: spatial cell culling → per-tree screen-space error LOD with geometry/impostor/culled tiers. Smooth blend via `smoothstep`, temporal coherence modes, visibility bit cache. This is a specialized LOD system for vegetation that Nanite also doesn't handle well (foliage is one of Nanite's known limitations).

### LOD Transitions

**File**: `shaders/dither_common.glsl`

Interleaved Gradient Noise dithered crossfade with temporal variant for TAA. This is the standard approach when LOD levels don't share boundary vertices. Nanite avoids needing transitions entirely because boundary locking guarantees watertight seams between adjacent LOD levels.

---

## 3. Culling

### Two-Pass Occlusion Culling

**Files**: `src/visibility/TwoPassCuller.h`, `shaders/cluster_cull.comp`

This directly implements Nanite's two-pass approach:

**Pass 1** (`passIndex == 0`): Test clusters that were visible last frame (`prevVisibleClusters[]`). High hit rate — most clusters visible last frame are still visible. Render these, build Hi-Z pyramid from resulting depth.

**Pass 2** (`passIndex == 1`): Test remaining clusters against the new Hi-Z. Catches newly visible clusters (disocclusion from camera movement).

Double-buffered visible cluster lists (`visibleClusterBuffers_` / `prevVisibleClusterBuffers_`) with `swapBuffers()` at end of frame.

**What matches Nanite**: The overall two-pass strategy, temporal visible list ping-pong, per-pass indirect draw generation.

**Gaps**:
- **Pass 2 skip set**: `cluster_cull.comp:123-126` iterates all clusters in pass 2 rather than skipping those already tested in pass 1. Nanite tracks which clusters were tested vs. untested to avoid redundant work.

### Per-Cluster Culling Tests

**File**: `shaders/cluster_cull.comp:112-209`

Each thread performs in sequence:
1. **Sphere frustum test** (`isSphereInFrustum`) — fast rejection
2. **AABB frustum test** (`isAABBInFrustum`) — precise test on all 6 planes
3. **Hi-Z occlusion test** (`hizOcclusionTestAABB`) — project AABB to screen rect, choose mip level covering ~2x2 texels, sample Hi-Z center, compare depth
4. **Normal cone backface test** (`dot(viewDir, worldConeAxis) > coneAngle`) — reject entire cluster if all normals face away

Output: `VkDrawIndexedIndirectCommand` via subgroup-batched atomics, plus visible cluster ID for next frame.

**What matches Nanite**: All four test types match (frustum sphere/AABB, Hi-Z occlusion, normal cone backface). Subgroup ballot batching reduces atomic contention by ~32x.

**Gaps**:
- **Hi-Z sampling**: Single center-point sample of the projected AABB rectangle. Nanite samples multiple points or uses conservative bounds. A single center sample can miss occluders that don't cover the AABB center.
- **AABB transform**: `cluster_cull.comp:150-155` transforms only min/max corners and takes component-wise min/max. This is only correct for axis-aligned transforms. For rotated instances, the 8-corner AABB should be transformed and re-bounded. The Hi-Z common code (`hiz_occlusion_common.glsl:20-53`) does correctly transform all 8 corners for the screen-space projection.

### Hi-Z Pyramid

**Files**: `src/postprocess/HiZSystem.h`, `shaders/hiz_downsample.comp`

- Format: `R32_SFLOAT`, reversed-Z (min of 2x2 = farthest = conservative for occlusion)
- Per-mip compute dispatch with push constants
- Edge handling for odd-sized source mips

This is solid. The `MipChainBuilder` with per-mip views and the reversed-Z convention are correct.

### Per-Object Culling (Legacy Path)

**Files**: `src/culling/GPUCullPass.h`, `shaders/scene_cull.comp`

Separate system for non-clustered objects: per-object sphere+AABB frustum test, Hi-Z occlusion, indirect draw generation. Max 8192 objects. This coexists with the cluster path.

---

## 4. Rasterization

### Visibility Buffer

**Files**: `src/visibility/VisibilityBuffer.h`, `shaders/visbuf.vert`, `shaders/visbuf.frag`, `shaders/visbuf_resolve.comp`

**Phase 1 — V-Buffer Rasterization** (hardware):
- Render target: `R32_UINT` (32-bit unsigned integer)
- Packing: `(instanceId << 23) | (triangleId & 0x7FFFFF)` — 9 bits instance (512 max), 23 bits triangle (~8M max)
- Vertex shader: minimal transform via push constants (`model`, `instanceId`, `triangleOffset`)
- Fragment shader: packs `gl_PrimitiveID + triangleOffset` with `instanceId`
- Alpha test support for masked materials (foliage)

**Phase 2 — Compute Material Resolve** (`visbuf_resolve.comp`):
- 8x8 workgroups
- Unpacks instanceId + triangleId from V-buffer
- Fetches 3 vertex indices from global index SSBO
- Fetches vertex data from global vertex SSBO (`PackedVertex`: pos+u, normal+v, tangent, color)
- Reconstructs world position from instance transform
- Computes perspective-correct barycentrics (screen-space projection + 1/w correction)
- Interpolates normals and UVs
- Evaluates simplified directional light PBR
- Writes to RGBA16F HDR output

**What matches Nanite**: Two-phase architecture (rasterize IDs → resolve materials), global vertex/index SSBOs, per-pixel barycentric reconstruction, decoupled geometry/material evaluation.

**Gaps**:
- **V-buffer width**: 32-bit vs Nanite's 64-bit. The 9-bit instance limit (512) and 23-bit triangle limit (~8M) are restrictive. Nanite uses 30 bits depth + 27 bits cluster + 7 bits triangle ID. The 64-bit format allows depth testing in the same atomic, enabling software rasterization.
- **Software rasterizer**: Not implemented. This is Nanite's biggest performance win — a compute shader that does scan-line rasterization with 64-bit atomic compare-and-swap, ~3x faster than hardware for sub-pixel triangles (which are 90%+ of clusters in a Nanite scene). The engine uses hardware rasterization exclusively.
- **Material sort/binning**: The resolve shader evaluates materials per-pixel via a material SSBO lookup. Nanite classifies pixels into 64x64 tiles by material, then dispatches a per-material full-screen pass that evaluates only relevant tiles. This tile-based approach is more efficient for scenes with many diverse materials.

---

## 5. Draw Call Architecture

| Aspect | This Engine | Nanite |
|---|---|---|
| **Cluster path** | `cluster_select.comp` → `cluster_cull.comp` → `VkDrawIndexedIndirectCommand[]` → hardware rasterize | DAG traversal → cluster cull → raster bin sort → software/hardware rasterize dispatch |
| **Object path** | `GPUSceneBuffer` → `scene_cull.comp` → `vkCmdDrawIndexedIndirectCount` | N/A (everything is clusters) |
| **Instance limit** | 512 (9-bit V-buffer packing) for cluster path; 8192 for object path | 16M instances |
| **Subgroup optimization** | Yes — ballot + broadcast for batched atomics in all cull shaders | Yes — similar wave-level batching |

**What matches Nanite**: The cluster pipeline (LOD select → cull → indirect draw) is the correct sequence. Subgroup-batched atomics throughout. Global vertex/index buffers.

**Gaps**:
- **Two separate paths**: The engine maintains both a cluster path (`TwoPassCuller` + `VisibilityBuffer`) and an object path (`GPUCullPass` + `GPUSceneBuffer`). Nanite has a single unified cluster path. The dual-path adds complexity and the non-cluster path becomes the bottleneck.
- **Per-draw push constants**: The V-buffer rasterization pass uses push constants per draw (`model`, `instanceId`, `triangleOffset`). Nanite avoids per-draw CPU state by fetching all instance data from SSBOs using the indirect draw's `firstInstance` field.

---

## 6. Streaming

| Aspect | This Engine | Nanite |
|---|---|---|
| **Terrain** | Tile-based streaming with tile cache, async transfer, load/unload radii (`TerrainTileCache.h`) | N/A for terrain |
| **Mesh data** | `GPUClusterBuffer` — fully resident | Page-based streaming, only visible clusters loaded, SSD-optimized |
| **Textures** | `AsyncTextureUploader` with non-blocking uploads | Virtual texturing (separate system) |
| **Virtual textures** | Implemented: 8192px virtual, 128px tiles, 2048px cache, 6 mip levels (`VirtualTextureSystem.h`) | Similar |

**Engine strengths**: The terrain tile cache (high-res tiles near camera in a 2D texture array, per-tile metadata, cooperative loading with yield callback) and virtual texture system are well-designed streaming systems. The `AsyncTransferManager` with dedicated transfer queue is the right pattern.

**Gap**: Mesh cluster data is fully resident in GPU memory. Nanite streams cluster pages on demand — the hierarchy metadata is always resident but actual vertex/index data is loaded per-page as needed. This is essential for billion-triangle scenes but not needed at the engine's current scale.

---

## 7. Shadows

| Aspect | This Engine | Nanite |
|---|---|---|
| **Approach** | 4-cascade CSM, 2048x2048 per cascade, lambda PSSM splits (`ShadowSystem.h`) | Virtual Shadow Maps (16K x 16K virtual, page-based, ~300px/m near camera) |
| **Per-cascade culling** | Compute callback before each cascade for GPU culling | Reuses cluster cull/rasterize from light perspective |
| **Dynamic lights** | Point (cubemap) + spot (2D) shadow arrays, 1024x1024, max 8 lights | VSMs for all light types |

**Gap**: Virtual Shadow Maps are tightly coupled to the cluster pipeline — they rasterize the same clusters from the light's perspective into a virtual page table. The engine's CSM is the traditional approach. VSMs would require the cluster rasterization pipeline to be reusable from arbitrary viewpoints.

---

## Summary: What's Implemented vs. Nanite

| Nanite Feature | Engine Status | Key File(s) |
|---|---|---|
| Mesh clustering (64-128 tri) | **Implemented** | `MeshClusterBuilder.h` |
| DAG hierarchy + simplification | **Implemented** (meshoptimizer, spatial grouping) | `MeshClusterBuilder::buildWithDAG()` |
| GPU DAG LOD selection | **Implemented** | `cluster_select.comp` |
| Screen-space error projection | **Implemented** | `cull_compute_common.glsl:96-111` |
| Two-pass temporal occlusion culling | **Implemented** | `TwoPassCuller.h`, `cluster_cull.comp` |
| Hi-Z pyramid | **Implemented** | `HiZSystem.h`, `hiz_downsample.comp` |
| Normal cone backface culling | **Implemented** | `cluster_cull.comp:170-178` |
| Visibility buffer | **Implemented** (32-bit) | `VisibilityBuffer.h`, `visbuf.frag` |
| Compute material resolve | **Implemented** (full PBR + material buffer) | `visbuf_resolve.comp` |
| Subgroup-batched atomics | **Implemented** | All cull shaders |
| Global vertex/index SSBOs | **Implemented** | `GPUClusterBuffer` |
| METIS graph partitioning | **Not implemented** — spatial grouping instead | — |
| Boundary-locked simplification | **Not implemented** — generic meshoptimizer | — |
| Software rasterizer | **Not implemented** | — |
| 64-bit visibility buffer | **Not implemented** — 32-bit | — |
| Material tile classification | **Not implemented** — per-pixel material lookup | — |
| Cluster streaming | **Not implemented** — fully resident | — |
| Virtual shadow maps | **Not implemented** — CSM | — |
| Top-down DAG traversal | **Implemented** — multi-pass ping-pong | `cluster_select.comp` |

---

## Remaining Gaps: Priority Order

### 1. Software Rasterizer (highest impact)
Nanite's biggest performance innovation. A compute shader performing scan-line rasterization with 64-bit atomic compare-and-swap. For sub-pixel triangles (which dominate at Nanite detail levels), this is ~3x faster than hardware rasterization because it avoids 2x2 quad overhead. Would require upgrading V-buffer to 64-bit and implementing a per-cluster size classifier to route small clusters to the software path and large clusters to hardware.

### 2. Boundary-Locked Simplification
The current meshoptimizer simplification can introduce T-junctions between adjacent LOD levels, causing visible cracks at LOD transitions. Nanite locks boundary vertices (those shared between clusters in a group) during edge collapse, guaranteeing watertight seams. This requires identifying shared boundary vertices before simplification and passing them as locked constraints.

### 3. METIS Graph Partitioning
Spatial grouping produces more boundary edges than graph partitioning, reducing the quality of the LOD hierarchy (more constrained vertices means less simplification freedom). Integrating METIS (or meshoptimizer's `meshopt_buildMeshlets` which does something similar) for initial cluster construction would improve both cluster quality and simplification ratios.

### 4. Persistent-Thread DAG Traversal
The current flat dispatch (one thread per cluster in the entire DAG) evaluates every cluster in the hierarchy regardless of whether it will be reached. Nanite uses a work-queue approach starting from root nodes, only descending into children when the parent error exceeds the threshold. For deep hierarchies this avoids significant wasted compute.

### 5. Material Tile Classification
The resolve shader evaluates materials per-pixel via SSBO lookup with texture array sampling and Cook-Torrance PBR. This works correctly but Nanite's tile classification (64x64 pixel tiles binned by material → per-material fullscreen dispatch) would reduce divergence for scenes with many distinct materials.

### 6. 64-bit V-Buffer + Instance Scalability
The 9-bit instance / 23-bit triangle packing limits the system to 512 instances. Nanite's 64-bit buffer (30-bit depth + 27-bit cluster + 7-bit triangle) supports much larger scenes and integrates depth testing into the atomic write, which is essential for software rasterization.

### 7. Cluster Streaming (scale-dependent)
Only needed when total geometry exceeds GPU memory budget. The `AsyncTransferManager` and tile cache patterns could be extended to stream cluster pages.

### 8. Virtual Shadow Maps (optional)
Would replace CSM with per-page shadow allocation reusing the cluster pipeline from the light's perspective. Only makes sense after the cluster pipeline is the primary rendering path.

---

## Conclusion

The engine has already implemented the core Nanite architecture: cluster DAG representation, GPU LOD selection via screen-space error, two-pass temporal occlusion culling, normal cone backface culling, and a visibility buffer with compute material resolve. This is significantly further along than a traditional renderer.

The remaining gaps fall into two categories: **quality** (boundary-locked simplification, METIS partitioning — needed for crack-free LOD transitions) and **performance at scale** (software rasterizer, persistent-thread traversal, 64-bit V-buffer, streaming). The software rasterizer is the single largest performance gap and the primary reason Nanite achieves its headline numbers. The simplification quality is the most important correctness gap.
