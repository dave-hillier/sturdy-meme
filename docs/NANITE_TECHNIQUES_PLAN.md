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

## Current State

### What's built

| Component | Status | Location |
|---|---|---|
| Mesh clustering (64-tri clusters) | Built | `MeshClusterBuilder.h/.cpp` |
| Cluster DAG with meshoptimizer simplification | Built | `MeshClusterBuilder::buildWithDAG()` |
| GPU cluster buffer (global vertex/index SSBOs) | Built | `GPUClusterBuffer` |
| GPU DAG LOD selection compute shader | Built | `shaders/cluster_select.comp` |
| Two-pass occlusion culler (LOD select + cull + indirect) | Built | `TwoPassCuller.h/.cpp`, `shaders/cluster_cull.comp` |
| Normal cone backface culling | Built | `cluster_cull.comp:170-178` |
| V-buffer render target (R32G32_UINT) | Built | `VisibilityBuffer.h/.cpp` |
| V-buffer raster pipeline (visbuf.vert/frag) | Built | `shaders/visbuf.vert`, `shaders/visbuf.frag` |
| Compute material resolve (PBR + lights) | Built | `shaders/visbuf_resolve.comp` |
| Material texture array (sampler2DArray) | Built | `VisibilityBuffer::buildMaterialTextureArray()` |
| GPU material buffer (per-material PBR data) | Built | `GPUMaterialBuffer` |
| Hi-Z depth pyramid | Built | `HiZSystem`, `shaders/hiz_downsample.comp` |

### What's broken: the integration

The individual subsystems exist but are not properly integrated into the rendering pipeline. The current state is a broken hybrid:

1. **HDR pass** (priority 30) renders sky, terrain, rocks, trees, grass, water, weather, debug lines — everything. It begins an `R16G16B16A16_SFLOAT` render pass, clears color+depth, and draws all registered `IHDRDrawable` implementations inline.

2. **V-buffer raster** (priority 31) renders ECS scene objects into a **separate** R32G32_UINT render target with its own render pass and framebuffer. It uses `VisBufferPasses::executeRasterPass()` which does immediate per-cluster draws with push constants.

3. **V-buffer resolve** (priority 28) runs a compute shader that reads the V-buffer, reconstructs geometry, evaluates PBR, and tries to `imageStore` the result **into the HDR color image**. This requires layout transitions (SHADER_READ_ONLY → GENERAL → SHADER_READ_ONLY) and depth comparison against the HDR depth buffer.

4. `SceneObjectsDrawable` has a `visBufferActive` flag that skips ECS objects in the HDR pass when V-buffer is enabled. But rocks, trees, and everything else still render in HDR.

5. `TwoPassCuller` is built but **not wired** into the rendering pipeline. V-buffer raster does immediate draws instead of consuming indirect draw buffers from the culler.

**Problems:**
- The resolve compute shader writes into an image owned by a different render pass, requiring fragile layout transition hacks
- Two separate depth buffers (V-buffer owns one, HDR owns one) with no shared depth testing
- Only ECS scene objects go through V-buffer; rocks and other static meshes bypass it entirely
- TwoPassCuller output (indirect draw buffers) is not consumed by anything
- Per-cluster push constants instead of GPU-driven indirect draws

---

## What We Already Have (pre-Nanite)

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

### Status: DONE

`MeshClusterBuilder` implements this. `build()` creates flat clusters; `buildWithDAG()` builds the full hierarchy. `GPUClusterBuffer` manages global vertex/index SSBOs.

### Runtime data structures

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

### Status: DONE (offline tool + compute shader)

`MeshClusterBuilder::buildWithDAG()` builds the hierarchy. `cluster_select.comp` does GPU LOD selection. `TwoPassCuller` orchestrates the dispatch.

### Runtime DAG node

```cpp
struct ClusterDAGNode {
    MeshCluster cluster;
    uint32_t parentIndex;           // UINT32_MAX for root
    uint32_t firstChildIndex;       // into children array
    uint32_t childCount;            // 0 for leaf nodes
    float parentError;              // error of parent cluster
};
```

