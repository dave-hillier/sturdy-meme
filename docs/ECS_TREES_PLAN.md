# ECS Trees Migration Plan

Phased migration of trees from `TreeSystem`'s parallel arrays + `Renderable` structs to ECS components. Each step is minimal, produces a working build, and uses components initially as parameter containers before progressively making them authoritative.

## Current State

**TreeSystem owns everything in parallel arrays:**
- `std::vector<TreeInstanceData> treeInstances_` (position, rotation, scale, meshIndex, archetypeIndex)
- `std::vector<Mesh> branchMeshes_` (1 per unique tree geometry)
- `std::vector<TreeOptions> treeOptions_` (config per mesh)
- `std::vector<std::vector<LeafInstanceGPU>> leafInstancesPerTree_`
- `std::vector<LeafDrawInfo> leafDrawInfoPerTree_`
- `std::vector<AABB> fullTreeBounds_`
- `std::vector<TreeMeshData> treeMeshData_`
- `std::vector<Renderable> branchRenderables_`
- `std::vector<Renderable> leafRenderables_`

**Flow:** `addTree()` populates arrays → `createSceneObjects()` builds `Renderable` structs → `TreeRenderer::render()` iterates renderables, reading `barkType`, `leafInstanceIndex`, `treeInstanceIndex`, etc.

**Existing ECS components (already defined, unused by trees):** `TreeData`, `BarkType`, `LeafType`, `Transform`, `MeshRef`, `BoundingSphere`, `CastsShadow`, `LODController`.

---

## Phase 1: Create Tree Entities (Components as Parameter Containers)

**Goal:** Each tree gets an ECS entity with components that mirror the existing data. TreeSystem arrays remain authoritative. No rendering changes.

### Step 1.1: TreeSystem gains a World reference

- `TreeSystem::InitInfo` adds `ecs::World* world`
- `TreeSystem` stores `ecs::World* world_`
- Pass it during construction from `VegetationSystemGroup::createAll()`
- No other changes

**Files:** `TreeSystem.h`, `TreeSystem.cpp`, `VegetationSystemGroup.h/cpp`

**Testing:** Builds and runs unchanged. No visible difference.

### Step 1.2: Create entities in addTree()

When `addTree()` (and `addTreeFromStagedData()`) stores a new `TreeInstanceData`, also create an entity:

```cpp
Entity e = world_->create();
world_->emplace<ecs::Transform>(e, instance.getTransformMatrix());
world_->emplace<ecs::TreeData>(e, TreeData{
    .leafInstanceIndex = static_cast<int>(meshIndex),
    .treeInstanceIndex = static_cast<int>(treeIdx),
    .leafTint = opts.leaves.tint,
    .autumnHueShift = opts.leaves.autumnHueShift
});
world_->emplace<ecs::BarkType>(e, BarkType{barkTypeIndex});
world_->emplace<ecs::LeafType>(e, LeafType{leafTypeIndex});
world_->emplace<ecs::BoundingSphere>(e, computeBoundingSphere(fullBounds, instance));
world_->emplace<ecs::CastsShadow>(e);
```

Store the entity handle alongside the tree instance (new field: `std::vector<ecs::Entity> treeEntities_`).

**Files:** `TreeSystem.h`, `TreeSystem.cpp`

**Testing:** Builds and runs. Query `world.view<TreeData>()` and verify entity count matches `getTreeCount()`.

### Step 1.3: Add a TreeTag component

Define a zero-size tag `struct TreeTag {};` in `Components.h` for fast tree entity queries. Attach to every tree entity.

**Files:** `Components.h`, `TreeSystem.cpp`

**Testing:** `world.view<TreeTag>().size()` equals tree count.

### Step 1.4: Entity cleanup on removeTree()

When `removeTree()` erases a tree from arrays, also destroy its entity (`world_->destroy(treeEntities_[index])`). Keep entity vector in sync with instance vector.

**Files:** `TreeSystem.cpp`

**Testing:** Add trees, remove one, verify entity count is tree count.

---

## Phase 2: Components Mirror Renderable Data

**Goal:** The `Renderable` struct's tree-specific fields are populated from ECS components instead of being hardcoded during `createSceneObjects()`. Components become the source of truth for tree metadata while `Renderable` still exists for rendering.

### Step 2.1: Add string-keyed type components

`BarkType` and `LeafType` currently store a `uint32_t typeIndex`. The rendering system uses string keys (`"oak"`, `"pine"`, etc.) for descriptor set lookup. Add string type name to the components:

```cpp
struct BarkType {
    uint32_t typeIndex = 0;
    std::string typeName = "oak";
};

struct LeafType {
    uint32_t typeIndex = 0;
    std::string typeName = "oak";
};
```

Set these in `addTree()` from `TreeOptions::bark.type` and `TreeOptions::leaves.type`.

**Files:** `Components.h`, `TreeSystem.cpp`

