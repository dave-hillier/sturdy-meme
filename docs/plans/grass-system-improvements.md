# Grass System Improvement Plan

This document outlines a phased approach to improving the grass system architecture. Each phase produces a working, testable state.

## Overview

The improvements focus on:
1. Reducing coupling between systems
2. Clarifying resource ownership
3. Improving testability
4. Enabling future extensibility

---

## Phase 1: Extract DisplacementSystem

**Goal**: Move displacement texture ownership out of GrassSystem into a standalone system.

### Steps

1. **Create DisplacementSystem class**
   - New files: `src/vegetation/DisplacementSystem.h`, `DisplacementSystem.cpp`
   - Move displacement texture, sampler, and update compute pipeline from GrassSystem
   - Move `DisplacementSource` struct and related constants

2. **Define clean interface**
   ```cpp
   class DisplacementSystem {
   public:
       void init(const InitContext& ctx);
       void destroy();

       void recordUpdate(VkCommandBuffer cmd, uint32_t frameIndex,
                         const glm::vec3& regionCenter,
                         std::span<const DisplacementSource> sources);

       VkDescriptorImageInfo getDescriptorInfo() const;
       glm::vec2 getRegionCenter() const;
       float getRegionSize() const;
   };
   ```

3. **Update GrassSystem**
   - Remove displacement texture ownership
   - Accept `DisplacementSystem*` in constructor or via setter
   - Update descriptor set creation to use `DisplacementSystem::getDescriptorInfo()`

4. **Update LeafSystem**
   - Remove `getDisplacementImageView()` / `getDisplacementSampler()` calls to GrassSystem
   - Accept `DisplacementSystem*` directly

5. **Update VegetationSystemGroup**
   - Add `DisplacementSystem` to Bundle
   - Wire up in `createAll()`

### Validation
- Run application, verify grass still bends when player walks through
- Verify leaf system still responds to displacement
- Check no Vulkan validation errors

---

## Phase 2: Introduce VegetationRenderContext

**Goal**: Bundle per-frame render state into a single struct passed to all vegetation systems.

### Steps

1. **Define context struct**
   - New file: `src/vegetation/VegetationRenderContext.h`
   ```cpp
   struct VegetationRenderContext {
       uint32_t frameIndex;
       float time;
       glm::vec3 cameraPosition;
       glm::mat4 viewMatrix;
       glm::mat4 projectionMatrix;

       // Shared resources (non-owning)
       VkBuffer windUBO;
       VkDeviceSize windUBOOffset;
       VkDescriptorImageInfo displacementInfo;
       VkDescriptorImageInfo shadowMapInfo;
       VkDescriptorImageInfo cloudShadowInfo;

       const EnvironmentSettings* environment;
   };
   ```

2. **Update GrassSystem interface**
   - Change `recordDraw(cmd, frameIndex, time)` to `recordDraw(cmd, ctx)`
   - Change `recordShadowDraw(...)` similarly
   - Internal methods extract what they need from context

3. **Update LeafSystem interface**
   - Same pattern as GrassSystem

4. **Update TreeRenderer interface**
   - Same pattern

5. **Update Renderer call sites**
   - Build `VegetationRenderContext` once per frame
   - Pass to all vegetation systems

### Validation
- Verify all vegetation renders correctly
- Verify shadows work
- Verify wind animation works

---

## Phase 3: Decouple GrassSystem from ParticleSystem

**Goal**: Remove inheritance/composition dependency on ParticleSystem, use focused utilities instead.

### Steps

1. **Audit ParticleSystem usage in GrassSystem**
   - List which methods/members are actually used
   - Expected: lifecycle helpers, buffer management, descriptor pool

2. **Create ComputeCullHelper** (if needed)
   - Extract indirect draw buffer setup
   - Extract compute dispatch patterns
   - Or determine if `FrameIndexedBuffers` from BufferUtils is sufficient

