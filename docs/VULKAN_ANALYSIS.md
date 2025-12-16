# Vulkan Usage Analysis

This document analyzes Vulkan usage patterns throughout the codebase, focusing on correctness, consistency, and opportunities to apply high-level helpers.

## Executive Summary

The codebase has **comprehensive high-level abstractions** with **excellent adoption** across systems. The sampler migration is complete and descriptor management is nearly fully migrated. Buffer creation remains the primary area for further migration.

**Current Statistics:**
- 3 files use raw `vkCreateDescriptorSetLayout` vs 20 using `LayoutBuilder` (87%)
- 0 files use raw `vkCreateSampler` - **100% migrated to `ManagedSampler`**
- 19 files use raw `vmaCreateBuffer` vs core infrastructure using `ManagedBuffer` (33%)
- 1 file uses raw `vkUpdateDescriptorSets` vs 27 using `SetWriter` (96%)

## Available RAII Wrappers (`VulkanRAII.h`)

The codebase has comprehensive RAII coverage for all major Vulkan object types:

### Buffer and Image Management

| Class | Purpose | Convenience Factories |
|-------|---------|----------------------|
| `ManagedBuffer` | VkBuffer + VmaAllocation | `createStaging`, `createReadback`, `createVertex`, `createIndex`, `createUniform`, `createStorage`, `createStorageHostReadable` |
| `ManagedImage` | VkImage + VmaAllocation | `create`, `fromRaw` |
| `ManagedImageView` | VkImageView | `create`, `fromRaw` |

### Sampler Management

| Class | Purpose | Convenience Factories |
|-------|---------|----------------------|
| `ManagedSampler` | VkSampler | `createNearestClamp`, `createLinearClamp`, `createLinearRepeat`, `createLinearRepeatAnisotropic`, `createShadowComparison`, `fromRaw` |

### Pipeline and Layout Management

| Class | Purpose | Features |
|-------|---------|----------|
| `ManagedDescriptorSetLayout` | VkDescriptorSetLayout | `create`, `fromRaw` |
| `ManagedPipelineLayout` | VkPipelineLayout | `create`, `fromRaw` |
| `ManagedPipeline` | VkPipeline | `createGraphics`, `createCompute`, `fromRaw` |
| `ManagedRenderPass` | VkRenderPass | `create`, `fromRaw` |
| `ManagedFramebuffer` | VkFramebuffer | `create`, `fromRaw` |

### Synchronization and Commands

| Class | Purpose | Features |
|-------|---------|----------|
| `ManagedCommandPool` | VkCommandPool | `create` |
| `ManagedSemaphore` | VkSemaphore | `create` |
| `ManagedFence` | VkFence | `create`, `createSignaled`, `wait`, `reset` |
| `CommandScope` | One-time command submission | RAII begin/end/submit |
| `ScopeGuard` | Generic cleanup helper | `dismiss` for success path |

---

## Descriptor Management (`DescriptorManager.h`)

### LayoutBuilder - Fluent Descriptor Set Layout Creation

```cpp
descriptorSetLayout = DescriptorManager::LayoutBuilder(device)
    .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
    .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)
    .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)
    .build();
```

**Adoption:** 20 files - Excellent (87%)

### SetWriter - Fluent Descriptor Set Updates

```cpp
DescriptorManager::SetWriter(device, descriptorSet)
    .writeBuffer(0, uniformBuffer, 0, sizeof(UBO))
    .writeImage(1, imageView, sampler)
    .update();
```

**Adoption:** 27 files - Excellent (96%)

### Pool - Auto-Growing Descriptor Pool

Automatically expands when exhausted. Used consistently across systems.

---

## Remaining Migration Work

### Files Still Using Raw `vkCreateDescriptorSetLayout` (3 files)

| File | Context | Notes |
|------|---------|-------|
| `CatmullClarkSystem.cpp` | 2 layouts | Complex multi-binding compute/render layouts |
| `PipelineBuilder.cpp` | 1 call | Legacy builder pattern |

### Files Still Using Raw `vkCreateSampler` (0 files)

**Complete!** All sampler creation now uses `ManagedSampler`.

### Files Still Using Raw `vmaCreateBuffer` (19 files)

Buffer creation is the largest remaining migration area:

