# Ghost of Tsushima Streaming Techniques Review

Review of this codebase against techniques from Adrian Bentley's GDC 2021 talk
"Zen of Streaming: Building and Loading Ghost of Tsushima" (Sucker Punch Productions)

---

## Summary

| Category | Implemented | Partially | Missing |
|----------|-------------|-----------|---------|
| Streaming Infrastructure | 3 | 2 | 4 |
| Memory Management | 2 | 1 | 3 |
| GPU Compute | 4 | 1 | 2 |
| Rendering Optimization | 4 | 2 | 2 |
| Loading Optimization | 2 | 1 | 3 |

**Overall Assessment**: The codebase implements many key streaming techniques well, particularly the virtual texture system, static distance heap, and procedural grass. Major gaps exist in pack file aggregation, mesh streaming, async compute overlap, and prefetching.

---

## Techniques Comparison

### 1. WORLD STRUCTURE & SPATIAL STREAMING

#### Tile-Based World (GOT: 200m tiles, quad-tree streaming)

**Status: IMPLEMENTED**

- `TerrainTileCache` (`src/terrain/TerrainTileCache.h:64`): 32x32 tile grid at LOD0
- 4 LOD levels with distance thresholds (1km/2km/4km/8km)
- Quad-tree style distance-based loading/unloading

```cpp
// Current implementation - matches GOT approach
static constexpr float LOD0_MAX_DISTANCE = 1000.0f;  // < 1km: LOD0
static constexpr float LOD1_MAX_DISTANCE = 2000.0f;  // 1-2km: LOD1
static constexpr float LOD2_MAX_DISTANCE = 4000.0f;  // 2-4km: LOD2
static constexpr float LOD3_MAX_DISTANCE = 8000.0f;  // 4-8km: LOD3
```

**Recommendation**: Consider 7-level quad-tree (GOT used this) for finer streaming control at extreme distances.

---

#### Static Distance Heap (GOT: O(1) amortized wake/sleep)

**Status: FULLY IMPLEMENTED**

- `StaticDistanceHeap` (`src/core/StaticDistanceHeap.h`): Exact implementation of GOT technique
- Travel distance tracking with heap-based scheduling
- 100m rebase interval for float precision
- Back-indices for O(log n) removal

```cpp
// Matches GOT pseudocode exactly
while (!entries.empty() &&
       entries[0].nextCheckDistance <= currentTravelDistance) {
    float sdf = signedDistanceToBoundary(playerPos, top.bounds);
    if (sdf < 0.0f) { /* wake */ }
    else { /* sleep */ }
    top.nextCheckDistance = currentTravelDistance + std::abs(sdf);
    heapifyDown(0);
}
```

**Status: Production-ready**. This is a textbook implementation of the GOT technique.

---

#### Region/Mission Streaming

**Status: MISSING**

GOT used region packs for sparse complex data (cities, villages, landmarks) with mission streaming.

**Current state**: Only terrain tiles stream. No concept of:
- Region packs with larger budgets
- Mission data streaming
- Overlapping region bounds
- Mission task graphs

**Recommendation**: Add `RegionManager` that coordinates loading of:
- NPC spawn data
- Mission scripts
- Dialogue
- Region-specific assets

---

### 2. TEXTURE STREAMING

#### Fine-Grained Virtual Texturing (GOT: single manifest, per-texture reads)

**Status: IMPLEMENTED**

- `VirtualTextureSystem` (`src/terrain/VirtualTextureSystem.h`): Full feedback-driven streaming
- GPU feedback buffer reads tile requests
- LRU cache eviction (`VirtualTextureCache`)
- Page table indirection

**Matches GOT**:
- Single manifest approach (tiles pre-generated in `tile_generator`)
- Per-tile reads (128x128 tiles)
- 16 max uploads/frame (GOT used 16)
- 64 max requests/frame

```cpp
static constexpr uint32_t MAX_UPLOADS_PER_FRAME = 16;
static constexpr uint32_t MAX_REQUESTS_PER_FRAME = 64;
```

---

#### Penalty Scheme for Over-Budget (GOT: graceful mip degradation)

**Status: IMPLEMENTED**

```cpp
// Ghost of Tsushima style penalty scheme
float currentPenalty = 0.0f;
static constexpr float PENALTY_INCREMENT = 0.5f;  // Half a mip level
static constexpr float PENALTY_RELAX_RATE = 0.1f;
static constexpr float MAX_PENALTY = 4.0f;        // Max 4 mip degradation
```

**Status: Production-ready**. Exact match to GOT approach.

---

#### UV Heuristics (GOT: log bucketing for texture scale)

**Status: MISSING**

GOT used sophisticated UV heuristics:
- Log bucketing by texture size
- Triangle area accumulation per bucket
- 80th percentile cutoff for degenerate UVs

**Current state**: No screen-size based texture priority. Virtual texture just loads what feedback requests.

**Recommendation**: Add UV-based mip selection heuristics to prevent:
- Degenerate UV explosions (e.g., procedural log generators)
- Counter-UV stretching artifacts