3. **Refactor GrassSystem**
   - Replace `ParticleSystem` member with direct usage of:
     - `FrameIndexedBuffers` for instance buffers
     - `SystemLifecycleHelper` for init/destroy (or inline if simple)
     - Direct descriptor set management
   - Remove unused particle emission code paths

4. **Verify LeafSystem still works**
   - LeafSystem may still use ParticleSystem legitimately
   - Ensure changes don't break it

### Validation
- Compile succeeds
- Grass renders identically
- Memory usage same or lower

---

## Phase 4: Split GrassTileManager Responsibilities

**Goal**: Separate tile logic (pure data) from Vulkan resource management.

### Steps

1. **Create GrassTileTracker**
   - Pure logic class, no Vulkan dependencies
   - Manages: active tile set, LOD decisions, load/unload requests
   ```cpp
   class GrassTileTracker {
   public:
       struct TileRequest { TileCoord coord; bool load; /* or unload */ };

       std::vector<TileRequest> update(const glm::vec3& cameraPos);
       std::span<const TileCoord> getActiveTiles(uint32_t lod) const;
       bool isTileActive(TileCoord coord) const;
   };
   ```

2. **Create GrassTileResourcePool**
   - Manages Vulkan resources for tiles
   - Descriptor set pooling
   - Tile info buffer management
   ```cpp
   class GrassTileResourcePool {
   public:
       void allocateForTile(TileCoord coord);
       void releaseForTile(TileCoord coord);
       VkDescriptorSet getDescriptorSet(TileCoord coord) const;
   };
   ```

3. **Refactor GrassTileManager**
   - Compose `GrassTileTracker` + `GrassTileResourcePool`
   - Or replace entirely with the two new classes

4. **Add unit tests for GrassTileTracker**
   - Test LOD boundary decisions
   - Test hysteresis (load margin vs unload margin)
   - Test tile coordinate calculations

### Validation
- All existing behavior preserved
- New unit tests pass
- No performance regression

---

## Phase 5: LOD Strategy Interface

**Goal**: Make LOD behavior configurable without code changes.

### Steps

1. **Define interface**
   ```cpp
   struct IGrassLODStrategy {
       virtual ~IGrassLODStrategy() = default;
       virtual uint32_t getNumLODLevels() const = 0;
       virtual uint32_t getLODForDistance(float distance) const = 0;
       virtual float getTileSize(uint32_t lod) const = 0;
       virtual float getSpacingMultiplier(uint32_t lod) const = 0;
       virtual float getMaxDrawDistance() const = 0;
       virtual uint32_t getTilesPerAxis(uint32_t lod) const = 0;
   };
   ```

2. **Create DefaultGrassLODStrategy**
   - Implements current hardcoded behavior
   - Uses values from GrassConstants

3. **Create alternative strategies**
   - `PerformanceGrassLODStrategy`: 2 LODs, larger tiles, shorter draw distance
   - `QualityGrassLODStrategy`: 4 LODs, denser grass, longer draw distance

4. **Update GrassTileTracker**
   - Accept `IGrassLODStrategy*` in constructor
   - Use strategy for all LOD decisions

5. **Update GrassSystem**
   - Accept strategy in constructor or provide setter
   - Forward to tile tracker

6. **Expose in settings**
   - Add grass quality setting (Low/Medium/High/Ultra)
   - Map to appropriate strategy

### Validation
- Test each quality preset
- Verify smooth LOD transitions at all quality levels
- Measure performance difference between presets

---

## Phase 6: Async Tile Loading

**Goal**: Prevent frame hitches when many tiles need loading at once.

### Steps

1. **Create GrassTileLoadQueue**
   ```cpp
   class GrassTileLoadQueue {
   public:
       void enqueue(TileCoord coord, uint32_t priority);
       std::optional<TileCoord> dequeueNext();
       void cancel(TileCoord coord);

       void setMaxLoadsPerFrame(uint32_t max);
   };
   ```

2. **Implement priority ordering**
   - Priority based on: distance to camera, visibility (frustum), LOD level
   - Closer visible tiles load first

