# Streaming Architecture: Current vs Ghost of Tsushima

This document compares our current streaming architecture against techniques from Ghost of Tsushima (GDC 2021: "Zen of Streaming") and outlines a plan for improvements.

---

## Executive Summary

Our current implementation has a solid foundation with **virtual texturing**, **terrain tile caching**, **GPU compute culling**, and **async loading**. However, Ghost of Tsushima introduced several advanced techniques that could improve memory efficiency, loading times, and scalability for non-terrain content.

### Key Architectural Difference: Terrain Geometry

| Aspect | This Project (CBT/LEB) | Ghost of Tsushima (Patches) |
|--------|------------------------|----------------------------|
| **Geometry LOD** | GPU-driven adaptive tessellation | Pre-baked LOD levels per tile |
| **Mesh Source** | Procedural from heightmap | Streamed from disk |
| **Subdivision** | Continuous screen-space error | Fixed LOD thresholds |
| **Memory** | Heightmap only | Heightmap + pre-tessellated meshes |
| **CPU/GPU Split** | GPU decides everything | CPU manages tile/LOD selection |

**Implication:** Ghost's terrain geometry streaming (200m tiles, neighbor stitching, LOD mesh loading) does not apply here. CBT/LEB generates geometry on-the-fly, so only heightmap data needs streaming—which is already handled by `TerrainTileCache`.

### Technique Applicability

| Area | Current Status | Ghost of Tsushima | Applies To |
|------|---------------|-------------------|------------|
| Texture Streaming | Virtual texturing, LRU cache | Fine-grained per-texture, UV heuristics | Non-terrain meshes |
| Mesh Streaming | Not implemented | Per-LOD streaming, squishy budgets | Characters, props, buildings |
| Asset Aggregation | Individual files | Pack system with pointer-patch | All assets |
| Distance Checks | Per-frame checks | Static distance heap (O(1) amortized) | Tile cache, physics, regions |
| Prefetching | Not implemented | Camera cut prediction | Cutscenes, spawns |
| Over-budget Handling | Cache eviction | Penalty scheme with progressive LOD | VT system |
| Terrain Geometry | **CBT/LEB (more advanced)** | Patch-based with neighbor buffers | N/A - different approach |

---

## Part 1: Current Architecture

### 1.1 Virtual Texture System (VT)
**Location:** `src/VirtualTexture*.h/cpp`

- **Approach:** Pre-baked megatexture tiles (128×128 px) streamed on-demand
- **Cache:** 4K×4K RGBA8 physical cache (~64MB), LRU eviction
- **Feedback:** GPU shader writes requested tile IDs to buffer
- **Loading:** Worker thread pool with priority queue
- **Memory:** ~70MB total (cache + indirection + staging)

**Strengths:**
- Single VT lookup per fragment (roads/rivers baked in)
- GPU feedback avoids CPU stalls
- Mip-level fallback while tiles load

**Limitations:**
- No UV-based priority (all tiles weighted equally)
- No penalty scheme when over-budget
- No prefetching for camera cuts

### 1.2 Terrain Tile Cache
**Location:** `src/TerrainTileCache.h/cpp`

- 4 LOD levels (512×512 heightmap samples each)
- Distance-based loading (2000m load, 3000m unload)
- Max 64 active tiles
- Direct CPU query for physics/collision

### 1.3 GPU Compute Culling
**Location:** `src/HiZSystem.h/cpp`, `src/WaterTileCull.h/cpp`, `src/TerrainSystem.cpp`

- Hi-Z occlusion culling via depth pyramid
- Water tile visibility culling (32×32 screen tiles)
- Terrain per-triangle culling (CBT/Clipmap)
- Shadow cascade culling

### 1.4 Asset Loading
- Individual file reads (glTF, FBX, PNG)
- No aggregated pack system
- No memoized manifests

---

## Part 2: Ghost of Tsushima Techniques

### 2.1 Napkin Math (Early Constraint Analysis)
Ghost of Tsushima started with scale estimation:
- 6,400 tiles × 72MB = 460GB (way too big)
- Target: ~4MB per tile for half a Blu-ray
- Estimated 2,500 trees per 100m tile → 12MB without optimization

**Key insight:** Early math drives architecture decisions. Budget backwards from target hardware.

### 2.2 200-Meter Tile Quad Tree (Patch-Based Terrain)
- 7-level quad tree for spatial streaming
- Leaf level: 3×3 grid of tiles (~200m guaranteed sight lines)
- Fewer files than 100m tiles, less duplication waste
- ~2.5MB terrain data per tile (textures + metadata + grass/water control)
- Used Frostbite-style neighbor index buffers for LOD stitching