| File | Calls | Purpose |
|------|-------|---------|
| `VirtualTextureFeedback.cpp` | 4 | Feedback chain storage buffers |
| `WaterTileCull.cpp` | 4 | Tile, counter, readback, indirect buffers |
| `CatmullClarkMesh.cpp` | 3 | Vertex, halfedge, face storage buffers |
| `CatmullClarkSystem.cpp` | 3 | Uniform and indirect buffers |
| `TerrainMeshlet.cpp` | 2 | Meshlet and error bound buffers |
| `DebugLineSystem.cpp` | 2 | Dynamic line vertex buffers |
| `SkinnedMeshRenderer.cpp` | 1 | Bone matrices uniform buffer |
| `AtmosphereLUTExport.cpp` | 1 | Staging buffer for export |
| `HiZSystem.cpp` | 1 | Object data storage buffer |
| `PostProcessSystem.cpp` | 1 | Histogram storage buffer |
| `CatmullClarkCBT.cpp` | 1 | CBT storage buffer |
| `TerrainCBT.cpp` | 1 | CBT storage buffer |
| `TerrainTileCache.cpp` | 1 | Tile info storage buffer |
| `LeafSystem.cpp` | 1 | Displacement region buffer |
| `FoamBuffer.cpp` | 1 | Wake uniform buffers |
| `WaterDisplacement.cpp` | 1 | Displacement storage buffer |
| `WaterSystem.cpp` | 1 | Water tile storage buffer |

**Common patterns requiring migration:**
1. Storage buffers for compute shaders
2. Per-frame uniform buffers with mapping
3. Indirect draw/dispatch buffers
4. Readback buffers for GPU->CPU data

### Files Still Using Raw `vkUpdateDescriptorSets` (1 file)

| File | Context | Notes |
|------|---------|-------|
| `TerrainSystem.cpp` | Line 537 | Batch update for terrain rendering |

---

## Best Practices

### Correct Pattern: Full RAII Usage

```cpp
// Good: Use RAII wrappers throughout
ManagedSampler sampler;
if (!ManagedSampler::createLinearClamp(device, sampler)) {
    return false;
}

auto layout = DescriptorManager::LayoutBuilder(device)
    .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT)
    .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)
    .build();

// Resources automatically cleaned up on destruction
```

### Reference Files (Good Examples)

| File | Demonstrates |
|------|--------------|
| `OceanFFT.cpp` | LayoutBuilder, SetWriter, ManagedSampler |
| `BloomSystem.cpp` | ManagedSampler, SetWriter |
| `FroxelSystem.cpp` | LayoutBuilder, ManagedSampler |
| `SSRSystem.cpp` | LayoutBuilder, SetWriter, ManagedSampler |
| `Texture.cpp` | ManagedBuffer, ManagedImage, ManagedSampler |
| `Mesh.cpp` | ManagedBuffer for vertex/index data |
| `Renderer.cpp` | ManagedSampler, LayoutBuilder |

---

## Migration Progress Summary

| Category | Raw API | Wrapper | Adoption |
|----------|---------|---------|----------|
| DescriptorSetLayout | 3 files | 20 files | 87% |
| Descriptor Updates | 1 file | 27 files | 96% |
| Sampler Creation | 0 files | 49 files | **100%** |
| Buffer Creation | 19 files | Core infra | 33% |

---

## Recommended Next Steps

### Priority 1: Buffer Migration (High Impact)
- **Virtual texture feedback** (4 calls) - Storage buffers
- **Water tile culling** (4 calls) - Mixed buffer types
- **Catmull-Clark subdivision** (6 calls) - Storage and uniform buffers
- Consider adding new ManagedBuffer factories:
  - `createIndirect()` for indirect draw/dispatch buffers
  - Per-frame buffer patterns via BufferUtils builders

### Priority 2: Final Descriptor Cleanup (Low Impact)
- Migrate `TerrainSystem.cpp` to use SetWriter
- Migrate `CatmullClarkSystem.cpp` to use LayoutBuilder
- Remove legacy `PipelineBuilder` descriptor layout creation

### Priority 3: Remove Deprecated Code
- Remove `BindingBuilder` usage from remaining systems
- Delete `BindingBuilder.h` and `BindingBuilder.cpp`

---

## Conclusion

The codebase has achieved **excellent RAII coverage** with:
- **100% sampler migration** - Complete
- **96% descriptor update migration** - Nearly complete
- **87% descriptor layout migration** - Well adopted

The remaining work focuses primarily on **buffer creation migration** (19 files), which involves specialized buffer types for compute shaders, per-frame data, and indirect rendering. The existing `ManagedBuffer` factories cover staging, vertex, index, and uniform buffers well, but storage buffers and indirect buffers are common patterns that could benefit from additional convenience factories.
