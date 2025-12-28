# Vulkan-hpp Migration Plan

## Current State Summary

| Category | Count |
|----------|-------|
| Raw Vulkan function calls (vk*) | 954 |
| Raw Vulkan struct usages (Vk*Info) | 519 |
| Custom RAII wrapper usages | 683 |

## Migration Strategy

### Approach
1. **Bottom-up migration**: Start with core infrastructure files, then move to higher-level systems
2. **Keep VMA integration**: Custom wrappers for VMA-allocated resources remain (VMA doesn't integrate with vk::raii)
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

## Phase 1: Core Infrastructure

### 1.1 VulkanRAII.h (207 usages) - FOUNDATION
- Keep VMA wrappers (ManagedBuffer, ManagedImage) but convert internal Vulkan types
- Convert VkDevice → vk::Device, VkBuffer → vk::Buffer, etc.
- Update all VkCreate* calls to use vulkan-hpp

### 1.2 VulkanBarriers.h (160 usages)
- Convert VkImageMemoryBarrier → vk::ImageMemoryBarrier
- Convert VkBufferMemoryBarrier → vk::BufferMemoryBarrier
- Convert VkMemoryBarrier → vk::MemoryBarrier
- Convert vkCmdPipelineBarrier → cmd.pipelineBarrier()

### 1.3 VulkanResourceFactory.cpp (94 usages)
- Convert all resource creation to vulkan-hpp builder pattern
- Update VmaCreateBuffer/VmaCreateImage calls with reinterpret_cast

---

## Phase 2: Terrain System

### 2.1 TerrainSystem.cpp (91 usages)
- Convert pipeline creation
- Convert descriptor set handling
- Convert command buffer recording

### 2.2 TerrainQuadTree.cpp
- Convert buffer operations
- Convert compute dispatch calls

### 2.3 TerrainLOD.cpp / TerrainHeight.cpp
- Convert remaining terrain-related Vulkan calls

---

## Phase 3: Vegetation Systems

### 3.1 TreeImpostorAtlas.cpp (134 usages) - LARGEST FILE
- Convert image/imageview creation
- Convert framebuffer setup
- Convert render pass handling

### 3.2 TreeLODSystem.cpp (121 usages)
- Convert compute pipeline usage
- Convert buffer operations

### 3.3 TreeRenderer.cpp (103 usages)
- Convert graphics pipeline usage
- Convert draw calls

### 3.4 GrassSystem.cpp (81 usages)
- Convert compute and graphics pipelines
- Convert indirect draw calls

---

## Phase 4: Rendering Systems

### 4.1 PostProcessSystem.cpp (86 usages)
- Convert post-processing pipeline
- Convert framebuffer operations

### 4.2 CatmullClarkSystem.cpp (81 usages)
- Convert subdivision compute pipelines

### 4.3 Remaining render systems
- Water rendering (WaterGBuffer, WaterDisplacement)
- Sky rendering
- Shadow mapping

---

## Phase 5: Animation & Mesh Systems

### 5.1 SkinnedMesh.cpp
- Convert vertex/index buffer handling
- Convert animation compute shaders

### 5.2 Other mesh systems
- Convert remaining mesh loading/rendering code

---

## Phase 6: Cleanup

### 6.1 Remove deprecated custom wrappers
- Replace Unique* types with vk::raii equivalents where VMA isn't needed
- Consolidate remaining VMA wrappers

### 6.2 Final validation
- Ensure all files use `#include <vulkan/vulkan.hpp>`
- Remove `#include <vulkan/vulkan.h>` (raw C header)
- Run full test suite

---

## Files to Migrate (Priority Order)

| Priority | File | Usages | Notes |
|----------|------|--------|-------|
| 1 | src/core/vulkan/VulkanRAII.h | 207 | Foundation - blocks everything |
| 2 | src/core/vulkan/VulkanBarriers.h | 160 | Used by most render code |
| 3 | src/core/vulkan/VulkanResourceFactory.cpp | 94 | Resource creation patterns |
| 4 | src/vegetation/TreeImpostorAtlas.cpp | 134 | Largest non-core file |
| 5 | src/vegetation/TreeLODSystem.cpp | 121 | High usage count |
| 6 | src/vegetation/TreeRenderer.cpp | 103 | High usage count |
| 7 | src/terrain/TerrainSystem.cpp | 91 | Core terrain rendering |
| 8 | src/postprocess/PostProcessSystem.cpp | 86 | Post-processing |
| 9 | src/vegetation/GrassSystem.cpp | 81 | Vegetation |
| 10 | src/subdivision/CatmullClarkSystem.cpp | 81 | Subdivision |

---

## Custom Wrapper Migration Map

| Current Wrapper | Target | Notes |
|-----------------|--------|-------|
| ManagedBuffer | Keep (VMA) | Just convert internal types |
| ManagedImage | Keep (VMA) | Just convert internal types |
| ManagedImageView | vk::raii::ImageView | No VMA needed |
| ManagedSampler | vk::raii::Sampler | No VMA needed |
| ManagedPipeline | vk::raii::Pipeline | No VMA needed |
| ManagedPipelineLayout | vk::raii::PipelineLayout | No VMA needed |
| ManagedRenderPass | vk::raii::RenderPass | No VMA needed |
| ManagedFramebuffer | vk::raii::Framebuffer | No VMA needed |
| ManagedFence | vk::raii::Fence | No VMA needed |
| ManagedSemaphore | vk::raii::Semaphore | No VMA needed |
| ManagedCommandPool | vk::raii::CommandPool | No VMA needed |
| ManagedDescriptorSetLayout | vk::raii::DescriptorSetLayout | No VMA needed |

---

## Testing Strategy

After each file migration:
1. Run `cmake --preset debug && cmake --build build/debug`
2. Run `./run-debug.sh` to verify no crashes
3. Visual inspection of rendering output
4. Commit with message: "Convert [filename] to vulkan-hpp"

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
