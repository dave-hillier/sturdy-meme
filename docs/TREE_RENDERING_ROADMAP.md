# Tree Rendering Optimization Roadmap

Scaling the tree rendering system to support 1M+ tree instances, based on techniques from SpeedTree, Unreal Engine 5, Nanite, and AAA game development.

## Core Philosophy: Screen-Space Error Budget

Rather than using fixed distance thresholds for LOD selection, this roadmap adopts a **screen-space error metric** approach inspired by Nanite and modern terrain rendering:

- **Resolution-independent**: Automatically adapts to 1080p, 4K, or any resolution
- **FOV-aware**: Narrow FOV (zoom) gets more detail, wide FOV gets less
- **Budget-constrained**: Global triangle/instance budget ensures consistent performance
- **Perceptually correct**: 1 pixel of error looks the same regardless of distance

### Screen-Space Error Formula

```
screenError = worldError * screenHeight / (2 * distance * tan(fov/2))
```

Where:

- `worldError`: Size of geometric detail being simplified (e.g., branch thickness)
- `screenHeight`: Viewport height in pixels
- `distance`: Distance from camera to object
- `fov`: Vertical field of view

A tree at 100m with 1m canopy detail on a 1080p screen at 90° FOV:

```
screenError = 1.0 * 1080 / (2 * 100 * tan(45°)) = 1080 / 200 = 5.4 pixels
```

## Architectural Constraint: Multi-Archetype Support

The system must support multiple tree archetypes (oak, pine, ash, aspen, etc.) with different:

- **Geometry**: Branch structure, triangle counts per LOD
- **Textures**: Bark albedo/normal, leaf textures
- **LOD parameters**: World-space error bounds, impostor sizing
- **Leaf types**: Different leaf shapes and densities

This is a **first-class requirement** that must be preserved across all optimization phases.

### Current Multi-Archetype Implementation

| Component | How Archetypes Are Handled |
|-----------|---------------------------|
| Tree instances | `archetypeIndex` field per tree |
| Leaf culling | `leafTypeIndex` partitions output buffer by type |
| Impostor atlas | Array texture with one layer per archetype |
| Descriptors | Per-type descriptor sets for bark/leaf textures |
| Draw calls | One indirect draw per leaf type (4 types currently) |

### Preserving Multi-Archetype Support

Each phase must maintain archetype separation:

```glsl
// CORRECT: Archetype-aware data structures
struct TreeInstanceGPU {
    vec4 positionAndScale;
    uint archetypeIndex;      // Which tree type (oak=0, pine=1, etc.)
    uint leafTypeIndex;       // Which leaf texture to use
    // ...
};

// CORRECT: Per-archetype LOD parameters
struct ArchetypeLODData {
    float worldErrorLOD0;     // Different per archetype
    float worldErrorLOD1;
    uint trianglesLOD0;       // Oak has different tri count than pine
    uint trianglesLOD1;
};

// WRONG: Single global LOD threshold (breaks multi-archetype)
float globalLODDistance = 100.0;  // Don't do this
```

### Output Buffer Partitioning

Culled output buffers must remain partitioned by archetype/leaf type to enable per-type draw calls:

```
Output Buffer Layout:
+------------------+------------------+------------------+------------------+
| Oak leaves       | Ash leaves       | Aspen leaves     | Pine needles     |
| (type 0)         | (type 1)         | (type 2)         | (type 3)         |
+------------------+------------------+------------------+------------------+
     ^                   ^                  ^                  ^
     |                   |                  |                  |
drawCmds[0].firstInstance                                drawCmds[3].firstInstance
```

Each draw command uses its own descriptor set with the appropriate leaf texture.

### Validation Checklist

When implementing any phase, verify:

- [ ] Archetype index flows through the entire pipeline
- [ ] Per-archetype LOD parameters are used (not global thresholds)
- [ ] Output buffers remain partitioned by type
- [ ] Draw calls remain separate per texture type
- [ ] Impostor atlas indexing preserves archetype layer
- [ ] Budget accounting uses per-archetype triangle counts

---

## Architectural Constraint: Lighting & Atmosphere Integration

