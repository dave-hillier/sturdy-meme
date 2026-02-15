# Nanite Techniques Applied to Non-Terrain Geometry

This document describes how Unreal Engine 5's Nanite virtualized geometry techniques can be applied to the non-terrain rendering systems in this engine. Each section maps a Nanite concept to our existing infrastructure, identifies what we already have, and details what needs to be built.

## Background: What Nanite Does

Nanite is a virtualized geometry system that renders arbitrarily complex meshes at near-constant GPU cost. Its core ideas are:

1. **Cluster-based mesh representation** — meshes split into ~128-triangle clusters organized in a DAG (directed acyclic graph) for hierarchical LOD
2. **GPU-driven cluster selection** — a compute shader walks the DAG and selects the appropriate LOD clusters based on screen-space error
3. **Two-phase occlusion culling** — last frame's depth culls most clusters; a second pass catches disoccluded geometry
4. **Visibility buffer rendering** — rasterize cluster ID + triangle ID per pixel, then shade in a deferred fullscreen pass
5. **Software rasterization** — clusters whose triangles project to sub-pixel size are rasterized via compute shader (avoids hardware raster inefficiency on tiny triangles)

---

## What We Already Have

| Existing System | Nanite Parallel | Location |
|---|---|---|
| GPU frustum culling (compute) | Cluster culling dispatch | `shaders/scene_cull.comp`, `GPUSceneBuffer` |
| Hi-Z depth pyramid | Occlusion culling input | `HiZSystem` |
| `vkCmdDrawIndexedIndirectCount` | GPU-driven draw submission | `SceneObjectsDrawable::recordSceneObjectsIndirect` |
| Tree impostor LOD with dither cross-fade | LOD transitions | `TreeLODSystem`, `dither_common.glsl` |
| SSBO instance data + atomic draw count | GPU scene buffer pattern | `GPUSceneBuffer` (8192 max objects) |
| Per-object AABB + bounding sphere | Bounds for culling | `Mesh::AABB`, `GPUCullObjectData` |
| Impostor Hi-Z occlusion culling | Two-phase culling (partial) | `ImpostorCullSystem`, `tree_impostor_cull.comp` |

---

## Phase 1: Mesh Clustering

### Goal
Split every static mesh into clusters of ~64–128 triangles with precomputed bounding information and a simplification error metric.

### Approach

**Offline preprocessing tool** (`tools/mesh_cluster`):

1. Load mesh vertices and indices (from our `Mesh` class vertex/index format)
2. Run graph-based partitioning (e.g. METIS or a simple greedy spatial partitioning) to group triangles into clusters of 64–128 triangles
3. For each cluster, compute:
   - Tight AABB and bounding sphere (for culling)
   - Normal cone (for backface cluster culling)
   - Maximum simplification error in object-space (used for LOD selection)
4. Output a `.cluster` file with the cluster metadata and reordered index buffer

**Runtime data structures**:

```cpp
struct MeshCluster {
    glm::vec4 boundingSphere;   // xyz center, w radius
    glm::vec4 normalCone;       // xyz cone axis, w cos(half-angle)
    glm::vec4 aabbMin;
    glm::vec4 aabbMax;
    float maxError;             // object-space simplification error
    uint32_t firstIndex;        // into global index buffer
    uint32_t indexCount;         // triangle count * 3
    uint32_t lodLevel;          // hierarchy depth
};
```

**What this replaces**: Currently `GPUCullObjectData` operates per-object (one entry per `Renderable`). After clustering, culling operates per-cluster — the same compute shader pattern but with finer granularity.

### Applicable Systems

| System | Current Geometry | Cluster Benefit |
|---|---|---|
| Scene objects (ECS) | Individual meshes, 1 cull entry each | Fine-grained culling for complex meshes |
| Rocks (`ScatterSystem`) | Procedural `createRock()` | Hundreds of instances; cluster LOD removes distant detail |
| Trees (`TreeSystem`) | Branch meshes + leaf instances | Branch geometry benefits from cluster LOD instead of hard impostor switch |
| Catmull-Clark (`CatmullClarkSystem`) | Subdivision surfaces | High poly output; clusters enable progressive detail |
| Skinned characters | Skeletal meshes | Partial — clusters work for static parts; animated parts need per-frame bounds update |

---

## Phase 2: Cluster DAG for Hierarchical LOD

### Goal
Build a DAG where groups of clusters at level N simplify into a parent cluster at level N+1. The GPU walks the DAG at runtime and selects the coarsest level that satisfies a screen-space error threshold.

### Approach

