# ECS Architecture Exploration

This document explores how an Entity Component System could enable new rendering capabilities rather than just reorganizing existing code.

## Current Renderer Limitations

### The Bottleneck: O(N) Draw Calls

The current rendering path in `HDRPassRecorder::recordSceneObjects()`:

```
For each Renderable:
  → Set push constants (128 bytes)
  → Bind descriptor set (if material changed)
  → vkCmdDrawIndexed (instanceCount=1)
```

Each object is an individual draw call. At 1000+ objects, CPU time for draw submission dominates frame time while GPU sits idle.

### Current Data Layout

`Renderable` is an Array-of-Structs with ~20 fields:

```cpp
struct Renderable {
    glm::mat4 transform;
    Mesh* mesh;
    MaterialId materialId;
    float roughness, metallic, emissiveIntensity;
    // Tree-specific:
    std::string barkType, leafType;
    int leafInstanceIndex, treeInstanceIndex;
    // NPC-specific:
    float hueShift;
    // ... more fields
};
```

Every object pays memory cost for every field. Adding features means expanding this struct.

### What Exists But Isn't Connected

- **HiZSystem**: GPU occlusion culling with indirect draw buffers - used for tree impostors only
- **SceneInstanceBuffer**: Instanced rendering infrastructure - used for shadow pass only
- **CullObjectData**: Bounding sphere + AABB for GPU culling - not used for main scene

## The Core Insight: ECS Data Layout = GPU Buffer Layout

An ECS naturally produces dense, homogeneous arrays - exactly what GPUs want:

```cpp
// ECS component arrays map directly to SSBOs
std::vector<Transform> transforms;     // → GPU SSBO binding 0
std::vector<MaterialData> materials;   // → GPU SSBO binding 1
std::vector<BoundingSphere> bounds;    // → GPU SSBO binding 2
```

This enables GPU-driven rendering where compute shaders do the work instead of CPU iteration.

## New Capabilities Enabled

### 1. GPU Culling + Indirect Draw (10x Object Count)

**Current:** CPU iterates 1000 objects → 1000 drawIndexed calls → GPU waits

**With ECS:**
```
1. Upload Transform[] to GPU once (bulk memcpy)
2. Compute shader reads BoundingSphere[], frustum planes
3. Compute shader writes IndirectDrawCommand[] for visible objects
4. Single vkCmdDrawIndexedIndirectCount submits all visible
```

**Capability:** 5000-10000 scene objects at same CPU cost as 100 today.

### 2. Automatic Instancing by Component Query

**Current:** Shadow pass manually groups by mesh. Main pass doesn't batch.

**With ECS:**
```cpp
// Query groups entities by (mesh, material) automatically
auto batches = world.query<Transform, MeshRef, MaterialRef>()
    .group_by([](auto& t, auto& m, auto& mat) {
        return std::make_tuple(m.mesh, mat.id);
    });

for (auto& [key, entities] : batches) {
    uploadTransforms(entities);
    drawIndexed(mesh->indexCount, entities.size());  // Single call
}
```

**Capability:** 1000 rocks with same mesh = 1 draw call, not 1000.

### 3. Sparse Render Features

**Current:** Adding outline support means adding fields to every Renderable.

**With ECS:**
```cpp
// Only outlined entities have this component
struct Outlined {
    glm::vec3 color;
    float thickness;
};

// Outline pass only iterates entities with Outlined
for (auto [e, transform, mesh, outline] : world.query<Transform, Mesh, Outlined>()) {
    drawOutline(transform, mesh, outline);
}
```

**Capability:** Add render features (decals, damage, selection, translucency) without touching core structs. Each feature is a sparse component on relevant entities only.

### 4. Visibility as a Component

**Current:** No culling before draw submission. Off-screen objects still cost CPU.

**With ECS:**
```cpp
struct Visible {};  // Tag component, zero bytes

// Compute or CPU pass sets visibility
void updateVisibility(World& world, Frustum frustum) {
    for (auto [e, bounds] : world.query<BoundingSphere>()) {
        if (frustum.contains(bounds))
            world.add<Visible>(e);
        else
            world.remove<Visible>(e);
    }
}

// Render queries only visible entities
for (auto [e, t, m] : world.query<Transform, Mesh, Visible>()) {
    // Only visible objects reach here
}
```