3. **Integrate with GrassTileManager**
   - `update()` enqueues load requests instead of immediate loading
   - Separate `processLoadQueue()` called each frame with budget

4. **Handle teleportation**
   - Detect large camera movement
   - Clear queue and prioritize new position
   - Consider showing loading indicator

5. **Add frame budget configuration**
   - Default: 2-3 tiles per frame
   - Configurable based on target framerate

### Validation
- Teleport across map, verify no frame hitch
- Verify tiles still load (may take a few frames)
- Verify no popping (fade-in still works)

---

## Phase 7: Debug Visualization

**Goal**: Add optional debug overlay for grass system state.

### Steps

1. **Define debug info struct**
   ```cpp
   struct GrassDebugInfo {
       uint32_t activeTilesPerLOD[3];
       uint32_t totalActiveInstances;
       uint32_t culledInstances;
       float lastComputeTimeMs;
       float lastRenderTimeMs;
       glm::vec3 cameraPosition;
       std::vector<TileCoord> loadingTiles;
   };
   ```

2. **Add GPU timestamp queries**
   - Wrap compute dispatch with timestamp queries
   - Wrap draw call with timestamp queries
   - Read back results (with appropriate latency)

3. **Create debug render pass**
   - Draw tile boundaries as wireframe boxes
   - Color code by LOD level (green/yellow/red)
   - Show loading tiles with different color

4. **Integrate with ImGui**
   - Add grass debug window
   - Show real-time stats
   - Toggle for tile boundary visualization

5. **Add debug hotkey**
   - Toggle grass debug overlay with key (e.g., F7)

### Validation
- Verify debug overlay shows correct tile counts
- Verify timing measurements are reasonable
- Verify overlay doesn't affect normal rendering

---

## Dependency Graph

```
Phase 1 (DisplacementSystem)
    │
    ▼
Phase 2 (VegetationRenderContext) ◄─── can start after Phase 1
    │
    ├───────────────────┐
    ▼                   ▼
Phase 3              Phase 4
(ParticleSystem)     (TileManager split)
    │                   │
    └─────────┬─────────┘
              ▼
         Phase 5 (LOD Strategy)
              │
              ▼
         Phase 6 (Async Loading)
              │
              ▼
         Phase 7 (Debug Viz)
```

Phases 3 and 4 can be done in parallel after Phase 2.

---

## Testing Strategy

Each phase should include:

1. **Manual visual testing**
   - Grass renders correctly
   - Wind animation works
   - LOD transitions smooth
   - Player displacement works

2. **Performance validation**
   - No regression in frame time
   - GPU usage similar or lower

3. **Vulkan validation**
   - No validation layer errors
   - No synchronization warnings

4. **Unit tests** (where applicable)
   - Phase 4: GrassTileTracker logic
   - Phase 5: LOD strategy calculations
   - Phase 6: Load queue priority ordering

---

## Progress

| Phase | Status | Notes |
|-------|--------|-------|
| 1 - DisplacementSystem | ✅ Complete | Extracted to standalone system |
| 2 - VegetationRenderContext | ✅ Complete | Context struct introduced |
| 3 - ParticleSystem decoupling | ✅ Complete | GrassSystem uses SystemLifecycleHelper + BufferSetManager directly |
| 4 - TileManager split | ✅ Complete | GrassTileTracker (logic) + GrassTileResourcePool (Vulkan) |
| 5 - LOD Strategy | ✅ Complete | IGrassLODStrategy interface with 4 presets (Default, Performance, Quality, Ultra) |
| 6 - Async Loading | ✅ Complete | GrassTileLoadQueue with priority and frame budget |
| 7 - Debug Viz | ✅ Complete | ImGui grass panel with LOD controls, statistics, and debug visualization toggles |

---

## Notes

- Each phase should be a separate branch/PR
- Phases are designed to be independently valuable
- Later phases can be skipped if not needed
- Phase 1 and 2 provide the most architectural benefit for moderate effort