**Offline** (extend `tools/mesh_cluster`):

1. Starting from the leaf clusters (full detail), group spatially adjacent clusters into sets of 2–4
2. Simplify the combined geometry of each group (edge collapse with quadric error metric) down to a single cluster (~64–128 triangles)
3. Record parent→child relationships and the max error at each level
4. Repeat until one root cluster remains (or error exceeds a threshold)
5. Store the DAG edges and per-node error in the `.cluster` file

**Runtime DAG node** (extends `MeshCluster`):

```cpp
struct ClusterDAGNode {
    MeshCluster cluster;
    uint32_t parentIndex;           // UINT32_MAX for root
    uint32_t firstChildIndex;       // into children array
    uint32_t childCount;            // 0 for leaf nodes
    float parentError;              // error of parent cluster
};
```

**GPU LOD selection** (`shaders/cluster_select.comp`):

```glsl
// For each DAG node, determine if this node should render:
// - Its own error projected to screen is acceptable (< threshold)
// - Its parent's error projected to screen is NOT acceptable (> threshold)
// This is the "cut" through the DAG.
float screenError = projectError(node.cluster.maxError, node.cluster.boundingSphere, viewProj, screenHeight);
float parentScreenError = projectError(node.parentError, node.cluster.boundingSphere, viewProj, screenHeight);

bool shouldRender = (screenError <= errorThreshold) && (parentScreenError > errorThreshold);
// Root nodes: parentScreenError is infinite → always pass parent test
```

### Integration with Existing LOD Systems

**Trees**: The current hard switch between full geometry and billboard impostor (`TreeLODState::Level`) would become a smooth continuum. The cluster DAG naturally produces coarser representations at distance. Impostors remain as the ultimate LOD level beyond the DAG's coarsest cluster — the impostor becomes the "root" of the DAG for each tree archetype.

**Rocks**: Currently rendered at full detail regardless of distance. The DAG gives rocks automatic distance-based simplification with no additional per-system logic.

**Scene objects**: The same system handles all static meshes uniformly, replacing the current per-object `GPUCullObjectData` with per-cluster entries.

---

## Phase 3: Two-Phase Occlusion Culling

### Goal
Use last frame's depth buffer to reject occluded clusters before rasterization, then catch newly-visible clusters in a second pass.

### Approach

We already have the `HiZSystem` producing a depth pyramid. The current `scene_cull.comp` has a `enableHiZ` uniform but only performs frustum culling. This phase activates occlusion culling.

**Pass 1 — Cull against previous frame's Hi-Z**:

1. After cluster LOD selection, run `cluster_cull.comp`:
   - Frustum test (existing sphere + AABB logic)
   - Backface cluster culling using normal cone vs camera direction
   - **Hi-Z occlusion test**: project cluster bounding sphere to screen-space rect, sample the Hi-Z pyramid at the appropriate mip, compare max depth
2. Surviving clusters go into the indirect draw buffer
3. Rasterize visible clusters → update depth buffer → rebuild Hi-Z pyramid

**Pass 2 — Catch disoccluded geometry**:

1. Re-run culling on clusters that were **occluded in pass 1** but test against the **new** Hi-Z from pass 1's rasterization
2. Clusters that now pass get appended to the draw buffer
3. Rasterize the additional clusters

**Integration**: This extends the existing `FrameGraph` with two new nodes at the start of the HDR stage:

```
Level 0: Compute (terrain LOD, grass sim, weather, cluster select + cull pass 1)
Level 1: Shadow, Froxel, WaterGBuffer, Cluster raster pass 1 (parallel)
Level 1.5: Hi-Z rebuild + Cluster cull pass 2
Level 2: Cluster raster pass 2 + HDR main scene
```

**What changes in existing code**:
- `HiZSystem` currently runs in the post stage. It needs to also run mid-frame after pass 1
- `scene_cull.comp` gains Hi-Z sampling logic (the binding `BINDING_SCENE_CULL_HIZ` already exists but is unused)
- `GPUSceneBuffer` grows a "deferred" indirect buffer for pass 2 results

---

## Phase 4: Visibility Buffer Rendering

### Goal
Replace per-object forward shading with a two-step approach: rasterize only IDs, then shade in a fullscreen compute/fragment pass. This decouples geometric complexity from shading cost.

### Approach

**Step 1 — Visibility pass**:
- Render all visible clusters into a uint32 render target (R32_UINT or R32G32_UINT)
- Each pixel stores: `clusterID` (upper bits) + `triangleID` (lower bits)
- Vertex shader is minimal (just transform), fragment shader writes the ID
- Depth test still applies (existing depth buffer from Hi-Z pipeline)