Trees must integrate with the engine's lighting and atmospheric systems to avoid visual discontinuities. All LOD levels and shader variants must honor these systems consistently.

### Required Systems Integration

| System | Purpose | Tree Integration |
|--------|---------|------------------|
| Cascaded Shadow Maps | Sun shadows (4 cascades) | Sample `shadowMapArray` in all tree shaders |
| Aerial Perspective | Distance fog/haze | Apply `applyAerialPerspective()` based on view distance |
| Volumetric Fog (Froxels) | Local fog volumes | Sample froxel grid for inscatter/transmittance |
| Cloud Shadows | Soft cloud shadow projection | Sample cloud shadow map if enabled |
| Atmosphere LUTs | Sky color, transmittance | Use precomputed LUTs for consistent sky lighting |
| Time of Day | Sun position, color, intensity | Read from `ubo.sunDirection`, `ubo.sunColor` |

### Current Implementation Status

| Shader | Shadows | Aerial Perspective | Froxels | Cloud Shadows |
|--------|---------|-------------------|---------|---------------|
| `tree.frag` (branches) | Yes | Yes | No | No |
| `tree_leaf.frag` | Yes | Partial | No | No |
| `tree_impostor.frag` | Yes | **Missing** | No | No |
| `tree_impostor_shadow.frag` | N/A | N/A | N/A | N/A |

### Atmospheric Consistency Requirements

All tree shaders must apply aerial perspective to match distant terrain and other objects:

```glsl
// REQUIRED in all tree fragment shaders
#include "atmosphere_common.glsl"

void main() {
    // ... lighting calculations ...

    // Apply aerial perspective for distance fog
    vec3 cameraToFrag = fragWorldPos - ubo.cameraPosition.xyz;
    float viewDist = length(cameraToFrag);
    vec3 viewDir = normalize(cameraToFrag);
    vec3 sunDir = normalize(-ubo.sunDirection.xyz);

    color = applyAerialPerspective(
        color,
        ubo.cameraPosition.xyz,
        viewDir,
        viewDist,
        sunDir,
        ubo.sunColor.rgb
    );

    outColor = vec4(color, 1.0);
}
```

### LOD Transition Lighting Match

When cross-fading between geometry and impostors, lighting must match closely to avoid visible seams:

1. **Same sun direction/color**: Both LODs read from `ubo.sunDirection`, `ubo.sunColor`
2. **Same shadow sampling**: Both use `calculateCascadedShadow()` with same parameters
3. **Same aerial perspective**: Both apply `applyAerialPerspective()` at same world position
4. **Matched ambient**: Both use `ubo.ambientColor` for sky contribution

### Impostor Lighting Considerations

Impostors are pre-rendered captures that don't receive real-time lighting changes. To maintain consistency:

1. **Neutral capture lighting**: Render impostor atlas with neutral directional light
2. **Runtime relighting**: Apply current sun direction/color in impostor shader
3. **Normal-based shading**: Use captured normal map for directional response
4. **Atmosphere post-apply**: Apply aerial perspective after impostor color lookup

```glsl
// tree_impostor.frag - proper atmosphere integration
vec3 color = impostor.rgb;  // Base captured color

// Relight based on current sun
color *= calculateRelighting(worldNormal, sunDir, sunColor);

// Apply current atmosphere (CRITICAL for consistency)
color = applyAerialPerspective(color, cameraPos, viewDir, viewDist, sunDir, sunColor);
```

### Shader Variant Atmospheric Requirements

When creating shader LOD variants (Phase 4), all variants must include atmosphere:

| Variant | Lighting | Atmosphere |
|---------|----------|------------|
| Full PBR | Full shadow + PBR | Full aerial perspective |
| Simplified | Shadow + diffuse only | Full aerial perspective |
| Minimal | Ambient only | **Still requires aerial perspective** |

Removing aerial perspective from distant trees causes visible color discontinuity against terrain/sky.

### Validation Checklist

When implementing any phase, verify:

- [ ] All tree shaders include `atmosphere_common.glsl`
- [ ] `applyAerialPerspective()` is called in all fragment shaders
- [ ] Shadow sampling uses same cascade parameters as terrain
- [ ] Impostor shader applies atmosphere after color lookup
- [ ] LOD transitions don't cause lighting pops
- [ ] Time-of-day changes affect all LOD levels equally