**Testing:** Builds. Check BarkType/LeafType components have correct strings for each tree.

### Step 2.2: createSceneObjects() reads from ECS

Change `createSceneObjects()` to read tree-specific properties from ECS components instead of directly from `treeOptions_[meshIndex]`:

```cpp
for (size_t treeIdx = 0; treeIdx < treeInstances_.size(); ++treeIdx) {
    Entity e = treeEntities_[treeIdx];
    const auto& treeData = world_->get<ecs::TreeData>(e);
    const auto& barkType = world_->get<ecs::BarkType>(e);
    const auto& leafType = world_->get<ecs::LeafType>(e);
    // ... use these to build Renderable
}
```

The parallel arrays still exist, but `Renderable` fields are now sourced from components.

**Files:** `TreeSystem.cpp`

**Testing:** Builds and renders identically. Visual regression check - all tree types should have correct bark/leaf textures.

### Step 2.3: Add MeshRef component

Attach `MeshRef` to each tree entity, pointing to the branch mesh:

```cpp
world_->emplace<ecs::MeshRef>(e, &branchMeshes_[meshIndex]);
```

Not consumed yet by rendering - preparation for Phase 4.

**Files:** `TreeSystem.cpp`

**Testing:** Builds. MeshRef pointers are valid for each tree entity.

---

## Phase 3: TreeRenderer Reads Components Alongside Renderables

**Goal:** The renderer can optionally access ECS data for trees, while still driven by the `Renderable` vector. This prepares for the eventual removal of tree fields from `Renderable`.

### Step 3.1: TreeRenderer gains World reference

- Add `ecs::World* world_` to `TreeRenderer`
- Pass via `InitInfo` or a setter

**Files:** `TreeRenderer.h`, `TreeRenderer.cpp`, `VegetationSystemGroup.cpp`

**Testing:** Builds and runs unchanged.

### Step 3.2: Use entity to resolve tree metadata in render()

Currently `render()` reads `renderable.barkType`, `renderable.treeInstanceIndex`, etc. Add a parallel code path that resolves these from the entity when available:

```cpp
for (const auto& renderable : treeSystem.getBranchRenderables()) {
    // Use existing fields - no behavior change yet
    const std::string& bark = renderable.barkType;
    int treeIdx = renderable.treeInstanceIndex;
    // ...
}
```

This step is about verifying the entity lookup produces identical values. Add asserts in debug builds:

```cpp
#ifndef NDEBUG
if (treeIdx >= 0 && world_) {
    Entity e = treeSystem.getTreeEntity(treeIdx);
    assert(world_->get<ecs::BarkType>(e).typeName == renderable.barkType);
}
#endif
```

**Files:** `TreeSystem.h` (add `getTreeEntity()`), `TreeRenderer.cpp`

**Testing:** Runs without assertion failures. All trees render correctly.

### Step 3.3: Expose tree entity accessor

Add `TreeSystem::getTreeEntity(uint32_t index)` that returns the entity for a given tree index. Also add `TreeSystem::getTreeEntities()` for bulk iteration.

**Files:** `TreeSystem.h`, `TreeSystem.cpp`

**Testing:** API exists and returns valid entities.

---

## Phase 4: Remove Tree Fields from Renderable

**Goal:** Tree-specific fields in `Renderable` (`barkType`, `leafType`, `leafInstanceIndex`, `treeInstanceIndex`, `leafTint`, `autumnHueShift`) are removed. Renderer reads them from ECS.

### Step 4.1: Renderer reads bark/leaf type from ECS

In `TreeRenderer::render()`, replace `renderable.barkType` lookups with component reads:

```cpp
Entity e = treeSystem.getTreeEntity(renderable.treeInstanceIndex);
const auto& barkType = world_->get<ecs::BarkType>(e);
auto descriptorSet = getBranchDescriptorSet(frameIndex, barkType.typeName);
```

Same for leaf type in leaf rendering.

**Files:** `TreeRenderer.cpp`

**Testing:** All trees render with correct textures. Performance unchanged.

### Step 4.2: Renderer reads leaf tint and autumn shift from ECS

Replace `renderable.leafTint` and `renderable.autumnHueShift` with reads from `TreeData` component.

**Files:** `TreeRenderer.cpp`

**Testing:** Seasonal coloring and tint still correct.

### Step 4.3: Renderer reads leaf/tree instance indices from ECS

Replace `renderable.leafInstanceIndex` and `renderable.treeInstanceIndex` with reads from `TreeData` component.

**Files:** `TreeRenderer.cpp`

**Testing:** Leaf instancing and LOD transitions still work.

### Step 4.4: Remove tree fields from Renderable and RenderableBuilder