**Step 2 — Material resolve pass** (`shaders/visibility_resolve.comp`):
- Fullscreen compute dispatch reads the visibility buffer
- For each pixel, look up cluster → mesh → material
- Reconstruct barycentric coordinates from triangle vertices + screen position
- Interpolate UVs, normals, tangents
- Sample material textures and evaluate the PBR lighting model (reuse existing `lighting_common.glsl`)

**What this replaces**:
- The current `shader.vert`/`shader.frag` pipeline with push constants per object
- Material sorting in `SceneObjectsDrawable` (no longer needed — all materials resolved in one compute pass)
- Per-object descriptor set switches (material textures accessed via bindless array)

**Prerequisite — Bindless textures**:
- Requires `VK_EXT_descriptor_indexing` (widely supported)
- Create a single large descriptor array of all material textures
- Each cluster references a material index; the resolve shader indexes into the texture array
- This replaces the current pattern of per-material descriptor set binding

### Integration with Existing Shading

The visibility buffer approach initially applies only to **opaque static geometry** (scene objects, rocks, Catmull-Clark surfaces, tree branches). Systems that need specialized vertex processing continue using their existing pipelines:

| System | Visibility Buffer? | Reason |
|---|---|---|
| Scene objects (static) | Yes | Standard opaque meshes |
| Rocks | Yes | Static procedural meshes |
| Tree branches | Yes | Static per-frame (wind applied in resolve) |
| Tree leaves | No | Instanced billboards with per-leaf attributes |
| Grass | No | Compute-generated, stochastic density |
| Water | No | FFT displacement, custom shading model |
| Skinned characters | Possible later | Needs skeleton transform in visibility pass |
| Particles/weather | No | Alpha blended, low poly |
| Tree impostors | No | Billboard with atlas sampling |

---

## Phase 5: Software Rasterization for Small Clusters

### Goal
Clusters that project to very small screen area (triangles < ~4 pixels) are inefficient to rasterize with hardware (poor quad occupancy). A compute shader can rasterize these more efficiently.

### Approach

During cluster culling, classify each visible cluster:
- **Large clusters** (projected area > threshold): hardware rasterize via indirect draw
- **Small clusters** (projected area < threshold): software rasterize via compute shader

**Software rasterizer** (`shaders/cluster_sw_raster.comp`):
1. Each workgroup processes one small cluster
2. Transform vertices to screen space
3. For each triangle, compute 2D bounding box
4. Rasterize pixels using edge function tests
5. Atomic depth test + write to visibility buffer (`imageAtomicMin` on a uint64 image encoding depth + visibility ID)

**Why this matters for our scene**: Rocks and tree branches at distance produce many sub-pixel triangles. Currently these burn rasterizer throughput for minimal visual contribution. The software path handles them in compute alongside the cluster culling passes.

---

## Phase 6: Per-System Application Details

### 6.1 Rocks (`ScatterSystem`)

**Current state**: Procedural `createRock()` meshes, rendered as individual `Renderable` objects with CPU frustum culling and per-object draw calls.

**Nanite application**:
1. Pre-cluster each rock archetype's mesh during build (different seed/subdivision variants)
2. Per-instance cluster DAG selection in compute (shared archetype DAG, per-instance transform)
3. Cluster-level occlusion culling via Hi-Z eliminates hidden rocks behind terrain or other rocks
4. Rocks at distance automatically simplify through the DAG — no need for explicit rock LOD code
5. Software raster for distant rock fields where triangles are sub-pixel

**Expected improvement**: Rocks currently have no LOD. With the DAG, a rock at 500m renders perhaps 4-8 triangles instead of hundreds. With occlusion culling, rocks behind terrain ridges are fully skipped.

### 6.2 Trees (`TreeSystem` + `TreeLODSystem`)

**Current state**: Two discrete LOD levels — full branch geometry or billboard impostor. Cross-fade uses IGN dithering. Leaves have GPU frustum culling with spatial cells.