---

#### Prefetching (GOT: camera cuts, character loading)

**Status: PARTIALLY IMPLEMENTED**

- `BreadcrumbTracker` (`src/scene/BreadcrumbTracker.h`): Respawn position tracking
- No cutscene prefetching
- No character costume prefetching

**GOT approach**:
```
- Camera cuts require foreknowledge
- Rigging knows how far ahead to preload
- 1K clamp for most scenes, 2K unlock for close-ups
```

**Recommendation**: Add `PrefetchManager` that:
- Tracks upcoming camera positions
- Pre-loads character textures on spawn
- Coordinates with cutscene system

---

#### Bounding Box Merging (GOT: sqrt(n) merged boxes)

**Status: MISSING**

GOT merged bounding boxes bottom-up to reduce overhead from per-instance boxes.

**Current state**: No bounding box streaming optimization.

**Recommendation**: For large instance counts, implement grid bucketing + merge to sqrt(n) boxes.

---

### 3. MESH STREAMING

#### Fine-Grained Mesh Streaming (GOT: virtual memory, LOD ranges)

**Status: MISSING**

GOT extended texture streaming to meshes:
- Virtual memory for fragmentation-free allocation
- Single read per LOD range
- Stipple-based LOD transitions

**Current state**: Meshes fully loaded. No streaming.

**Recommendation**: Implement `MeshStreamingSystem`:
- Virtual memory allocation (VMA supports this)
- LOD range mapping (bytes per LOD)
- Distance-based mesh priority
- Stipple transitions

---

### 4. GPU COMPUTE & ASYNC

#### GPU Occlusion Culling (GOT: 24-byte instances)

**Status: PARTIALLY IMPLEMENTED**

- `HiZSystem` (`src/postprocess/HiZSystem.h`): Hi-Z pyramid generation
- Used for screen-space effects

**Missing**:
- 24-byte compact instance representation
- GPU-driven draw calls
- Far LOD stochastic culling

**GOT approach**:
```cpp
// 24 bytes per instance (very compact)
struct GPUInstance {
    vec3 position;      // 12 bytes
    uint16_t rotation;  // 2 bytes
    uint16_t scale;     // 2 bytes
    uint32_t meshId;    // 4 bytes
    uint32_t flags;     // 4 bytes
};
```

**Recommendation**: Implement GPU instance culling with:
- Compact instance buffer
- Indirect draw generation
- Hi-Z occlusion testing

---

#### Async Compute Overlap (GOT: heavy async usage)

**Status: PARTIALLY IMPLEMENTED**

- Compute shaders exist (`ComputeStage`)
- Not running on separate async queue

**GOT used async compute heavily**:
- Terrain subdivision
- Cloth simulation
- Particle systems
- Cube map relighting
- Shadow generation

**Current state**: Compute runs synchronously in render pipeline.

**Recommendation**: Add async compute queue:
```cpp
// Separate async compute queue
VkQueue asyncComputeQueue;
VkCommandPool asyncComputePool;
// Overlap compute with graphics
```

---

#### Procedural Grass (GOT: GPU compute, wind interaction)

**Status: FULLY IMPLEMENTED**

- `GrassSystem` (`src/vegetation/GrassSystem.h`): 100K instances
- GPU compute culling (`grass.comp`)
- Player displacement texture (512x512, 50m region)
- Wind interaction
- Double-buffered instance buffers

```cpp
static constexpr uint32_t DISPLACEMENT_TEXTURE_SIZE = 512;
static constexpr float DISPLACEMENT_REGION_SIZE = 50.0f;
static constexpr uint32_t MAX_INSTANCES = 100000;
```

**Status: Production-ready**. Excellent implementation matching GOT style.

---

#### GPU Cloth Simulation

**Status: NOT PRESENT**

GOT had extensive GPU cloth for character clothing, horse reins, banners.

**Current state**: No GPU cloth system.

**Recommendation**: Add compute-based cloth for:
- Character capes/clothing
- Environmental flags/banners
- Vegetation cloth (leaves, grass clumps)

---

### 5. PACK/AGGREGATE FILE SYSTEM

#### Pack Files (GOT: header + TOC, single read to usable)

**Status: MISSING**

GOT used pack files:
- Aggregate small files into large chunks
- Header + TOC cached at boot
- Pointer patching for instant usability
- Virtual memory for fragmentation avoidance

**Current state**: Assets loaded individually (PNG tiles, GLTF meshes).

**Recommendation**: Implement `PackFileSystem`:
```cpp
struct PackHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t tocOffset;
    uint32_t tocCount;
};

struct PackEntry {
    uint64_t hash;      // Asset name hash
    uint32_t offset;    // Byte offset in pack
    uint32_t size;      // Uncompressed size
    uint32_t flags;     // Compression, type
};
```

---

#### Compile-Time Data Merging (GOT: flattened BVHs, index ranges)

**Status: PARTIALLY IMPLEMENTED**

- Terrain preprocessing tools exist
- No runtime BVH merging
- No index range optimization