---

## Current System Overview

The existing tree rendering system implements several modern GPU-driven techniques:

| Component | Implementation | Files |
|-----------|---------------|-------|
| GPU Culling | Compute shaders for frustum/distance culling | `shaders/tree_leaf_cull.comp`, `shaders/tree_impostor_cull.comp` |
| Indirect Drawing | `DrawIndexedIndirectCommand` with atomic counters | `TreeRenderer.h` |
| Impostor Atlas | 17-view capture (8 horizontal + 8 elevated + 1 top-down) | `TreeImpostorAtlas.h` |
| LOD System | FullDetail / Blending / Impostor states | `TreeLODSystem.h` |
| Instance Data | SSBOs for `TreeCullData`, `TreeRenderDataGPU` | `TreeRenderer.h` |
| Spatial Indexing | Uniform grid partitioning with GPU buffers | `TreeSpatialIndex.h` |
| Hi-Z Occlusion | Hierarchical depth culling for impostors | `ImpostorCullSystem.h` |
| Two-Phase Leaf Cull | Tree filtering then leaf culling | `tree_filter.comp`, `tree_leaf_cull_phase3.comp` |

### Implementation Status Summary

| Phase | Status | Notes |
|-------|--------|-------|
| Phase 1: Spatial Partitioning | ✅ Complete | `TreeSpatialIndex`, `tree_cell_cull.comp` |
| Phase 2: Hi-Z Occlusion Culling | ✅ Complete | `ImpostorCullSystem`, integrated into `tree_impostor_cull.comp` |
| Phase 3: Two-Phase Tree-to-Leaf Culling | ✅ Complete | `tree_filter.comp`, `tree_leaf_cull_phase3.comp` with UI toggle |
| Phase 4: Screen-Space Error LOD | ✅ Complete | `TreeLODSettings.useScreenSpaceError`, FOV-aware LOD selection |
| Phase 5: Temporal Coherence | ✅ Complete | `ImpostorCullSystem::TemporalSettings`, visibility cache with UI toggle |
| Phase 6: Octahedral Impostor Mapping | ✅ Complete | `octahedral_mapping.glsl`, 8x8 grid, 3-frame blending |

### Remaining Bottlenecks

1. **No budget control**: Triangle count can explode in dense forest views
2. **Uniform shader complexity**: Same PBR shader at all distances

---

## Phase 1: Spatial Partitioning ✅ COMPLETE

**Goal**: Reduce culling workload from O(n) to O(visible cells + trees in visible cells)

**Status**: Fully implemented in `TreeSpatialIndex.h/cpp` and `tree_cell_cull.comp`.

### Design

Divide the world into a uniform grid of cells. Each cell stores:
- Axis-aligned bounding box (AABB)
- Index range into the global tree buffer
- Tree count

```
World Grid (example 64x64m cells):
+---+---+---+---+
| 12| 45|  8| 23|  <- tree counts per cell
+---+---+---+---+
|  5| 89| 34| 11|
+---+---+---+---+
```

### Implementation

#### Data Structures

```cpp
// CPU-side cell structure
struct TreeCell {
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    uint32_t firstTreeIndex;  // Index into sorted tree buffer
    uint32_t treeCount;
};

// GPU cell data (compact for culling)
struct TreeCellGPU {
    vec4 boundsMinAndFirst;   // xyz = min, w = firstTreeIndex as float bits
    vec4 boundsMaxAndCount;   // xyz = max, w = treeCount as float bits
};
```

#### Compute Pipeline

**Pass 1: Cell Culling** (`tree_cell_cull.comp`)
- Input: All cells (~1000s for large world)
- Output: Visible cell indices (compacted list)
- Dispatch: One thread per cell

**Pass 2: Tree Culling** (`tree_cull.comp` - modified)
- Input: Visible cell list, trees sorted by cell
- Output: Visible tree indices
- Dispatch: One workgroup per visible cell

#### Tree Sorting

Trees must be sorted spatially so each cell references a contiguous range:

