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

## Beyond Rendering: Other High-Value Areas

### 7. NPC Scaling with Shared Animation Templates

**Current:** Each NPC holds a full `AnimatedCharacter` instance (`NPCSimulation.h:122`). With 54 NPCs:
- 54 × sizeof(AnimatedCharacter) with bone matrices
- Each NPC owns animation state machine, layers, blend trees
- Memory scales linearly with NPC count

**With ECS:**
```cpp
// Shared archetypes - maybe 10 character types
struct AnimationArchetype {
    Skeleton* skeleton;
    std::vector<AnimationClip*> clips;
    AnimationStateMachine stateMachine;
};

// Per-entity: just playback state (tiny)
struct AnimationPlayback {
    ArchetypeId archetype;
    uint32_t currentClip;
    float currentTime;
    float blendWeight;
};

// Batch process all NPCs using same skeleton together
for (auto [e, playback] : world.query<AnimationPlayback>()) {
    auto& archetype = archetypes[playback.archetype];
    // Compute bone matrices using shared skeleton
}
```

**Capability:** Scale to 1000+ animated NPCs. Memory cost is per-archetype (10) not per-instance (1000).

### 8. Eliminating Parallel Index Arrays

**Current:** Fragile manual index mappings throughout the codebase:
```cpp
// SceneManager.cpp:203-241 - parallel arrays require manual sync
std::vector<PhysicsBodyID> scenePhysicsBodies;  // Same size as sceneObjects
for (size_t i = 0; i < scenePhysicsBodies.size(); i++) {
    if (scenePhysicsBodies[i] != INVALID_BODY_ID) {
        sceneObjects[i].transform = physics.getBodyTransform(scenePhysicsBodies[i]);
    }
}

// NPCData.h:54 - NPCs store indices into scene objects
std::vector<size_t> renderableIndices;

// SceneBuilder - hardcoded indices for special objects
size_t playerIndex, emissiveOrbIndex, capeIndex, flagIndex, poleIndex...
```

**With ECS:**
```cpp
// Physics is a component, not a parallel array
struct PhysicsBody {
    PhysicsBodyID bodyId;
};

// Sync system - no index bookkeeping
void syncPhysicsToTransform(World& world, PhysicsSystem& physics) {
    for (auto [e, body, transform] : world.query<PhysicsBody, Transform>()) {
        transform.matrix = physics.getBodyTransform(body.bodyId);
    }
}

// Add/remove physics at runtime - just add/remove component
world.add<PhysicsBody>(entity, physics.createBox(size));
world.remove<PhysicsBody>(entity);  // Cleanup automatic
```

**Capability:** No index synchronization bugs. Add/remove physics at runtime without bookkeeping.

### 9. Dynamic Entity Composition

**Current:** Adding a new entity type (vehicle, destructible, interactable) requires:
- Extending Renderable struct with type-specific fields
- New fields in hot path even for objects that don't use them
- Modifying multiple systems to handle new type

**With ECS:**
```cpp
// Compose entities from components - no struct changes
auto vehicle = world.create();
world.add<Transform>(vehicle, position);
world.add<Mesh>(vehicle, vehicleMesh);
world.add<PhysicsBody>(vehicle, vehicleCollider);
world.add<Driveable>(vehicle, {maxSpeed, acceleration});
world.add<Damageable>(vehicle, {health: 100});

auto prop = world.create();
world.add<Transform>(prop, position);
world.add<Mesh>(prop, propMesh);
// No physics, not driveable, not damageable - no cost for unused features

auto destructible = world.create();
world.add<Transform>(destructible, position);
world.add<Mesh>(destructible, crateMesh);
world.add<Damageable>(destructible, {health: 25});
world.add<SpawnsDebris>(destructible, {debrisMesh, count: 5});
```

**Capability:** Prototype new entity types without touching core code. Each entity pays only for components it has.

### 10. Unified LOD Scheduling

**Current:** LOD duplicated across systems with different implementations:
- `NPCSimulation` has Real/Bulk/Virtual tiers with frame counters
- `TreeSystem` has separate LOD logic
- Each system polls camera position independently
- LOD transitions can be inconsistent across systems

**With ECS:**
```cpp
struct LODController {
    std::array<float, 3> thresholds;  // Distance thresholds
    uint8_t currentLevel;              // 0=high, 1=medium, 2=low
    uint16_t updateInterval;           // Frames between updates at this LOD
    uint16_t frameCounter;
};

// Single system manages all LOD
void updateLOD(World& world, glm::vec3 cameraPos) {
    for (auto [e, transform, lod] : world.query<Transform, LODController>()) {
        float dist = distance(cameraPos, transform.position);
        lod.currentLevel = dist < lod.thresholds[0] ? 0
                         : dist < lod.thresholds[1] ? 1 : 2;
        lod.updateInterval = (lod.currentLevel == 0) ? 1
                           : (lod.currentLevel == 1) ? 60 : 600;
    }
}

// Animation system respects LOD
void updateAnimations(World& world, float dt) {
    for (auto [e, anim, lod] : world.query<AnimationPlayback, LODController>()) {
        if (++lod.frameCounter >= lod.updateInterval) {
            lod.frameCounter = 0;
            updateAnimation(anim, dt * lod.updateInterval);
        }
    }
}

// Render system uses LOD for mesh selection
for (auto [e, mesh, lod] : world.query<LODMesh, LODController>()) {
    Mesh* toRender = mesh.levels[lod.currentLevel];
    // ...
}
```

**Capability:** Consistent LOD behavior across all entity types. Single place to tune LOD thresholds. Systems automatically respect LOD without custom logic.

## Current Pain Points Summary

| Issue | Location | ECS Solution |
|-------|----------|--------------|
| Parallel physics arrays | `SceneManager.cpp:88` | Physics as component |
| NPC renderable indices | `NPCData.h:54` | Entity owns all its data |
| Hardcoded special objects | `SceneBuilder` (10+ indices) | Query by component |
| Per-NPC AnimatedCharacter | `NPCSimulation.h:122` | Shared archetypes |
| Scattered LOD logic | Multiple systems | Unified LODController |
| Transform duplication | NPCData + Renderable | Single Transform component |
| Type-specific Renderable fields | `RenderableBuilder.h` | Sparse components |

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