> **Note:** This patch-based approach differs fundamentally from our CBT/LEB terrain. Ghost pre-tessellated terrain meshes at multiple LOD levels and streamed them from disk. CBT/LEB generates geometry procedurally on the GPU from heightmap data, eliminating the need for terrain mesh streaming. The virtual texturing and heightmap tile streaming concepts still apply.

### 2.3 Pack Aggregation System
**Key optimization: Single-read asset loading**

```
Pack Structure:
┌─────────────────────────┐
│ Header (cached at boot) │
├─────────────────────────┤
│ Table of Contents       │
├─────────────────────────┤
│ Pointer-patched data    │  ← Ready to use immediately
│ (memory-layout format)  │
└─────────────────────────┘
```

- Header + TOC read once at boot
- Subsequent reads: 1-2 seeks → usable data
- No fix-up, pointers already resolved
- Virtual memory for fragmentation-free allocation
- ~1,700 terrain tiles + 1,000 mission/region packs for entire game

### 2.4 Compile-Time Data Merging
Convert expensive high-level objects to compact low-level representations:

```cpp
// Full object: scriptable, flexible
struct FullTree { Transform, LODGroup, Materials[], Scripts[], Physics... };

// Merged record: ~6 bytes per instance per LOD per shader
struct MergedInstance { uint16_t pos[3]; };  // Position in grid cell
```

- Build BVHs for physics, occlusion, cloth obstacles
- Merge imposters at compile time (not runtime)
- Filter during merge (skip irrelevant content)
- Example: 5 min merge → 30 sec with filtering

### 2.5 Index Ranges
Refer to subsets of merged arrays with 4 bytes (2-byte min + 2-byte max):

```cpp
// Instead of storing list of children:
struct Node {
    std::vector<uint32_t> children;  // Expensive
};

// Use index range (sorted by parent chain):
struct Node {
    uint16_t childrenMin, childrenMax;  // 4 bytes total
};
```

- Sort merged arrays by instancing hierarchy
- Parent nodes reference contiguous child ranges
- Add/remove subsets efficiently (e.g., when breakables break)

### 2.6 Static Distance Heap
**O(1) amortized distance checks for hundreds of volumes**

```cpp
struct DistanceHeapItem {
    float nextCheckDistance;  // Current travel + distance to boundary
    VolumeId volume;
};

// Per frame: only check top of heap
void update(float currentTravelDistance) {
    while (heap.top().nextCheckDistance <= currentTravelDistance) {
        auto item = heap.pop();
        float distToBoundary = computeDistanceToBoundary(item.volume);

        if (distToBoundary < 0) {
            wakeObject(item.volume);
        }

        // Reschedule with new distance
        item.nextCheckDistance = currentTravelDistance + abs(distToBoundary);
        heap.push(item);
    }
}

// Rebase every 100m to maintain precision
```

- 12μs for hundreds of volumes (vs 300μs naive sphere checks)
- Back-indices for O(1) removal
- Works best with single moving reference point

### 2.7 Fine-Grained Texture Streaming
**Key insight: Not tile-based like VT, but per-texture with UV heuristics**

```
Manifest (loaded at boot):
┌──────────────────────────────────┐
│ All texture headers + metadata   │
│ Lower LOD mips (always available)│
├──────────────────────────────────┤
│ Per-texture: header + mip data   │  ← Single read per texture
└──────────────────────────────────┘
```

**UV Heuristics (Log Bucketing):**
1. Bucket triangles by log2(uv_scale)
2. Sum triangle areas per bucket
3. Find 50th/80th percentile bucket
4. Derive desired texture resolution
5. Handles degenerate UVs (procedural meshes)

**Over-Budget Penalty Scheme:**
```cpp
float globalPenalty = 0.0f;

void adjustBudget() {
    while (requestedSize > budget) {
        globalPenalty += 0.1f;  // Increase penalty
        recomputeRequests(globalPenalty);  // Lower all mip requests
    }

    // Relax penalty when stable
    if (allRequestsLoaded) {
        globalPenalty = max(0, globalPenalty - 0.05f);
    }
}
```

**Bounding Box Optimization:**
- Problem: Per-instance per-LOD per-shader bounding boxes = huge overhead
- Solution: Grid bucketing + bottom-up merge → √N boxes
- Min camera distance clamps for far-LOD content