```cpp
void TreeSpatialIndex::rebuildIndex(const std::vector<TreeInstanceData>& trees) {
    // Assign each tree to a cell
    for (size_t i = 0; i < trees.size(); i++) {
        int cellX = static_cast<int>(trees[i].position.x / cellSize_);
        int cellZ = static_cast<int>(trees[i].position.z / cellSize_);
        cellAssignments_[i] = {cellX, cellZ, i};
    }

    // Sort by cell, then by original index for stability
    std::sort(cellAssignments_.begin(), cellAssignments_.end());

    // Build cell ranges
    // ...
}
```

### Expected Impact

- **Culling reduction**: 10-100x depending on view frustum
- **Memory overhead**: ~32 bytes per cell (negligible)
- **Rebuild cost**: Only when trees added/removed (amortized)

---

## Phase 2: Hi-Z Occlusion Culling ✅ COMPLETE

**Goal**: Eliminate trees hidden behind terrain and large occluders

**Status**: Fully implemented in `ImpostorCullSystem.h/cpp` and integrated into `tree_impostor_cull.comp`. Features Hi-Z pyramid sampling, conservative occlusion testing using back of bounding sphere, and runtime toggle via UI.

### Design

Leverage the existing `HiZSystem` to perform hierarchical depth testing in the tree culling shader.

### Integration Points

1. **Depth pass order**: Ensure terrain renders before tree culling
2. **Hi-Z pyramid**: Already generated by `HiZSystem`
3. **Tree culling shader**: Add Hi-Z sampling after frustum test

### Shader Modification

Add to `tree_impostor_cull.comp`:

```glsl
layout(binding = BINDING_HIZ_PYRAMID) uniform sampler2D hiZPyramid;

// After frustum culling, before output write
vec4 clipPos = viewProj * vec4(treePos, 1.0);
vec3 ndc = clipPos.xyz / clipPos.w;

// Skip if behind camera
if (ndc.z < 0.0) return;

vec2 screenUV = ndc.xy * 0.5 + 0.5;

// Choose mip level based on screen-space size
float screenRadius = boundingRadius / clipPos.w;
float mipLevel = log2(max(screenRadius * screenSize.x, 1.0));

// Sample Hi-Z at appropriate mip
float occluderDepth = textureLod(hiZPyramid, screenUV, mipLevel).r;

// Conservative test: tree center depth vs occluder + margin
float treeDepth = ndc.z * 0.5 + 0.5;  // [0,1] range
if (treeDepth > occluderDepth + depthMargin) {
    return;  // Occluded
}
```

### Considerations

- **Depth margin**: Account for tree bounding sphere, not just center
- **Mip selection**: Use conservative (lower) mip to avoid false culling
- **Temporal stability**: Hi-Z from previous frame may cause 1-frame pop-in

### Expected Impact

- **Visibility reduction**: 30-70% in hilly/mountainous terrain
- **Overhead**: One texture sample per tree (minimal)

---

## Phase 3: Two-Phase Tree-to-Leaf Culling ✅ COMPLETE

**Goal**: Process leaf instances only for visible trees

**Status**: Fully implemented with `tree_filter.comp` (Phase 3 tree filtering) and `tree_leaf_cull_phase3.comp` (per-visible-tree leaf culling). Includes indirect dispatch via `DispatchIndirectCommand`, output buffer partitioning by leaf type, and UI toggle to compare with legacy single-phase culling. Descriptor sets are configured at runtime to avoid buffer binding at creation time.

### Current Problem

`tree_leaf_cull.comp` processes ALL leaf instances globally:
- 1M trees x 1000 leaves = 1 billion thread invocations
- Binary search per thread to find parent tree
- Massive waste when only 10K trees are visible

### Redesigned Pipeline

```
Phase 1: Tree Culling
    Input:  All trees (1M)
    Output: Visible tree list (compacted, ~10K)
            Visible tree count (atomic counter)

Phase 2: Leaf Culling
    Input:  Visible tree list
            Leaf instances for visible trees only
    Output: Visible leaf instances
            Indirect draw commands
```

### Implementation

#### Phase 1 Output Buffer