### GPU LOD selection (`shaders/cluster_select.comp`)

```glsl
float screenError = projectError(node.cluster.maxError, node.cluster.boundingSphere, viewProj, screenHeight);
float parentScreenError = projectError(node.parentError, node.cluster.boundingSphere, viewProj, screenHeight);
bool shouldRender = (screenError <= errorThreshold) && (parentScreenError > errorThreshold);
```

### Integration with Existing LOD Systems

**Trees**: The current hard switch between full geometry and billboard impostor (`TreeLODState::Level`) would become a smooth continuum. The cluster DAG naturally produces coarser representations at distance. Impostors remain as the ultimate LOD level beyond the DAG's coarsest cluster — the impostor becomes the "root" of the DAG for each tree archetype.

**Rocks**: Currently rendered at full detail regardless of distance. The DAG gives rocks automatic distance-based simplification with no additional per-system logic.

**Scene objects**: The same system handles all static meshes uniformly, replacing the current per-object `GPUCullObjectData` with per-cluster entries.

---

## Phase 3: Two-Phase Occlusion Culling

### Goal
Use last frame's depth buffer to reject occluded clusters before rasterization, then catch newly-visible clusters in a second pass.

### Status: Built (`TwoPassCuller`), NOT INTEGRATED

`TwoPassCuller` implements the full two-pass pipeline (LOD select → cull pass 1 → Hi-Z rebuild → cull pass 2) and produces indirect draw buffers. But nothing consumes those buffers yet — `VisBufferPasses` does immediate per-cluster draws with push constants instead.

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

## Phase 4: Visibility Buffer Integration

### Goal
Make the V-buffer the primary rendering path for opaque static geometry. The V-buffer raster and resolve replace the forward shading that currently happens for these objects in the HDR pass. Non-V-buffer systems (terrain, water, grass, sky, particles) continue rendering in the HDR pass.

### Status: Partially built, poorly integrated

The V-buffer raster pipeline, resolve compute shader, material texture array, and GPU material buffer all work individually. The integration into the frame graph is the problem — the V-buffer currently runs as a secondary overlay that tries to composite into the HDR pass's render targets via compute `imageStore`.

### Target architecture

The V-buffer raster pass and the HDR render pass share the same depth buffer. The V-buffer rasters first into its uint target while also writing depth. Then the HDR render pass begins with `loadOp = eLoad` on depth (preserving V-buffer depth writes) and `loadOp = eClear` on color. Non-V-buffer drawables render into HDR as before. Finally the resolve compute shader reads the V-buffer and writes resolved materials into the HDR color target.

This means V-buffer objects participate in depth testing against HDR objects naturally — no separate depth comparison needed in the resolve shader.

### Frame graph pass ordering

```
Compute [100] ──> Shadow [50], Froxel [50], WaterGBuffer [40]
                      │
              ┌───────┴────────┐
              v                v
     ClusterSelect [92]   (other compute)
              │
              v
     ClusterCull [91]
              │
              v
     VisBufferRaster [35]     ← writes uint V-buffer + shared depth
              │
              v
     HDR [30]                  ← loads depth (preserves V-buffer writes), clears color
              │                  renders: sky, terrain, rocks*, trees*, grass, water, weather
              v
     VisBufferResolve [28]     ← compute: reads V-buffer, imageStore to HDR color
              │
              v
     HiZ [15], SSR [20], Bloom [10], etc.
              │
              v
     PostProcess [0]
```

*Rocks and trees move to V-buffer path incrementally (see Phase 6).

### Step-by-step integration plan

**Step 1: Share the depth buffer**

The V-buffer raster pass and the HDR pass must use the **same** depth image. Currently they each own separate depth attachments.