**Capability:** Spatial queries become component queries. Visibility, shadow participation, LOD level - same pattern.

### 5. Render Passes as Component Queries

**Current:** Hardcoded HDR slots (Sky+Terrain, Scene+Skinned, Grass+Water).

**With ECS:**
```cpp
// Components declare render pass participation
struct CastsShadow {};
struct Transparent { float sortKey; };
struct Reflective {};

void shadowPass() {
    for (auto [e, t, m] : world.query<Transform, Mesh, CastsShadow>()) { ... }
}

void transparentPass() {
    auto query = world.query<Transform, Mesh, Transparent>();
    std::sort(query, by_sortKey);  // Back-to-front
    for (auto [e, t, m, trans] : query) { ... }
}
```

**Capability:** Add new render passes without modifying existing code. Reflection pass? Query for `Reflective` component.

### 6. Dynamic Material Composition

**Current:** MaterialId → pre-baked descriptor set. New effects require new materials.

**With ECS:**
```cpp
struct BaseMaterial { TextureHandle albedo, normal; };
struct WetnessOverlay { float wetness; };
struct DamageOverlay { TextureHandle damageMap; float amount; };

for (auto [e, base] : world.query<BaseMaterial>()) {
    auto* wet = world.try_get<WetnessOverlay>(e);
    auto* damage = world.try_get<DamageOverlay>(e);

    MaterialParams params = base.toParams();
    if (wet) params.applyWetness(wet->wetness);
    if (damage) params.applyDamage(damage->damageMap, damage->amount);

    draw(e, params);
}
```

**Capability:** Runtime material modification. Objects get wet in rain, dry over time - just component values.

## Architectural Shift

### Current Flow
```
CPU: for each object → set push constants → drawIndexed
GPU: execute draws sequentially
CPU: blocked during submission
```

### ECS-Enabled Flow
```
CPU: upload component arrays to GPU (bulk memcpy)
GPU: compute shader culls → builds indirect draws → executes
CPU: free to do other work
```

The ECS provides the data layout that makes GPU-driven rendering natural:
- Dense component arrays → SSBOs
- Component presence → bitmasks
- Entity IDs → buffer indices

## Incremental Migration Path

Each step is independently testable and provides measurable improvement:

### Step 1: Transform as Component
Keep Renderable but add parallel `Transform[]` that uploads to GPU as SSBO. Measure upload cost vs iteration cost.

### Step 2: Compute Visibility Pass
Use existing HiZ infrastructure with Transform SSBO. Output `Visible` bitfield. Compare visible count to total submitted.

### Step 3: Indirect Draw Path
Scene objects use `vkCmdDrawIndexedIndirectCount` based on visibility compute output. Measure draw call reduction.

### Step 4: Sparse Feature Components
Migrate hueShift, emissive, outline to sparse components. Only entities needing features pay the cost.

### Step 5: Full Component-Based Rendering
Remove Renderable struct entirely. All render data lives in components.

## What NOT to Migrate

These systems are already well-architected and wouldn't benefit:

- **FrameGraph** - Already handles render pass dependencies cleanly
- **Terrain System** - Specialized tile-based rendering, not entity-based
- **Weather/Wind/Time** - Global state, not per-entity
- **Post-processing** - Screen-space effects, no entity involvement

## Performance Expectations

| Metric | Current | With ECS + GPU-Driven |
|--------|---------|----------------------|
| Scene objects | 500-1000 comfortable | 5000-10000 comfortable |
| Draw calls | O(N) | O(unique meshes) |
| CPU frame time | 2-5ms at 1000 objects | <1ms at 5000 objects |
| Adding render feature | Modify Renderable struct | Add sparse component |
| Visibility culling | None (all submitted) | GPU compute pass |

## References

- [GPU-Driven Rendering Pipelines](https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf) - Wihlidal, Ubisoft
- [Rendering of Assassin's Creed III](https://www.gdcvault.com/play/1017626/Rendering-Assassin-s-Creed-III) - GPU-driven approach
- [entt](https://github.com/skypjack/entt) - Header-only ECS for C++
- [flecs](https://github.com/SanderMertens/flecs) - ECS with query caching
