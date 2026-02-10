# Descriptor Indexing / Bindless Rendering Plan

## Motivation

The current renderer creates per-material descriptor sets with fixed bindings (0-20 in Set 0). Each material gets its own descriptor set per frame-in-flight, with placeholder textures written for unused PBR slots. This works but has scaling issues:

- **Descriptor set explosion**: N materials * 3 frames = 3N descriptor sets, each with the full binding layout
- **Redundant writes**: Common bindings (shadow maps, UBO, lights) are duplicated into every material's descriptor set
- **Binding rigidity**: Adding a new texture type requires layout changes, pipeline recreation, and touching every material
- **Draw call overhead**: Changing descriptor sets between materials is one of the more expensive state changes

Bindless rendering addresses this by moving to an indexing model: all textures live in a single large array, and shaders access them via integer index rather than fixed binding slot.

## Current State Analysis

### What exists today

| Aspect | Current approach |
|--------|-----------------|
| Layout | `DescriptorInfrastructure::addCommonDescriptorBindings()` — 21 fixed bindings in Set 0 |
| Per-material | `MaterialRegistry` stores `MaterialDef` with `Texture*` pointers; `MaterialDescriptorFactory` writes a full descriptor set per material |
| Textures | No central registry. `Texture` objects are created ad-hoc and referenced by raw pointer |
| Device features | Vulkan 1.2 with `timelineSemaphore` only. No descriptor indexing features enabled |
| Pool | `DescriptorManager::Pool` with auto-growth, standard pool sizes |
| Shaders | `bindings.h` defines slots shared between C++ and GLSL. Shaders use `layout(set=0, binding=N)` |

### Systems that use per-material descriptor sets

- `SceneObjectsDrawable` / `SceneObjectsShadowDrawable` — main scene meshes
- `SkinnedMeshRenderer` — animated characters (adds bone matrix binding 12)
- `MaterialDescriptorFactory` — factory that writes material textures into sets

### Systems with independent descriptor sets (not affected in Phase 1)

- Terrain, Sky, Water, Grass, Trees, Post-process, Compute passes — all use their own layouts on separate descriptor sets

## Design

### Core idea

Introduce a **bindless texture array** on a dedicated descriptor set. Textures are registered once and given a persistent integer handle. Shaders sample via `textures[nonuniformEXT(textureIndex)]`. Per-material descriptor sets are replaced by a material index pushed via push constant or instance buffer.

### Descriptor set strategy

```
Set 0: Per-frame global data (unchanged from current common bindings)
        UBO, shadow maps, light buffer, snow/cloud/wind UBOs, etc.
        Updated once per frame, shared by all draws.

Set 1: Bindless texture array (NEW)
        binding 0: sampler2D textures[]  — variable-count array
        Updated only when textures are added/removed.
        One set per frame-in-flight.

Set 2: Material data buffer (NEW)
        binding 0: MaterialData materials[]  — SSBO
        Each entry contains texture indices + scalar parameters.
        Updated when materials change.
```

Draw calls pass a `materialIndex` via push constant (or instance SSBO for batched draws). The fragment shader does:

```glsl
layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(push_constant) uniform PushConstants {
    uint materialIndex;
};

layout(set = 2, binding = 0, std430) buffer MaterialBuffer {
    MaterialData materials[];
};

// In fragment shader:
MaterialData mat = materials[materialIndex];
vec4 albedo = texture(textures[nonuniformEXT(mat.albedoIndex)], uv);
vec3 normal = texture(textures[nonuniformEXT(mat.normalIndex)], uv).xyz;
```

### MaterialData struct

```glsl
struct MaterialData {
    uint albedoIndex;
    uint normalIndex;
    uint roughnessIndex;
    uint metallicIndex;
    uint aoIndex;
    uint heightIndex;
    uint emissiveIndex;
    uint _pad0;
    vec4 scalarParams;  // roughness, metallic, emissive strength, alpha cutoff
};
```

This is 48 bytes (std430), fitting cleanly in an SSBO.

## Implementation Phases

### Phase 1: Foundation — Enable descriptor indexing features

**Goal**: Enable the Vulkan features and verify the device supports them. No rendering changes.

**Changes**:

1. `VulkanContext.cpp` — `selectPhysicalDevice()`:
   - Query `VkPhysicalDeviceDescriptorIndexingFeatures` (or `VkPhysicalDeviceVulkan12Features` fields)
   - Enable: `descriptorBindingPartiallyBound`, `descriptorBindingSampledImageUpdateAfterBind`, `descriptorBindingVariableDescriptorCount`, `runtimeDescriptorArray`, `shaderSampledImageArrayNonUniformIndexing`
   - These are all Vulkan 1.2 core (already requiring 1.2), so no new extensions needed