- `PostProcessSystem` owns the HDR depth image (`hdrDepthImage`, D32_SFLOAT)
- `VisibilityBuffer` owns a separate depth image (`depthImage_`, D32_SFLOAT)
- Change: `VisibilityBuffer` stops creating its own depth image. Instead, it receives the HDR depth image/view at init (or via a setter) and uses it in its framebuffer
- The V-buffer render pass attachment description for depth uses `loadOp = eClear`, `storeOp = eStore`, `finalLayout = eDepthStencilAttachmentOptimal` (not READ_ONLY, because HDR needs to continue writing to it)
- The HDR render pass depth attachment changes to `loadOp = eLoad` (not eClear) so V-buffer depth writes are preserved
- Both passes write to the same depth image — standard Vulkan depth testing handles visibility between V-buffer and HDR objects

**Step 2: V-buffer raster runs before HDR**

Already the case (priority 31 vs 30) but the dependency graph needs updating:

- `VisBufferRaster` depends on: Compute, Shadow (for shadow maps if needed)
- `HDR` depends on: `VisBufferRaster` (needs shared depth populated), Shadow, Froxel, WaterGBuffer
- This ensures V-buffer depth writes are complete before HDR begins

In `FrameGraphBuilder.cpp`:
```cpp
// V-buffer raster must complete before HDR (shared depth buffer)
if (visBufferIds.raster != FrameGraph::INVALID_PASS) {
    frameGraph.addDependency(computeIds.compute, visBufferIds.raster);
    frameGraph.addDependency(visBufferIds.raster, hdr);  // HDR loads V-buffer depth
}
```

**Step 3: HDR clears color only, loads depth**

The HDR render pass in `PostProcessSystem::createRenderPass()` currently clears both color and depth. When V-buffer is active:

- Color: `loadOp = eClear` (still clear — V-buffer objects haven't written color yet, only IDs)
- Depth: `loadOp = eLoad` (preserve V-buffer raster depth writes)

This can be a separate render pass variant, or the `VisibilityBuffer` can set a flag that `PostProcessSystem` checks when creating the render pass.

HDR drawables (sky, terrain, grass, water, rocks*) render as before. Their fragments are depth-tested against V-buffer geometry naturally via the shared depth buffer — no special logic needed.

**Step 4: Resolve writes to HDR color**

The resolve compute shader runs after HDR (priority 28). At this point:
- HDR color is in `SHADER_READ_ONLY_OPTIMAL` (set by HDR render pass `finalLayout`)
- V-buffer uint image is in `SHADER_READ_ONLY_OPTIMAL` (set by V-buffer render pass `finalLayout`)
- Shared depth is in `DEPTH_STENCIL_READ_ONLY_OPTIMAL` (set by HDR render pass `finalLayout`)

The resolve pass needs:
- V-buffer image: transition to `GENERAL` for storage image read
- HDR color image: transition to `GENERAL` for `imageStore`
- Depth: already in `READ_ONLY`, just needs memory dependency for sampling

After dispatch:
- HDR color: transition back to `SHADER_READ_ONLY_OPTIMAL` for post-processing

The resolve shader no longer needs to compare V-buffer depth against HDR depth — the shared depth buffer already resolved visibility during rasterization. The depth comparison code in `visbuf_resolve.comp` can be removed.

**Step 5: Remove duplicate rendering**

Once depth is shared, `SceneObjectsDrawable::visBufferActive` correctly skips ECS objects in HDR. V-buffer resolve writes their shaded output to the HDR color image. Other HDR drawables (sky, terrain, etc.) render normally and coexist via shared depth.

**Step 6: Wire TwoPassCuller indirect draws**

Replace the immediate per-cluster draws in `VisBufferPasses::executeRasterPass()` with indirect draws from `TwoPassCuller`:

- Add `ClusterSelect` and `ClusterCull` as compute passes in the frame graph (before `VisBufferRaster`)
- `TwoPassCuller::recordLODSelection()` → populates selected cluster buffer
- `TwoPassCuller::recordCullPass()` → populates indirect draw buffer
- `VisBufferRaster` binds the cluster vertex/index buffer and calls `vkCmdDrawIndexedIndirectCount` using TwoPassCuller's indirect buffer + draw count buffer
- Push constants are eliminated — the vertex shader reads instance data from SSBO using `gl_InstanceIndex` or `gl_DrawID`

### What this replaces

- The current `shader.vert`/`shader.frag` pipeline with push constants per object (for V-buffer objects)
- Material sorting in `SceneObjectsDrawable` (no longer needed — all materials resolved in one compute pass)
- Per-object descriptor set switches (material textures accessed via bindless array)
- The separate V-buffer depth image
- The depth comparison hack in `visbuf_resolve.comp`
- Per-cluster immediate draw calls (replaced by indirect draws from TwoPassCuller)

### Prerequisite — Bindless textures

- Requires `VK_EXT_descriptor_indexing` (widely supported)
- Create a single large descriptor array of all material textures
- Each cluster references a material index; the resolve shader indexes into the texture array
- This replaces the current pattern of per-material descriptor set binding

### V-buffer vs HDR pass ownership

| System | Renders in | Reason |
|---|---|---|
| Scene objects (ECS static) | V-buffer | Standard opaque meshes |
| Rocks | V-buffer (Phase 6.1) | Static procedural meshes |
| Tree branches | V-buffer (Phase 6.2) | Static per-frame (wind applied in resolve) |
| Catmull-Clark surfaces | V-buffer (Phase 6.4) | High poly, benefits from cluster LOD |
| Sky | HDR pass | Fullscreen, no geometry benefit |
| Terrain | HDR pass | Own LOD system (CBT), heightmap-based |
| Grass | HDR pass | Compute-generated, stochastic density |
| Water | HDR pass | FFT displacement, custom shading model |
| Tree leaves | HDR pass | Instanced billboards with per-leaf attributes |
| Tree impostors | HDR pass | Billboard with atlas sampling |
| Skinned characters | HDR pass (for now) | Needs skeleton transform |
| Particles/weather | HDR pass | Alpha blended, low poly |

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

**Current state**: Procedural `createRock()` meshes, rendered as individual `Renderable` objects via `SceneObjectsDrawable` in the HDR pass. Own descriptor sets for rock textures. No LOD.

**Migration to V-buffer**:
1. Cluster each rock archetype's mesh via `MeshClusterBuilder::buildWithDAG()`
2. Upload clusters to `GPUClusterBuffer` alongside scene object clusters
3. Add rock instances to the V-buffer instance buffer (transform + material index)
4. Rock materials go into `GPUMaterialBuffer`; rock textures into the material texture array
5. Remove rock rendering from `SceneObjectsDrawable` (delete the `resources_.rocks` block)
6. Rocks now participate in cluster LOD selection and two-pass occlusion culling automatically

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

**Current state**: `GPUSceneBuffer` collects up to 8192 objects. Compute shader frustum culls and emits indirect draw commands. Push constants carry per-object PBR params. When `visBufferActive` is true, ECS objects skip the HDR pass and render through V-buffer instead.

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
Phase 1: Mesh Clustering .......................... DONE
   ↓
Phase 2: Cluster DAG + GPU LOD Selection .......... DONE
   ↓
Phase 4: V-Buffer Integration ..................... IN PROGRESS
   Step 1: Share depth buffer between V-buffer and HDR
   Step 2: Fix frame graph ordering (V-buffer raster → HDR → resolve)
   Step 3: HDR loads depth instead of clearing it
   Step 4: Proper resolve barriers (V-buffer + HDR color)
   Step 5: Remove duplicate rendering paths
   Step 6: Wire TwoPassCuller indirect draws
   ↓                              ↓
Phase 3: Two-Phase Occlusion    Phase 6.1-6.2: Apply to rocks & trees
   ↓                              ↓
Phase 6.3-6.4: Scene objects    Phase 6.3-6.4: Catmull-Clark & subdivision
   ↓
Phase 5: Software Rasterization (optimization, can defer)
   ↓
Phase 7: Streaming (optimization, can defer)
```

Phase 4 integration is the current blocker. Steps 1-3 (shared depth) are the critical path — once V-buffer and HDR share a depth buffer, the fragile layout transition hacks and depth comparison code in the resolve shader go away. Step 6 (TwoPassCuller) eliminates the per-cluster push constant draws.

Each step produces a working renderer. After step 3, V-buffer objects are visible and depth-correct against HDR objects. After step 6, rendering is fully GPU-driven.

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