### 2.8 Mesh Streaming
Same architecture as texture streaming:
- Virtual memory allocation (no defrag)
- Per-LOD-range reads
- Compressed distance bytes
- Stipple-based LOD fading awareness

**Key benefit:** Unlocked character count constraints for missions.

### 2.9 Prefetching
Critical for avoiding "Halo 2 style cutscene pops":

- Every camera cut needs foreknowledge
- Characters entering scene need advance notice
- Default to 1K textures, unlock for close-ups
- Cutscene rigging specifies required assets ahead of time

### 2.10 Runtime Relighting
No fixed time-of-day baking (20× world size made it impossible):

- 6-16 nearest cube maps with self-shadowing
- Coarse terrain probes (3 height layers) → 44MB total
- Re-light probes in async compute
- BC6H compression (saved ~200MB)

### 2.11 Breadcrumb Respawning
- Player respawns to closest safe breadcrumb
- Keeps player near same content → faster reload
- Breadcrumbs update as player moves

---

## Part 3: Gap Analysis

### Non-Gaps (Where Current Approach is Equal or Better)

#### Terrain Geometry Streaming
**Not needed.** CBT/LEB terrain generates geometry procedurally on the GPU. Ghost had to stream pre-tessellated terrain meshes at multiple LOD levels with neighbor stitching buffers. Our approach:
- Eliminates terrain mesh storage entirely
- Provides continuous LOD (no visible LOD pops)
- Adapts to screen-space error in real-time
- Only requires heightmap tile streaming (already implemented in `TerrainTileCache`)

#### GPU Terrain Culling
**Already implemented.** Per-triangle culling via CBT compute shaders, shadow cascade culling. Ghost used GPU occlusion culling for props/trees but CPU-managed terrain tiles.

### High Priority Gaps

#### 3.1 Pack Aggregation System
**Impact:** Significantly faster loading, reduced disk I/O

**Current:** Individual file reads with standard loaders
**Target:** Single-read packs with pointer-patched data

**Implementation Complexity:** High
- Requires custom pack format and build tooling
- All asset loaders need pack-aware paths
- Virtual memory allocation for pack pages

#### 3.2 Mesh Streaming
**Impact:** Remove mesh memory constraints, faster warps

**Current:** All meshes loaded at init or on-demand as full files
**Target:** Per-LOD streaming with squishy budgets

**Implementation Complexity:** Medium
- Leverage existing VT infrastructure
- Add mesh manifest at boot
- LOD-range reads with priority queue

#### 3.3 Prefetching System
**Impact:** Eliminate pop-in during camera cuts

**Current:** No prefetching
**Target:** Asset prediction for cutscenes, character spawns

**Implementation Complexity:** Medium
- Requires cutscene/script integration
- Camera cut detection system
- Asset dependency graphs

### Medium Priority Gaps

#### 3.4 UV Heuristics for Texture Priority
**Impact:** Better texture quality for important objects

**Current:** Distance-based priority only (VT feedback)
**Target:** Screen-space area × UV scale priority

**Implementation Complexity:** Medium
- Pre-compute UV scale per mesh at build time
- Integrate with VT feedback weighting
- Log-bucket approach for edge cases

#### 3.5 Static Distance Heap
**Impact:** Faster distance checks for streaming volumes

**Current:** Per-frame distance checks (implicit in TerrainTileCache)
**Target:** O(1) amortized heap-based checks

**Implementation Complexity:** Low-Medium
- Self-contained algorithm
- Apply to tile streaming, physics activation, LOD
- Travel-distance tracking required

#### 3.6 Over-Budget Penalty Scheme
**Impact:** Graceful degradation instead of stalls

**Current:** LRU eviction (may cause thrashing)
**Target:** Global penalty with progressive relaxation

**Implementation Complexity:** Low
- Modify VT request processing
- Add penalty uniform to LOD computation
- Gradual relaxation when stable

### Lower Priority Gaps

#### 3.7 Compile-Time Data Merging
**Impact:** Smaller runtime footprint, faster streaming

**Current:** Build-time tools generate assets
**Target:** Deep object flattening with index ranges

**Implementation Complexity:** High
- Requires restructuring asset representation
- Build tool changes
- Most benefit for very large worlds

#### 3.8 Virtual Memory for Streaming
**Impact:** Fragmentation-free mesh allocation

**Current:** VMA allocator handles GPU memory
**Target:** Virtual address space for instant realloc

**Implementation Complexity:** Medium
- Already using VMA (good foundation)
- Add virtual memory reservation layer
- Apply to mesh streaming