```glsl
// Compacted visible tree data
struct VisibleTreeData {
    uint originalTreeIndex;     // Index into full tree array
    uint leafFirstInstance;     // Offset into leaf instance buffer
    uint leafInstanceCount;     // Number of leaves for this tree
    uint outputBaseOffset;      // Where to write visible leaves
};

layout(std430, binding = BINDING_VISIBLE_TREES) buffer VisibleTreeBuffer {
    uint visibleTreeCount;
    VisibleTreeData visibleTrees[];
};
```

#### Phase 2 Dispatch

Instead of dispatching for all leaf instances, dispatch based on visible trees:

```cpp
// CPU-side: read back visible tree count (async)
// Or use indirect dispatch based on Phase 1 output

vkCmdDispatchIndirect(cmd, visibleTreeCountBuffer, 0);
```

#### Phase 2 Shader

```glsl
// One workgroup per visible tree
layout(local_size_x = 256) in;

void main() {
    uint visibleTreeIdx = gl_WorkGroupID.x;
    if (visibleTreeIdx >= visibleTreeCount) return;

    VisibleTreeData tree = visibleTrees[visibleTreeIdx];
    uint localLeafIdx = gl_LocalInvocationID.x;

    // Process leaves for this tree only
    for (uint i = localLeafIdx; i < tree.leafInstanceCount; i += 256) {
        uint globalLeafIdx = tree.leafFirstInstance + i;
        // ... frustum cull, LOD cull, write output
    }
}
```

### Expected Impact

