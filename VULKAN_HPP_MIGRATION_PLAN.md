# Vulkan-hpp Migration Plan

## Migration Status

- [x] **Phase 0**: Add `vk::raii::Device` to VulkanContext (foundation for vk::raii types)
- [ ] Phase 1: Deprecate VulkanRAII.h custom wrappers
- [ ] Phase 2: Deprecate VulkanBarriers.h and VulkanResourceFactory
- [x] Phase 3.1: TreeImpostorAtlas.cpp migrated to vk::raii types
- [ ] Phase 3: Migrate remaining high-usage files

---

## Current State Summary

| Category | Count |
|----------|-------|
| Raw Vulkan function calls (vk*) | 954 |
| Raw Vulkan struct usages (Vk*Info) | 519 |
| Custom RAII wrapper usages | 683 |

## Migration Strategy

### Approach
1. **Deprecate VulkanRAII.h**: Replace custom RAII wrappers with vulkan-hpp's `vk::raii::*` types
2. **Keep only VMA wrappers**: `ManagedBuffer` and `ManagedImage` stay (VMA doesn't integrate with vk::raii)
3. **Builder pattern**: Use `.set*()` methods, not positional constructors
4. **Incremental**: Each file conversion should compile and run

### Key Patterns to Apply

```cpp
// BEFORE (raw Vulkan C)
VkBufferCreateInfo bufferInfo{};
bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
bufferInfo.size = size;
bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

// AFTER (vulkan-hpp builder pattern)
auto bufferInfo = vk::BufferCreateInfo{}
    .setSize(size)
    .setUsage(vk::BufferUsageFlagBits::eVertexBuffer);
```

```cpp
// BEFORE (raw command calls)
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
vkCmdDraw(cmd, 3, 1, 0, 0);

// AFTER (method calls)
cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
cmd.draw(3, 1, 0, 0);
```

---

## Phase 1: Deprecate VulkanRAII.h Custom Wrappers

### Types to REMOVE (use vulkan-hpp equivalents)

| Custom Wrapper | vulkan-hpp Replacement | Usage Count |
|----------------|------------------------|-------------|
| ManagedImageView | vk::raii::ImageView | 14 files |
| ManagedSampler | vk::raii::Sampler | 41 files |
| ManagedPipeline | vk::raii::Pipeline | 53 files |
| ManagedPipelineLayout | vk::raii::PipelineLayout | 51 files |
| ManagedRenderPass | vk::raii::RenderPass | 9 files |
| ManagedFramebuffer | vk::raii::Framebuffer | 7 files |
| ManagedFence | vk::raii::Fence | 3 files |
| ManagedSemaphore | vk::raii::Semaphore | 3 files |
| ManagedCommandPool | vk::raii::CommandPool | 3 files |
| ManagedDescriptorSetLayout | vk::raii::DescriptorSetLayout | 44 files |
| CommandScope | vk::raii::CommandBuffer | - |
| RenderPassScope | cmd.beginRenderPass()/endRenderPass() | - |

### Types to KEEP (VMA integration required)

| Custom Wrapper | Reason | Action |
|----------------|--------|--------|
| ManagedBuffer | VMA allocates memory | Update to use vk::Buffer internally |
| ManagedImage | VMA allocates memory | Update to use vk::Image internally |

### Utilities to Keep
- `VK_CHECK` / `VK_CHECK_VOID` macros - still useful for error checking
- `ScopeGuard` - general utility pattern

---

## Phase 2: Deprecate Core Infrastructure Files

### 2.1 VulkanBarriers.h (160 usages) - DEPRECATE

This file provides barrier convenience functions that vulkan-hpp handles natively:

| Current | vulkan-hpp Replacement |
|---------|------------------------|
| `Barriers::computeToCompute(cmd)` | `cmd.pipelineBarrier(...)` with `vk::MemoryBarrier{}` |
| `Barriers::transitionImage(...)` | `cmd.pipelineBarrier(...)` with `vk::ImageMemoryBarrier{}` |
| `TrackedImage` | Inline layout tracking + vulkan-hpp barriers |
| `BarrierBatch` | `cmd.pipelineBarrier()` with `std::vector<vk::ImageMemoryBarrier>` |
| `ImageBarrier` | `vk::ImageMemoryBarrier{}` builder pattern |
| `ScopedComputeBarrier` | RAII pattern at call sites if needed |

**Migration approach**: Replace all Barriers:: calls at their call sites with direct vulkan-hpp barrier calls, then delete VulkanBarriers.h.

### 2.2 VulkanResourceFactory.cpp (94 usages) - DEPRECATE

This factory wraps vulkan-hpp with reinterpret_cast - unnecessary indirection:

| Current | vulkan-hpp Replacement |
|---------|------------------------|
| `createCommandPool(device, ...)` | `vk::raii::CommandPool(device, createInfo)` |
| `createSyncResources(...)` | `vk::raii::Semaphore`, `vk::raii::Fence` directly |
| `createDepthResources(...)` | VMA + `vk::raii::ImageView` at call site |
| `createFramebuffers(...)` | `vk::raii::Framebuffer` at call site |
| `createRenderPass(...)` | `vk::raii::RenderPass` at call site |
| `createStagingBuffer(...)` | `ManagedBuffer::create()` directly (keep VMA wrapper) |
| `createSampler*(...)` | `vk::raii::Sampler` directly |

**Migration approach**: Inline factory calls at their call sites using vk::raii constructors, then delete VulkanResourceFactory.cpp/.h.

---

## Phase 3: High-Usage Files Migration

### 3.1 TreeImpostorAtlas.cpp (134 usages) ✅ DONE
- ✅ Replace ManagedImageView → vk::raii::ImageView
- ✅ Replace ManagedFramebuffer → vk::raii::Framebuffer
- ✅ Replace ManagedRenderPass → vk::raii::RenderPass
- ✅ Replace ManagedPipeline → vk::raii::Pipeline
- ✅ Replace ManagedPipelineLayout → vk::raii::PipelineLayout
- ✅ Replace ManagedDescriptorSetLayout → vk::raii::DescriptorSetLayout
- ✅ Replace ManagedSampler → vk::raii::Sampler
- ✅ Convert command buffer recording to vulkan-hpp method calls
- ✅ Add raiiDevice to InitInfo

### 3.2 TreeLODSystem.cpp (121 usages)
- Replace ManagedPipeline → vk::raii::Pipeline
- Replace ManagedDescriptorSetLayout → vk::raii::DescriptorSetLayout
- Convert compute dispatch calls

### 3.3 TreeRenderer.cpp (103 usages)
- Replace ManagedPipeline → vk::raii::Pipeline
- Replace ManagedPipelineLayout → vk::raii::PipelineLayout
- Convert draw calls

### 3.4 TerrainSystem.cpp (91 usages)
- Replace all Managed* types with vk::raii::* equivalents
- Convert command buffer recording

### 3.5 PostProcessSystem.cpp (86 usages)
- Replace ManagedFramebuffer → vk::raii::Framebuffer
- Replace ManagedRenderPass → vk::raii::RenderPass
- Convert post-processing pipeline

### 3.6 GrassSystem.cpp (81 usages)
- Replace all Managed* types
- Convert indirect draw calls

### 3.7 CatmullClarkSystem.cpp (81 usages)
- Replace ManagedPipeline → vk::raii::Pipeline
- Convert subdivision compute pipelines

---

## Phase 4: Remaining Files

Migrate all other files using Managed* types:
- WaterGBuffer, WaterDisplacement
- SkinnedMesh
- Sky rendering
- Shadow mapping
- Any remaining render systems

---

## Phase 5: Final Cleanup

### 5.1 Update VulkanRAII.h
- Remove all deprecated Unique*/Managed* types
- Keep only:
  - `ManagedBuffer` (VMA wrapper)
  - `ManagedImage` (VMA wrapper)
  - `VK_CHECK` macros
  - `ScopeGuard`

### 5.2 Code cleanup
- Ensure all files use `#include <vulkan/vulkan.hpp>`
- Remove `#include <vulkan/vulkan.h>` (raw C header)
- Verify build and run

---

## Files to Migrate (Priority Order)

| Priority | File | Usages | Status | Primary Changes |
|----------|------|--------|--------|-----------------|
| 1 | VulkanBarriers.h | 160 | Pending | Convert barrier structs/calls |
| 2 | TreeImpostorAtlas.cpp | 134 | ✅ Done | ImageView, Framebuffer, RenderPass |
| 3 | TreeLODSystem.cpp | 121 | Pending | Pipeline, DescriptorSetLayout |
| 4 | TreeRenderer.cpp | 103 | Pending | Pipeline, PipelineLayout |
| 5 | VulkanResourceFactory.cpp | 94 | Pending | Resource creation patterns |
| 6 | TerrainSystem.cpp | 91 | Pending | All Managed* types |
| 7 | PostProcessSystem.cpp | 86 | Pending | Framebuffer, RenderPass |
| 8 | GrassSystem.cpp | 81 | Pending | All Managed* types |
| 9 | CatmullClarkSystem.cpp | 81 | Pending | Pipeline |
| 10 | Remaining files | - | Pending | Clean up stragglers |

---

## vulkan-hpp RAII Usage Examples

### Creating a Pipeline
```cpp
// BEFORE
ManagedPipeline pipeline;
ManagedPipeline::createGraphics(device, cache, pipelineInfo, pipeline);

// AFTER
auto pipeline = vk::raii::Pipeline(device, cache, pipelineInfo);
```

### Creating an ImageView
```cpp
// BEFORE
ManagedImageView imageView;
ManagedImageView::create(device, viewInfo, imageView);

// AFTER
auto imageView = vk::raii::ImageView(device, viewInfo);
```

### Creating a RenderPass
```cpp
// BEFORE
ManagedRenderPass renderPass;
ManagedRenderPass::create(device, renderPassInfo, renderPass);

// AFTER
auto renderPass = vk::raii::RenderPass(device, renderPassInfo);
```

### Command Buffer Recording
```cpp
// BEFORE (CommandScope)
CommandScope cmd(device, commandPool, queue);
cmd.begin();
vkCmdCopyBuffer(cmd.get(), src, dst, 1, &region);
cmd.end();

// AFTER (vk::raii with one-time submit)
auto cmdBuffer = vk::raii::CommandBuffer(device, allocInfo);
cmdBuffer.begin(vk::CommandBufferBeginInfo{}.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
cmdBuffer.copyBuffer(src, dst, region);
cmdBuffer.end();
queue.submit(vk::SubmitInfo{}.setCommandBuffers(*cmdBuffer));
queue.waitIdle();
```

---

## Prerequisites

Ensure dynamic dispatch is configured:
```cpp
// In ONE .cpp file (main.cpp or VulkanContext.cpp):
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// After instance creation:
VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

// After device creation:
VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
```

---

## Testing Strategy

After each file migration:
1. Run `cmake --preset debug && cmake --build build/debug`
2. Run `./run-debug.sh` to verify no crashes
3. Visual inspection of rendering output
4. Commit with message: "Convert [filename] to vulkan-hpp"