---

## Part 4: Implementation Plan

### Phase 1: Foundation Improvements

#### 1A: Static Distance Heap
Apply Ghost's distance heap to existing systems:

```cpp
// New: src/StaticDistanceHeap.h
template<typename T>
class StaticDistanceHeap {
    struct Entry {
        float nextCheckDistance;
        T item;
        size_t heapIndex;  // For O(1) removal
    };

    std::vector<Entry> heap;
    float currentTravel = 0.0f;

public:
    void update(const glm::vec3& playerPos);
    void add(T item, BoundingVolume bounds);
    void remove(T item);
    std::vector<T> getWokenItems();
};
```

**Apply to:**
- TerrainTileCache load/unload decisions
- TerrainPhysicsTiles activation
- Future region streaming

#### 1B: Over-Budget Penalty Scheme
Modify VirtualTextureSystem:

```cpp
// In VirtualTextureSystem.cpp
void processRequests() {
    float penalty = 0.0f;

    while (estimatedMemory(requests, penalty) > budget) {
        penalty += 0.1f;
    }

    for (auto& req : requests) {
        req.desiredMip = clamp(req.desiredMip + int(penalty), 0, maxMip);
    }

    // Relax when stable
    if (allRequestsLoaded && penalty > 0) {
        globalPenalty = max(0.0f, globalPenalty - 0.05f);
    }
}
```

### Phase 2: Mesh Streaming

#### 2A: Mesh Manifest
Build-time tool generates manifest of all meshes:

```cpp
// New: tools/mesh_manifest_generator/
struct MeshManifest {
    struct Entry {
        uint64_t hash;
        uint32_t fileOffset;
        uint32_t lodRanges[MAX_LODS];  // Byte offsets per LOD
        uint16_t compressedDistance;
    };
    std::vector<Entry> entries;
};
```

#### 2B: Streaming Mesh Loader
```cpp
// New: src/StreamingMeshSystem.h
class StreamingMeshSystem {
    MeshManifest manifest;
    std::unordered_map<uint64_t, LoadedLODRange> loadedMeshes;
    VirtualMemoryPool meshPool;  // Pre-reserved address space

public:
    void requestMesh(uint64_t hash, int desiredLOD);
    void update(const glm::vec3& cameraPos);
    MeshHandle getMesh(uint64_t hash);  // Returns whatever LOD is loaded
};
```

### Phase 3: Prefetching

#### 3A: Camera Cut Detection
```cpp
// New: src/CameraCutDetector.h
class CameraCutDetector {
public:
    bool detectCut(const Camera& prevCamera, const Camera& newCamera);
    void registerPrefetchCallback(PrefetchCallback cb);
};
```

#### 3B: Asset Dependency Graph
Build-time generation of what assets each cutscene/spawn needs:

```cpp
// In mission/cutscene metadata
struct CutsceneAssets {
    std::vector<uint64_t> requiredTextures;
    std::vector<uint64_t> requiredMeshes;
    float prefetchLeadTime;  // Seconds before cutscene
};
```

### Phase 4: Pack System (Long-Term)

This is a significant architecture change best done as a dedicated project:

1. **Pack format design:** Header, TOC, pointer-patch sections
2. **Build tool:** Aggregate assets into packs by spatial region
3. **Runtime loader:** Single-read path with virtual memory allocation
4. **Migration:** Gradual conversion of asset types

---

## Part 5: Quick Wins

These can be implemented with minimal changes:

### 5.1 Distance-Based LOD Clamping
For far-LOD content, clamp minimum camera distance:

```cpp
// In culling code
float effectiveDistance = max(actualDistance, mesh.minLODDistance);
int lod = computeLOD(effectiveDistance);
```

### 5.2 Texture Debug Visualization
Add Ghost-style debugging:

```cpp
// In VirtualTextureSystem
void renderDebugOverlay() {
    // Show: which textures are largest, which have bad UV heuristics
    // Filter by: texture name, shader, "badness" score
}
```

### 5.3 Breadcrumb Respawning
Track safe positions for faster reloads:

```cpp
class BreadcrumbTracker {
    std::deque<glm::vec3> breadcrumbs;

    void update(const glm::vec3& playerPos) {
        if (isSafeLocation(playerPos)) {
            breadcrumbs.push_back(playerPos);
            if (breadcrumbs.size() > MAX_BREADCRUMBS) {
                breadcrumbs.pop_front();
            }
        }
    }

    glm::vec3 getNearestSafeBreadcrumb(const glm::vec3& deathPos);
};
```