Delete the tree-specific fields and builder methods:
- `Renderable`: Remove `barkType`, `leafType`, `leafInstanceIndex`, `treeInstanceIndex`, `leafTint`, `autumnHueShift`, `setTreeProperties()`, `isTree()`
- `RenderableBuilder`: Remove `withBarkType()`, `withLeafType()`, `withLeafTint()`, `withAutumnHueShift()`, `withTreeInstanceIndex()`, `withLeafInstanceIndex()`, `withTreeData()`

Update any other code that checks `renderable.isTree()` to use `world.has<TreeTag>(entity)`.

**Files:** `RenderableBuilder.h`, `RenderableBuilder.cpp`, `TreeSystem.cpp`, any other consumers of `isTree()`

**Testing:** Builds, runs, all trees render correctly. No tree-specific data in Renderable.

---

## Phase 5: Simplify TreeSystem Arrays

**Goal:** Reduce duplication between TreeSystem's arrays and ECS components. Transform and bounds are sourced from ECS. TreeSystem retains ownership of GPU resources (meshes, buffers, textures).

### Step 5.1: TreeInstanceData uses ECS Transform

Remove `Transform transform` from `TreeInstanceData`. Position/rotation/scale is now in the `ecs::Transform` component. TreeSystem reads from ECS when it needs the transform.

`TreeInstanceData` becomes:
```cpp
struct TreeInstanceData {
    uint32_t meshIndex;
    uint32_t archetypeIndex;
    bool isSelected;
};
```

The `getTransformMatrix()` accessor moves to a free function that takes `ecs::World` + entity.

**Files:** `TreeSystem.h`, `TreeSystem.cpp`, any callers of `TreeInstanceData::position()`, `rotation()`, `scale()`

**Testing:** Builds. Trees appear at correct positions. Camera/physics interactions unchanged.

### Step 5.2: Bounds from ECS BoundingSphere

Remove `fullTreeBounds_` vector. Compute and store bounds in the `ecs::BoundingSphere` component. TreeLODSystem and culling systems read from ECS.

**Files:** `TreeSystem.h`, `TreeSystem.cpp`, `TreeLODSystem.cpp`, `TreeLeafCulling.cpp`

**Testing:** LOD transitions and culling work correctly at all distances.

### Step 5.3: Selected tree via ECS component

Replace `selectedTreeIndex_` integer with a `SelectedTree` tag component. `selectTree(index)` removes the tag from the old entity and adds it to the new one.

```cpp
struct SelectedTreeTag {};
```

**Files:** `Components.h`, `TreeSystem.h`, `TreeSystem.cpp`, `TreeEditSystem` (if it reads selectedTreeIndex)

**Testing:** Tree editing still works. Selecting trees in the editor highlights the correct one.

---

## Phase 6: ECS-Driven Tree Iteration

**Goal:** Rendering iterates ECS entities instead of `Renderable` vectors. TreeSystem's `branchRenderables_` and `leafRenderables_` are eliminated.

### Step 6.1: TreeRenderer iterates entities for branches

Replace the branch rendering loop:

```cpp
// Old: for (const auto& renderable : treeSystem.getBranchRenderables()) { ... }
// New:
auto view = world_->view<ecs::Transform, ecs::TreeData, ecs::BarkType, ecs::MeshRef, ecs::TreeTag>();
for (auto [entity, transform, treeData, barkType, meshRef] : view.each()) {
    // Bind mesh, descriptor set, push constants
    // Draw
}
```

**Files:** `TreeRenderer.cpp`

**Testing:** Branch rendering identical. Frame time stable.

### Step 6.2: TreeRenderer iterates entities for leaves

Same pattern for leaf rendering, using `LeafType` and `TreeData` components.

**Files:** `TreeRenderer.cpp`

**Testing:** Leaf rendering identical with correct alpha test and tinting.

### Step 6.3: Remove branchRenderables_ and leafRenderables_

Delete the renderable vectors from `TreeSystem`. Remove `getBranchRenderables()`, `getLeafRenderables()`, `createSceneObjects()`, `rebuildSceneObjects()`.

Update `SceneObjectsDrawable` to no longer fetch these vectors.

**Files:** `TreeSystem.h`, `TreeSystem.cpp`, `SceneObjectsDrawable.cpp`

**Testing:** Builds. Trees still render. No renderable vectors in tree system.

---

## Validation After Each Step

1. `cmake --preset debug && cmake --build build/debug` - compiles
2. `./run-debug.sh` - runs without crashes
3. Visual check - all tree types render correctly (bark textures, leaf types, LOD transitions, shadows)
4. Entity count matches tree count via `world.view<TreeTag>().size()`

## Risk Mitigation

- Each step leaves the existing rendering path functional until explicitly replaced
- Debug asserts in Phase 3 verify ECS data matches legacy data before switching
- TreeSystem retains GPU resource ownership (meshes, leaf SSBO, textures) throughout - only metadata moves to ECS
- The tree spatial index, leaf culling, and branch culling systems are not changed in this plan - they continue working with TreeSystem's internal data. A future plan can migrate those to ECS queries.