2. Add a capability query: `VulkanContext::supportsDescriptorIndexing()` that checks the features were actually enabled

3. Compile and run — no visual changes, just feature enablement

**Validation**: Build compiles, runs without validation errors. `SDL_Log` confirms descriptor indexing features enabled.

### Phase 2: TextureRegistry — Central texture handle management

**Goal**: All textures go through a single registry that assigns persistent indices. No shader changes yet.

**Changes**:

1. New class `TextureRegistry`:
   ```cpp
   class TextureRegistry {
   public:
       struct Handle {
           uint32_t index;  // Index into the bindless array
       };

       Handle registerTexture(vk::ImageView view, vk::Sampler sampler);
       void unregisterTexture(Handle handle);

       uint32_t getCount() const;
       // Writes the descriptor array update
       void updateDescriptorSet(vk::Device device, vk::DescriptorSet set);

   private:
       struct Entry {
           vk::ImageView view;
           vk::Sampler sampler;
           bool active = false;
       };
       std::vector<Entry> entries_;
       std::vector<uint32_t> freeList_;
   };
   ```

2. Register existing textures at load time:
   - Placeholder textures (white, black, normal-flat) get well-known indices (0, 1, 2)
   - Material textures registered when `MaterialRegistry::registerMaterial()` is called
   - `MaterialDef` gains `Handle` fields alongside existing `Texture*` pointers

3. No descriptor set or shader changes — this phase just builds the registry

**Validation**: Log all registered textures and their indices. Existing rendering unchanged.

### Phase 3: Bindless descriptor set layout and allocation

**Goal**: Create the bindless descriptor set (Set 1) and the material SSBO (Set 2). Not yet used by shaders.

**Changes**:

1. Create bindless descriptor set layout with `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT` and `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT` and `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`:
   ```cpp
   // In new BindlessDescriptorManager or extend DescriptorInfrastructure
   auto bindingFlags = vk::DescriptorBindingFlags{
       vk::DescriptorBindingFlagBits::ePartiallyBound |
       vk::DescriptorBindingFlagBits::eUpdateAfterBind |
       vk::DescriptorBindingFlagBits::eVariableDescriptorCount
   };
   ```

2. Allocate with `VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT`

3. Create material SSBO (one per frame-in-flight):
   ```cpp
   struct GPUMaterialData {
       uint32_t albedoIndex;
       uint32_t normalIndex;
       uint32_t roughnessIndex;
       uint32_t metallicIndex;
       uint32_t aoIndex;
       uint32_t heightIndex;
       uint32_t emissiveIndex;
       uint32_t _pad0;
       float roughness;
       float metallic;
       float emissiveStrength;
       float alphaCutoff;
   };
   ```

4. Update pipeline layout to include Sets 0, 1, 2 (Set 0 stays as-is)

5. Populate the material SSBO from `MaterialRegistry` data + `TextureRegistry` handles

**Validation**: Build compiles. Descriptor sets allocated without validation errors. Existing rendering still uses old path.

### Phase 4: Shader migration — Fragment shader uses bindless

**Goal**: Main scene fragment shader reads from the bindless array. Visual output should be identical.

**Changes**:

1. Add GLSL extension in shaders that use material textures:
   ```glsl
   #extension GL_EXT_nonuniform_qualifier : require
   ```

2. New shader include `bindless_material.glsl`:
   ```glsl
   layout(set = 1, binding = 0) uniform sampler2D globalTextures[];

   layout(set = 2, binding = 0, std430) readonly buffer MaterialBuffer {
       MaterialData materials[];
   };

   vec4 sampleMaterialAlbedo(uint matIndex, vec2 uv) {
       return texture(globalTextures[nonuniformEXT(materials[matIndex].albedoIndex)], uv);
   }
   // Similar helpers for normal, roughness, etc.
   ```

3. Modify `shader.frag` to use bindless path:
   - Replace `layout(set=0, binding=1) uniform sampler2D diffuseMap` with bindless lookup
   - Material index comes from push constant
   - Keep Set 0 bindings unchanged (UBO, shadow maps, lights — these are global)

4. Add push constant range for `materialIndex` to the pipeline layout

5. At draw time: `cmd.pushConstants(layout, stages, 0, sizeof(uint32_t), &materialIndex)` before each draw

**Validation**: Scene renders identically. Toggle between old/new path with a runtime flag to compare.

### Phase 5: Remove per-material descriptor sets

**Goal**: Clean up the old path once bindless is verified working.

**Changes**:

1. `MaterialDescriptorFactory` no longer writes per-material descriptor sets for the main rendering path
2. `MaterialRegistry` drops the `descriptorSets[materialId][frameIndex]` storage
3. `SceneObjectsDrawable` binds Sets 0+1+2 once, then uses push constants per draw
4. Shadow rendering: either uses its own minimal bindless path or keeps fixed binding (shadows rarely need textures beyond alpha-test)
5. Remove placeholder texture writes from common descriptor binding