---

## Part 6: Memory Considerations

### Current Default Configuration
| Resource | Size | Notes |
|----------|------|-------|
| VT Physical Cache | 64 MB | Configurable via `cacheSizePixels` |
| VT Indirection | 1 MB | Scales with virtual texture size |
| VT Feedback/Staging | 5 MB | Per-frame buffers |
| Terrain Tiles (64 max) | ~128 MB | Heightmap data for physics/rendering |

These are current defaults, not hard constraints. The VT cache can be increased (e.g., 8K×8K = 256MB) depending on target hardware.

### Ghost of Tsushima Budget (PS4 Reference)
| Resource | Size | Notes |
|----------|------|-------|
| Texture space | 1 GB | Hard PS4 constraint |
| Stream meshes | Up to 1 GB | Characters, props, buildings |
| Packs | 1.6 GB | Aggregated asset data |
| Terrain probes | 44 MB | Runtime relighting |
| Render targets + heaps | Remainder | ~5.5GB total available |

### Key Efficiency Techniques (Hardware-Agnostic)
Ghost's streaming efficiency came from architectural choices, not just memory size:

1. **Single-read packs** - Reduced I/O operations regardless of budget
2. **UV heuristics** - Prioritized textures that matter on screen
3. **Static distance heap** - O(1) amortized checks saved CPU time
4. **Compile-time flattening** - 6-byte instances vs full objects
5. **Penalty scheme** - Graceful degradation without stalls

These techniques improve efficiency at any memory budget.

---

## Part 7: Testing Strategy

### Performance Metrics to Track
1. **Load times:** Cold boot, fast travel, death reload
2. **Pop-in:** Visible texture/mesh streaming during gameplay
3. **Frame time:** Streaming overhead in ms/frame
4. **Memory:** Peak vs average usage, fragmentation
5. **Disk I/O:** Reads per second, bytes per second

### Test Scenarios
1. **Camera fly-through:** Full speed across terrain
2. **Teleport stress test:** Random position jumps
3. **Combat scenario:** Many characters, effects, LOD changes
4. **Cutscene transitions:** Pre/post camera cut quality

---

## References

1. **GDC 2021:** "Zen of Streaming: Building and Loading the Ghost of Tsushima" - Adrian Bentley
2. **Related talks:**
   - Bill Rose: Particle and Cloth Simulation
   - Matt P: Samurai Landscapes (procedural tools, GPU culling)
   - Eric W: Procedural Grass
   - Jasmine: Lighting and Rendering (SIGGRAPH)
3. **BC6H Compression:** Krzysztof Narkowicz - Runtime BC6H compressor
4. **Terrain Rendering:** Frostbite heightmap techniques

---

## Appendix: Code Snippets from Talk

### Static Distance Heap Pseudocode
```cpp
void update(float travelDistance) {
    while (heap.top().nextCheck <= travelDistance) {
        auto item = heap.pop();
        float sdf = signedDistanceToVolume(item.volume, playerPos);

        if (sdf < 0) {
            activate(item.volume);
        } else {
            item.nextCheck = travelDistance + sdf;
            heap.push(item);
        }
    }

    // Rebase every 100m
    if (travelDistance > lastRebase + 100.0f) {
        for (auto& item : heap) {
            item.nextCheck -= 100.0f;
        }
        lastRebase = travelDistance;
    }
}
```

### UV Heuristic Log Bucketing
```cpp
// Pre-compute per mesh
struct UVHeuristic {
    uint16_t bucket[16];  // Area per log2(uv_scale) bucket
};

// Runtime: find 80th percentile
int computeDesiredMip(const UVHeuristic& h, float screenArea) {
    float total = 0;
    for (int i = 0; i < 16; i++) total += h.bucket[i];

    float threshold = total * 0.8f;
    float sum = 0;
    int bucket = 0;
    for (int i = 15; i >= 0; i--) {
        sum += h.bucket[i];
        if (sum >= threshold) {
            bucket = i;
            break;
        }
    }

    return bucket - log2(screenArea);
}
```

### Texture Streaming SIMD Measurement
```cpp
// Structure of arrays for SIMD processing
void measureTextureUsage(
    const float* triangleAreas,  // N triangles
    const uint16_t* uvScales,    // Packed UV scale per triangle
    uint32_t count,
    TextureUsage* out            // Per shader
) {
    // Find top 16 by screen size
    // Log2 ceiling via integer add trick
    // Keep bottom 16 for eviction candidates
}
```