**Nanite application**:
1. Cluster the branch mesh per tree archetype
2. Build a DAG hierarchy per archetype with progressive simplification
3. At the coarsest DAG level (just a few clusters), transition to the existing impostor system
4. The hard Full→Impostor switch becomes: Full clusters → Coarse clusters → Impostor
5. Leaf instances remain in their existing compute-culled pipeline (they're points/billboards, not mesh geometry)
6. Per-tree cluster selection allows partial trees to simplify — far side of a tree canopy can be coarser than the near side

**Integration detail**: `TreeLODState` currently tracks `FullDetail / Impostor / Blending`. Add a `Clustered` level that uses the DAG. The blend factor drives the DAG error threshold — as blend increases, the error threshold rises and coarser clusters are selected, until the impostor takes over.

### 6.3 Scene Objects (ECS Entities)

**Current state**: `GPUSceneBuffer` collects up to 8192 objects. Compute shader frustum culls and emits indirect draw commands. Push constants carry per-object PBR params.

**Nanite application**:
1. Replace per-object entries with per-cluster entries in `GPUSceneBuffer`
2. The `GPUCullObjectData` struct adds cluster-specific fields (normal cone, DAG parent/child links, error metric)
3. The `MAX_GPU_SCENE_OBJECTS` limit (8192) becomes a cluster limit — increase to 64K–256K
4. The compute cull shader gains the DAG traversal, normal cone culling, and Hi-Z occlusion test
5. Per-instance data (`GPUSceneInstanceData`) stays as-is — clusters share their object's instance data via `objectIndex`
6. With visibility buffer rendering, push constants and per-material descriptor switching are eliminated — the resolve pass handles all materials

### 6.4 Catmull-Clark Subdivision Surfaces

**Current state**: CPU or GPU subdivision producing high-poly output rendered conventionally.

**Nanite application**: Subdivision surfaces naturally produce massive triangle counts. Clustering the output and building a DAG means the subdivision can target maximum quality, and the DAG handles LOD. At distance, a subdivided mesh might render as just its base cage triangles.

### 6.5 Skinned Characters

**Current state**: Skeletal animation with bone matrices, individual draw calls.

**Nanite application** (partial):
- Cluster the mesh, but cluster bounds must be updated per-frame after skinning (compute pass)
- Simpler alternative: apply Nanite clustering only to **static attachments** (armor, weapons, accessories) while the body mesh uses a traditional LOD chain
- The visibility buffer can include skinned clusters if the resolve shader applies bone transforms during attribute interpolation

---

## Phase 7: Streaming and Memory Management

### Goal
Virtualized geometry means only resident clusters need GPU memory. Clusters are streamed on demand.

### Approach

1. **Cluster pool**: A large pre-allocated GPU buffer holding cluster index data. Clusters are loaded/evicted like virtual texture tiles.
2. **Residency feedback**: After cluster selection, the compute shader flags which clusters are needed. A readback pass identifies missing clusters for async loading.
3. **Integration with `AsyncTransferManager`**: Use the existing dedicated transfer queue for background cluster uploads.
4. **Fallback**: If a cluster isn't resident, render its parent (coarser LOD). The DAG guarantees a parent is always available (loaded eagerly at init).

This mirrors how `TerrainTileCache` and `VirtualTextureSystem` manage terrain tile residency — the same pattern applied to mesh geometry.

---

## Implementation Order and Dependencies

```
Phase 1: Mesh Clustering (offline tool)
   ↓
Phase 2: Cluster DAG + GPU LOD Selection (offline tool + compute shader)
   ↓                              ↓
Phase 3: Two-Phase Occlusion    Phase 6.1-6.2: Apply to rocks & trees
   ↓                              ↓
Phase 4: Visibility Buffer      Phase 6.3-6.4: Apply to scene objects & subdivision
   ↓
Phase 5: Software Rasterization (optimization, can defer)
   ↓
Phase 7: Streaming (optimization, can defer)
```

Phases 1-2 provide the biggest architectural shift — all downstream phases build on the cluster DAG representation. Phases 3 and 6 can proceed in parallel. Phases 5 and 7 are optimizations that can be deferred until the core pipeline is working.

Each phase produces a working renderer. At the end of Phase 1, we have finer-grained culling. After Phase 2, we have automatic LOD. After Phase 3, we have occlusion culling. The system improves incrementally.

---

## Vulkan Feature Requirements

| Feature | Used For | Availability |
|---|---|---|
| `VK_EXT_descriptor_indexing` | Bindless textures in visibility resolve | Vulkan 1.2 core |
| `vkCmdDrawIndexedIndirectCount` | Variable-length indirect draws | Already used |
| `VK_KHR_shader_atomic_int64` | Software rasterizer depth test | Wide support on desktop |
| `VK_EXT_shader_image_atomic_int64` | Visibility buffer atomic writes | Phase 5 only |
| Compute shader SSBOs | DAG traversal, cluster culling | Already used throughout |

No features beyond what desktop Vulkan 1.2+ provides. The engine already uses indirect count draws and compute SSBOs extensively.