**Validation**: Descriptor pool usage drops significantly. No visual regression.

### Phase 6: Batched rendering with instance buffer

**Goal**: Eliminate per-draw push constants by packing materialIndex into instance data.

**Changes**:

1. The existing `BINDING_SCENE_INSTANCE_BUFFER` (binding 18 in Set 0) already exists for batched rendering
2. Add `materialIndex` field to the instance data struct
3. Vertex shader passes `materialIndex` to fragment as flat varying
4. Single `vkCmdDrawIndexedIndirect` per mesh group replaces individual draws
5. Combine with GPU-driven culling (`SceneCullCompute` already exists)

**Validation**: Draw call count drops. Frame time improves with many materials.

## Key Vulkan 1.2 Features Required

All of these are in Vulkan 1.2 core (promoted from `VK_EXT_descriptor_indexing`):

| Feature | Purpose |
|---------|---------|
| `runtimeDescriptorArray` | Allows `sampler2D textures[]` (unsized array) in shaders |
| `descriptorBindingPartiallyBound` | Not all array slots need valid descriptors |
| `descriptorBindingSampledImageUpdateAfterBind` | Can update the texture array without recreating the set |
| `descriptorBindingVariableDescriptorCount` | Array size determined at allocation time, not layout time |
| `shaderSampledImageArrayNonUniformIndexing` | `nonuniformEXT()` qualifier for dynamic indexing |

## Limits to query

```cpp
VkPhysicalDeviceDescriptorIndexingProperties props{};
// maxDescriptorSetUpdateAfterBindSampledImages — max textures in bindless array
// Typically 500,000+ on desktop GPUs, 16,384+ on mobile
```

A practical starting limit: **4096 textures**. The current scene uses far fewer.

## Files Modified Per Phase

### Phase 1
- `src/core/vulkan/VulkanContext.cpp` — enable features
- `src/core/vulkan/VulkanContext.h` — add capability query

### Phase 2
- New: `src/core/material/TextureRegistry.h`, `TextureRegistry.cpp`
- `src/core/material/MaterialRegistry.h` — add texture handles
- `src/core/material/MaterialRegistry.cpp` — register on material creation

### Phase 3
- `src/core/DescriptorInfrastructure.h/cpp` — add bindless layout + material SSBO
- `src/core/material/DescriptorManager.h` — support update-after-bind pool flag
- `shaders/bindings.h` — add Set 1 and Set 2 binding constants

### Phase 4
- New: `shaders/bindless_material.glsl`
- `shaders/shader.frag` — use bindless sampling
- `shaders/bindings.h` — bindless constants
- `src/core/DescriptorInfrastructure.cpp` — push constant range in pipeline layout
- `src/core/scene/SceneObjectsDrawable.cpp` — push material index at draw time

### Phase 5
- `src/core/material/MaterialDescriptorFactory.h/cpp` — remove per-material set creation
- `src/core/material/MaterialRegistry.h/cpp` — remove descriptor set storage
- `src/core/scene/SceneObjectsDrawable.cpp` — bind only global + bindless sets

### Phase 6
- `src/core/scene/SceneObjectsDrawable.cpp` — instance buffer with material index
- `shaders/shader.vert` — read materialIndex from instance data
- `shaders/shader.frag` — receive as flat varying
- Potentially combine with `SceneCullCompute` for GPU-driven indirect draws

## Risks and Considerations

- **MoltenVK**: The project has `docs/MOLTENVK_CONSTRAINTS.md`. MoltenVK supports argument buffers (Metal's equivalent) but descriptor indexing support has limits. Check `maxPerStageDescriptorUpdateAfterBindSampledImages`. The custom triplets in `triplets/` suggest restricted environment support — test early.

- **Validation layers**: Partially bound descriptors can mask bugs. Always test with validation layers that check descriptor indexing usage.

- **Sampler deduplication**: The bindless array uses `sampler2D` (combined image sampler). If many textures share the same sampler, consider splitting to separate `texture2D[]` + `sampler` bindings (requires `VK_KHR_sampled_image_array_non_uniform_indexing`). Start with combined; optimize later if sampler pressure is an issue.

- **Texture lifetime**: `TextureRegistry::unregisterTexture()` must ensure no in-flight frames reference the slot. Use a deferred-free queue keyed on frame index.

- **Subsystem isolation**: Only the main scene rendering (Set 0 users) should migrate initially. Terrain, water, sky, trees, grass, and post-process all have independent descriptor sets and can migrate independently later (or not at all — they have different access patterns).