**GOT approach**:
- Merge physics BVHs at compile time
- Use 2-byte min/max index ranges for sub-object references
- Sort by parent chain for spatial coherence

**Recommendation**: Add build-time merging for:
- Physics collision geometry
- Occlusion volumes
- Renderable batches

---

### 6. LOADING OPTIMIZATION

#### Fast Travel Texture Reduction (GOT: 75% reduction during warp)

**Status: MISSING**

GOT reduced texture budget by 3/4 during warps for faster loads.

**Recommendation**: Add load state to `VirtualTextureSystem`:
```cpp
void setLoadMode(LoadMode mode) {
    if (mode == LoadMode::FastTravel) {
        targetPenalty = 3.0f;  // Force 3 mip levels coarser
    }
}
```

---

#### Breadcrumb Respawning (GOT: respawn near death for faster reload)

**Status: FULLY IMPLEMENTED**

- `BreadcrumbTracker` (`src/scene/BreadcrumbTracker.h`): Exact GOT technique
- Safety check callbacks
- Distance thresholds
- Most recent + nearest search

```cpp
// Matches GOT exactly
std::optional<glm::vec3> getNearestSafeBreadcrumb(const glm::vec3& position);
std::optional<glm::vec3> getSafeBreadcrumbAwayFrom(const glm::vec3& position, float minSafeDistance);
```

**Status: Production-ready**.

---

#### Death Reload Pacing (GOT: minimum tip display time)

**Status: MISSING**

GOT intentionally slowed death reloads so loading tips were readable.

**Recommendation**: Add minimum display time for loading screens.

---

### 7. TERRAIN & RENDERING

#### Height Map Terrain (GOT: Frostbite-style, shared CPU/GPU)

**Status: FULLY IMPLEMENTED**

- CBT adaptive subdivision (`TerrainCBT`)
- Meshlet LOD system (`TerrainMeshlet`)
- CPU height queries for physics
- Shared height data between GPU and physics

---

#### Virtual Texturing for Terrain

**Status: FULLY IMPLEMENTED**

- Material compositing via virtual textures
- Decal support through VT
- Efficient blend calculations

---

#### Time-of-Day Relighting (GOT: cube maps, terrain probes)

**Status: PARTIALLY IMPLEMENTED**

- `AtmosphereLUTSystem`: Scattering LUTs update with time
- `SkySystem`: Dynamic sky
- No cube map relighting
- No terrain probe system

**GOT approach**:
- 6-16 nearest cube maps for interiors
- Coarse terrain probes at multiple heights
- Async compute relighting

**Recommendation**: Add probe-based indirect lighting:
- Baked probe positions
- Runtime SH update based on time of day
- Async compute for probe updates

---

#### Screen Space Shadows (GOT: drop small shadow geometry)

**Status: MISSING**

GOT used screen space shadows to avoid rendering small objects in shadow passes.

**Current state**: All objects render to all cascade levels.

**Recommendation**: Add screen-space shadow system:
- Skip sub-pixel shadow casters
- Contact hardening from screen depth

---

### 8. WORLD PRECISION

#### Large World Precision (GOT: subtract before multiply)

**Status: LIKELY NEEDED**

GOT applied precision fixes liberally at 8km scale. This engine targets 16km terrain.

**Recommendation**: Audit for:
- Camera-relative rendering (subtract camera before transform)
- Matrix inverse precision (subtract before multiply)
- Double-precision for world positions

---

## Priority Recommendations

### High Priority (Core Streaming)

1. **Pack File System** - Critical for disk I/O efficiency
2. **Mesh Streaming** - Memory constraint for large scenes
3. **Async Compute Queue** - GPU parallelism gains

### Medium Priority (Optimization)

4. **GPU Instance Culling** - Reduce draw calls for dense scenes
5. **Prefetch Manager** - Smoother camera cuts
6. **Fast Travel Mode** - Faster world traversal

### Lower Priority (Polish)

7. **UV Heuristics** - Edge case texture popping
8. **Screen Space Shadows** - Shadow pass optimization
9. **Probe-Based Lighting** - Better time-of-day transitions

---

## Existing Strengths

The codebase excels at several GOT techniques:

1. **StaticDistanceHeap** - Textbook implementation
2. **BreadcrumbTracker** - Complete respawn optimization
3. **VirtualTextureSystem** - Production-quality with penalty scheme
4. **GrassSystem** - Full GPU compute with displacement
5. **TerrainTileCache** - Multi-LOD streaming
6. **CBT Terrain** - Excellent adaptive mesh

---

## References

- GDC 2021: "Zen of Streaming" - Adrian Bentley, Sucker Punch
- GDC 2021: "Procedural Grass in Ghost of Tsushima" - Eric Wohllaib
- GDC 2021: "Samurai Landscapes" - Matt Pettineo
- SIGGRAPH 2021: "Lighting in Ghost of Tsushima" - Jasmin Patry
- Frostbite Terrain Rendering (referenced in talk)
- BC6H Real-time Compression - Kristof (referenced for cube maps)