- **Work reduction**: 50-90% (only process visible trees' leaves)
- **Memory**: Additional buffer for visible tree list (~40 bytes x visible count)
- **Synchronization**: Barrier between Phase 1 and Phase 2

---

## Phase 4: Screen-Space Error LOD Selection ✅ COMPLETE

**Goal**: Replace fixed distance thresholds with perceptually-correct screen-space error metrics

**Status**: Fully implemented with FOV-aware screen-space error calculation in both GPU (`tree_impostor_cull.comp`) and CPU (`TreeLODSystem`). Per-archetype world error bounds stored in `ArchetypeCullData.lodErrorData`. UI toggle in Tree tab allows comparison with legacy distance-based LOD.

### Why Screen-Space Error?

Fixed distance LOD has fundamental problems:

| Problem | Distance-Based | Screen-Error Based |
|---------|---------------|-------------------|
| 4K vs 1080p | Same LOD at same distance | 4K gets more detail (4x pixels) |
| Zoom/FOV change | LOD pops when zooming | Smooth, FOV-aware transitions |
| Dense forest view | May exceed triangle budget | Budget caps total triangles |
| Consistent quality | Varies with scene | Guaranteed pixel error threshold |

### LOD Selection by Screen Error

Instead of distance thresholds, each LOD level has a **maximum screen-space error** it can represent:

| LOD Level | Max Screen Error | Representation |
|-----------|-----------------|----------------|
| Full geometry | 0-2 pixels | All branches, individual leaves |
| Simplified geometry | 2-8 pixels | Major branches, leaf clusters |
| Billboard impostor | 8-32 pixels | Single textured quad |
| Point/culled | >32 pixels | Sub-pixel, not worth rendering |

### Implementation

#### Per-Tree Error Calculation (GPU)

```glsl
// In tree_lod_select.comp
layout(binding = BINDING_LOD_PARAMS) uniform LODParams {
    float screenHeight;      // Viewport height in pixels
    float tanHalfFOV;        // tan(fov/2) - precomputed
    float errorThreshold0;   // Full -> Simplified transition (pixels)
    float errorThreshold1;   // Simplified -> Impostor transition (pixels)
    float errorThreshold2;   // Impostor -> Culled transition (pixels)
    uint triangleBudget;     // Global triangle budget
};

// Per-archetype error bounds (uploaded once per tree type)
struct ArchetypeLODData {
    float worldErrorLOD0;    // World-space error for full geometry
    float worldErrorLOD1;    // World-space error for simplified
    float worldErrorLOD2;    // World-space error for impostor
    float boundingSphere;    // For screen coverage calculation
    uint trianglesLOD0;      // Triangle count at each LOD
    uint trianglesLOD1;
    uint trianglesLOD2;
};

float computeScreenError(float worldError, float distance) {
    // screenError = worldError * screenHeight / (2 * distance * tan(fov/2))
    return worldError * screenHeight / (2.0 * distance * tanHalfFOV);
}

uint selectLOD(float distance, ArchetypeLODData arch) {
    float screenErr0 = computeScreenError(arch.worldErrorLOD0, distance);
    float screenErr1 = computeScreenError(arch.worldErrorLOD1, distance);
    float screenErr2 = computeScreenError(arch.worldErrorLOD2, distance);

    // Select highest quality LOD that stays under threshold
    if (screenErr0 <= errorThreshold0) return 0;  // Full detail
    if (screenErr1 <= errorThreshold1) return 1;  // Simplified
    if (screenErr2 <= errorThreshold2) return 2;  // Impostor
    return 3;  // Culled (sub-pixel)
}
```

#### Global Triangle Budget

To prevent performance spikes in dense views, enforce a global triangle budget:

```glsl
// Atomic counter for budget tracking
layout(std430, binding = BINDING_BUDGET_COUNTER) buffer BudgetCounter {
    uint currentTriangles;
    uint budgetExceeded;  // Flag to signal overflow
};

void processTree(uint treeIdx, float distance, ArchetypeLODData arch) {
    uint lod = selectLOD(distance, arch);

    // Get triangle cost for selected LOD
    uint triCost = (lod == 0) ? arch.trianglesLOD0 :
                   (lod == 1) ? arch.trianglesLOD1 :
                   (lod == 2) ? arch.trianglesLOD2 : 0;

    // Atomically try to reserve budget
    uint prevTotal = atomicAdd(currentTriangles, triCost);

    if (prevTotal + triCost > triangleBudget) {
        // Budget exceeded - downgrade LOD or cull
        atomicAdd(currentTriangles, -int(triCost));  // Undo reservation

        // Try lower LOD
        if (lod < 2) {
            lod++;
            triCost = (lod == 1) ? arch.trianglesLOD1 : arch.trianglesLOD2;
            prevTotal = atomicAdd(currentTriangles, triCost);
            if (prevTotal + triCost > triangleBudget) {
                atomicAdd(currentTriangles, -int(triCost));
                return;  // Fully culled
            }
        } else {
            return;  // Already at lowest LOD, cull
        }
    }

    // Output to appropriate LOD bucket
    outputTreeAtLOD(treeIdx, lod);
}
```

#### Priority-Based Budget Allocation

For better quality distribution, sort trees by screen coverage and allocate budget to largest first:

```
1. Compute screen coverage for all visible trees (parallel)
2. Sort by coverage (descending) - use GPU radix sort
3. Iterate sorted list, greedily assign highest affordable LOD
4. Stop when budget exhausted
```

This ensures nearby/large trees always get detail, while distant trees absorb budget cuts.

### Shader LOD Variants

Use Vulkan specialization constants for material complexity tiers:

```glsl
layout(constant_id = 0) const uint MATERIAL_QUALITY = 0;

void main() {
    vec3 albedo = texture(albedoMap, uv).rgb;

    if (MATERIAL_QUALITY == 0) {
        // Full PBR: normal, roughness, AO, subsurface
        color = fullPBR(albedo, uv);
    } else if (MATERIAL_QUALITY == 1) {
        // Simplified: albedo + normal only
        color = simplifiedPBR(albedo, uv);
    } else {
        // Minimal: albedo * ambient
        color = albedo * ambientLight;
    }
}
```

### Error Metric Authoring

Each tree archetype needs world-space error values for its LOD levels:

```cpp
struct TreeArchetypeLOD {
    // Authored per tree type (oak, pine, etc.)
    float branchThickness;      // Thinnest visible branch at LOD0
    float leafClusterSize;      // Leaf cluster size at LOD1
    float canopyRadius;         // Overall canopy size for impostor error

    // Computed
    float worldErrorLOD0() { return branchThickness; }
    float worldErrorLOD1() { return leafClusterSize; }
    float worldErrorLOD2() { return canopyRadius * 0.1f; }  // 10% of canopy = impostor error
};
```

### Expected Impact

- **Consistent quality**: Same pixel error across resolutions/FOVs
- **Budget guarantee**: Never exceed triangle limit regardless of view
- **Automatic adaptation**: No manual tuning per resolution
- **Smooth transitions**: Error-based blending instead of distance popping

---

## Phase 5: Temporal Coherence ✅ COMPLETE

**Goal**: Reuse visibility data across frames for stable camera views

**Status**: Fully implemented in `ImpostorCullSystem.h/cpp` and `tree_impostor_cull.comp`.

### Implementation Details

- **Visibility cache**: GPU buffer storing 1 bit per tree (packed into `uint` words)
- **Camera tracking**: Position and rotation delta calculation per frame
- **Update modes**: Full (>5m or >10°), partial (10% round-robin), skip (stationary)
- **UI controls**: Toggle + sliders for position/rotation thresholds and partial update fraction

### Design

For stationary or slowly moving cameras, most tree visibility doesn't change. Track camera movement and selectively update visibility:

```cpp
struct TemporalCullingState {
    glm::vec3 lastCameraPos;
    glm::quat lastCameraRot;
    std::vector<uint8_t> treeVisibility;  // Bitfield: 0=hidden, 1=visible
    uint32_t framesSinceFullUpdate;
};
```

### Update Strategy

1. **Camera delta calculation**:
   ```cpp
   float posDelta = glm::length(currentPos - lastPos);
   float rotDelta = glm::angle(currentRot, lastRot);
   ```

2. **Update mode selection**:
   - **Full update**: Camera moved significantly (>5m or >10 degrees)
   - **Partial update**: Update 10-20% of trees per frame (round-robin)
   - **Skip update**: Camera stationary, reuse previous visibility

3. **Dirty marking**:
   - Trees that moved
   - Trees crossing LOD boundaries
   - Trees near frustum edges

### Shader Support

```glsl
layout(std430, binding = BINDING_VISIBILITY_CACHE) buffer VisibilityCache {
    uint visibilityBits[];  // Packed bitfield
};

layout(push_constant) uniform PushConstants {
    uint updateMode;      // 0=full, 1=partial, 2=skip
    uint updateOffset;    // For partial: which trees to update
    uint updateCount;
};

void main() {
    uint treeIdx = gl_GlobalInvocationID.x;

    // Check if this tree needs updating
    bool needsUpdate = (updateMode == 0) ||  // Full update
                       (updateMode == 1 && treeIdx >= updateOffset &&
                        treeIdx < updateOffset + updateCount);

    if (!needsUpdate) {
        // Use cached visibility
        bool wasVisible = (visibilityBits[treeIdx / 32] >> (treeIdx % 32)) & 1;
        if (wasVisible) {
            outputVisibleTree(treeIdx);
        }
        return;
    }

    // Perform full culling test
    // ...
}
```

### Expected Impact

- **Work reduction**: 50-80% for typical gameplay (camera following player)
- **Latency**: Potential 1-frame delay for visibility changes
- **Memory**: 1 bit per tree for visibility cache (~125KB for 1M trees)

---

## Phase 6: Octahedral Impostor Mapping

**Goal**: Improve impostor quality and reduce memory with continuous view mapping

### Current System

- 17 discrete views per archetype (8 horizontal + 8 elevated + 1 top-down)
- 256x256 pixels per cell
- Atlas size: 2304x512 per archetype
- ~4MB per archetype (albedo + normal)

### Octahedral Mapping

Single hemisphere projection providing continuous view coverage:

```
Octahedral UV mapping:
    +Y (top-down)
       /\
      /  \
     /    \
    /      \
   +--------+
   |   N    |
   | W   E  |
   |   S    |
   +--------+
```

### Benefits

- **Smoother transitions**: No discrete view jumps
- **Better coverage**: Continuous interpolation between all angles
- **Memory reduction**: Single 512x512 texture (~1.5MB per archetype)

### Shader Implementation

```glsl
// Convert view direction to octahedral UV
vec2 octahedralEncode(vec3 dir) {
    dir /= abs(dir.x) + abs(dir.y) + abs(dir.z);

    if (dir.y < 0.0) {
        dir.xz = (1.0 - abs(dir.zx)) * sign(dir.xz);
    }

    return dir.xz * 0.5 + 0.5;
}

// Sample impostor with octahedral mapping
vec4 sampleImpostor(vec3 viewDir, uint archetypeIndex) {
    vec2 uv = octahedralEncode(viewDir);
    return texture(impostorArray, vec3(uv, float(archetypeIndex)));
}
```

### Migration Path

1. Generate octahedral impostors alongside current system
2. Add runtime toggle for comparison
3. Validate quality matches or exceeds current system
4. Remove legacy 17-view system

### Expected Impact

- **Memory reduction**: ~60% per archetype
- **Quality**: Smoother view transitions, no angular artifacts
- **Generation time**: Slightly faster (fewer render passes)

---

## Implementation Schedule

| Phase | Status | Dependencies | Complexity | Files Modified |
|-------|--------|--------------|------------|----------------|
| 1. Spatial Partitioning | ✅ Done | None | High | `TreeSpatialIndex.h/cpp`, `tree_cell_cull.comp` |
| 2. Hi-Z Occlusion | ✅ Done | Phase 1 (optional) | Medium | `ImpostorCullSystem.h/cpp`, `tree_impostor_cull.comp` |
| 3. Two-Phase Culling | ✅ Done | Phase 1 | High | `tree_filter.comp`, `tree_leaf_cull_phase3.comp`, `TreeRenderer.cpp` |
| 4. Screen-Space Error LOD | ✅ Done | None | Medium | `TreeLODSettings`, `tree_impostor_cull.comp`, `TreeLODSystem.cpp` |
| 5. Temporal Coherence | ✅ Done | Phase 1, 3 | Medium | `ImpostorCullSystem.h/cpp`, `tree_impostor_cull.comp`, `GuiTreeTab.cpp` |
| 6. Octahedral Impostors | ✅ Done | None | High | `octahedral_mapping.glsl`, `TreeImpostorAtlas.cpp`, `tree_impostor.vert/frag` |

### Recommended Next Steps

All six phases are now complete! Potential future improvements:
- **Triangle Budget Control**: Limit total triangle count across all visible trees
- **Mesh LOD Merging**: Combine nearby tree geometry into shared mesh chunks
- **Virtual Texturing**: Stream impostor atlas tiles on demand
- **Ray-Marched Impostors**: Replace billboards with volumetric ray-marching for close views

---

## Performance Targets

| Metric | Current (10K trees) | Target (1M trees) |
|--------|---------------------|-------------------|
| Culling time | ~0.5ms | <2ms |
| Draw calls | ~20 | <50 |
| Visible tree ratio | 100% | 1-5% (after culling) |
| VRAM (instances) | ~10MB | ~500MB |
| VRAM (impostors) | ~64MB | ~100MB (octahedral) |
| Triangle budget | Unbounded | 2-5M triangles |
| Screen error target | N/A | <2 pixels at LOD0 |

### Budget Tuning Guidelines

| Quality Preset | Triangle Budget | Error Threshold | Target Use Case |
|----------------|-----------------|-----------------|-----------------|
| Ultra | 5M | 1 pixel | 4K, high-end GPU |
| High | 3M | 2 pixels | 1440p, mid-range GPU |
| Medium | 2M | 4 pixels | 1080p, integrated GPU |
| Low | 1M | 8 pixels | 720p, mobile/low-end |

---

## Testing Strategy

### Stress Test Scenarios

1. **Dense forest**: Camera inside forest, high visible tree count
2. **Overlook**: Camera on mountain, viewing forest below (tests Hi-Z)
3. **Flyover**: Fast camera movement (tests temporal coherence)
4. **Edge cases**: Trees at frustum boundaries, LOD transitions

### Validation

- **Visual**: No popping, smooth LOD transitions
- **Performance**: GPU profiler timestamps per phase
- **Correctness**: Compare culling results with CPU reference implementation

### Debug Visualization

- Color trees by: cell, LOD level, visibility source (cached vs computed)
- Draw cell bounding boxes
- Show Hi-Z pyramid mip levels
- Display culling statistics overlay
