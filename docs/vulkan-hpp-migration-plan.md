# Vulkan-hpp Migration Plan: Remove VulkanBarriers, VulkanRAII, and VulkanResourceFactory

## Overview

This plan outlines the migration from custom Vulkan RAII wrappers and utilities to native vulkan-hpp alternatives.

### Files to Remove
1. `src/core/vulkan/VulkanBarriers.h` (920 lines) - included by 30 files
2. `src/core/vulkan/VulkanRAII.h` (1024 lines) - included by 51 files
3. `src/core/vulkan/VulkanResourceFactory.h` (306 lines) - included by 30 files
4. `src/core/vulkan/VulkanResourceFactory.cpp` (805 lines) - implementation

---

## Phase 1: Migrate VulkanBarriers.h

### Current Functionality
- `Barriers::computeToCompute()`, `computeToIndirectDraw()`, etc. - standalone barrier functions
- `Barriers::transitionImage()` - image layout transition helper
- `Barriers::TrackedImage` - RAII image layout tracking
- `Barriers::BarrierBatch` - batch multiple barriers into single call
- `Barriers::ScopedComputeBarrier` - RAII compute barrier guard
- `Barriers::ImageBarrier` - fluent builder for image barriers
- `Barriers::copyBufferToImage()`, etc. - high-level operations

### Migration Strategy

**Step 1.1: Replace standalone barrier functions with inline vulkan-hpp**

Current:
```cpp
Barriers::computeToCompute(cmd);
```

After:
```cpp
vk::CommandBuffer vkCmd(cmd);
vkCmd.pipelineBarrier(
    vk::PipelineStageFlagBits::eComputeShader,
    vk::PipelineStageFlagBits::eComputeShader,
    {},
    vk::MemoryBarrier{}.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eShaderRead),
    {}, {});
```

**Step 1.2: Replace `Barriers::transitionImage()` with direct vulkan-hpp**

Current:
```cpp
Barriers::transitionImage(cmd, image, oldLayout, newLayout, srcStage, dstStage, srcAccess, dstAccess);
```

After:
```cpp
vk::CommandBuffer vkCmd(cmd);
vkCmd.pipelineBarrier(
    vk::PipelineStageFlags(srcStage),
    vk::PipelineStageFlags(dstStage),
    {},
    {},
    {},
    vk::ImageMemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlags(srcAccess))
        .setDstAccessMask(vk::AccessFlags(dstAccess))
        .setOldLayout(vk::ImageLayout(oldLayout))
        .setNewLayout(vk::ImageLayout(newLayout))
        .setImage(image)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setLevelCount(VK_REMAINING_MIP_LEVELS)
            .setLayerCount(VK_REMAINING_ARRAY_LAYERS)));
```

**Step 1.3: TrackedImage, BarrierBatch, ImageBarrier - Keep as thin wrappers**

These classes provide valuable abstractions (layout tracking, batching). Rewrite them to use vulkan-hpp internally but keep the public API. Move to a new `VulkanSync.h` file.

**Step 1.4: High-level operations (copyBufferToImage, etc.)**

These are useful convenience functions. Keep them but migrate internals to vulkan-hpp. Move to `VulkanSync.h`.

### Files Affected (30 files)
- src/water/*.cpp (5 files)
- src/vegetation/*.cpp (3 files)
- src/terrain/*.cpp (6 files)
- src/postprocess/*.cpp (4 files)
- src/atmosphere/*.cpp (6 files)
- src/lighting/*.cpp (1 file)
- src/subdivision/*.cpp (2 files)
- src/core/Texture.cpp

---

## Phase 2: Migrate VulkanRAII.h

### Current Functionality

**Unique* types (thin std::unique_ptr wrappers):**
- `UniqueVmaImage`, `UniqueVmaBuffer` - VMA-allocated resources
- `UniquePipeline`, `UniqueRenderPass`, `UniquePipelineLayout`, etc.

**Managed* types (extended Unique* with factory methods):**
- `ManagedBuffer` - VMA buffer with map/unmap
- `ManagedImage` - VMA image
- `ManagedImageView`, `ManagedSampler`, etc.
- `ManagedPipeline`, `ManagedPipelineLayout`, `ManagedRenderPass`, etc.

**Utility classes:**
- `ScopeGuard` - RAII cleanup helper
- `CommandScope` - one-time command buffer submission
- `RenderPassScope` - RAII render pass begin/end
- `VK_CHECK`, `VK_CHECK_VOID` macros

### Migration Strategy

**Step 2.1: VMA-allocated resources - Keep custom wrappers**

vulkan-hpp's `vk::raii::*` types don't integrate with VMA. Keep `ManagedBuffer` and `ManagedImage` but simplify:
- Rename to `VmaBuffer`, `VmaImage`
- Use vulkan-hpp types internally
- Move to new `VmaResources.h`

**Step 2.2: Non-VMA resources - Replace with vk::raii::***

| Current Type | vulkan-hpp Replacement |
|--------------|------------------------|
| `ManagedImageView` | `vk::raii::ImageView` |
| `ManagedSampler` | `vk::raii::Sampler` |
| `ManagedPipeline` | `vk::raii::Pipeline` |
| `ManagedPipelineLayout` | `vk::raii::PipelineLayout` |
| `ManagedRenderPass` | `vk::raii::RenderPass` |
| `ManagedFramebuffer` | `vk::raii::Framebuffer` |
| `ManagedFence` | `vk::raii::Fence` |
| `ManagedSemaphore` | `vk::raii::Semaphore` |
| `ManagedCommandPool` | `vk::raii::CommandPool` |
| `ManagedDescriptorSetLayout` | `vk::raii::DescriptorSetLayout` |

**Step 2.3: Utility classes**

- `ScopeGuard` - Keep (standard C++ pattern, not Vulkan-specific)
- `CommandScope` - Replace with `vk::raii::CommandBuffer` or helper using vulkan-hpp
- `RenderPassScope` - Keep as thin wrapper using vulkan-hpp

**Step 2.4: VK_CHECK macros**

Replace with vulkan-hpp exceptions or simplified pattern:
```cpp
// Result checking is automatic with vulkan-hpp when VULKAN_HPP_NO_EXCEPTIONS=0
auto pipeline = device.createGraphicsPipeline(cache, pipelineInfo);

// Or for VMA operations:
VK_CHECK_VMA(vmaCreateBuffer(...));
```

### Files Affected (51 files)
- All header files declaring Managed* types as members
- All source files using Managed*/Unique* types

---

## Phase 3: Migrate VulkanResourceFactory

### Current Functionality
Static factory methods for:
- Command pools/buffers
- Sync resources (semaphores, fences)
- Depth resources (image + view + sampler)
- Framebuffers
- Render passes
- Buffer factories (staging, vertex, index, uniform, storage, etc.)
- Sampler factories

### Migration Strategy

**Step 3.1: Replace with direct vulkan-hpp calls**

Most factory methods are thin wrappers. Replace with direct vulkan-hpp:

Current:
```cpp
VulkanResourceFactory::createVertexBuffer(allocator, size, buffer);
```

After:
```cpp
auto bufferInfo = vk::BufferCreateInfo{}
    .setSize(size)
    .setUsage(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer);
VmaAllocationCreateInfo allocInfo{};
allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
// Direct VMA call with vulkan-hpp struct
buffer = VmaBuffer::create(allocator, bufferInfo, allocInfo);
```

**Step 3.2: Keep complex resource creation as free functions**

For multi-step resource creation (DepthResources, DepthArrayResources), keep as free functions in a new `VulkanResources.h`:
```cpp
namespace VulkanResources {
    std::optional<DepthResources> createDepthResources(const vk::raii::Device& device, VmaAllocator allocator, vk::Extent2D extent, vk::Format format);
}
```

### Files Affected (30 files)
- src/core/Renderer.cpp
- Various system initialization code

---

## Phase 4: New File Structure

### Files to Create

1. **`src/core/vulkan/VmaResources.h`**
   - `VmaBuffer` (replaces ManagedBuffer)
   - `VmaImage` (replaces ManagedImage)
   - VMA helper functions

2. **`src/core/vulkan/VulkanSync.h`**
   - `TrackedImage` - layout tracking (uses vulkan-hpp internally)
   - `BarrierBatch` - barrier batching (uses vulkan-hpp internally)
   - Barrier convenience functions using vulkan-hpp
   - `copyBufferToImage()` and similar high-level operations

3. **`src/core/vulkan/VulkanHelpers.h`**
   - `ScopeGuard` (unchanged)
   - `RenderPassScope` (using vulkan-hpp)
   - Error handling macros for VMA

4. **`src/core/vulkan/VulkanResources.h`**
   - Complex resource creation (depth buffers, framebuffers, render passes)
   - Uses vulkan-hpp builder pattern

### Files to Delete
- `src/core/vulkan/VulkanBarriers.h`
- `src/core/vulkan/VulkanRAII.h`
- `src/core/vulkan/VulkanResourceFactory.h`
- `src/core/vulkan/VulkanResourceFactory.cpp`

---

## Implementation Order

### Batch 1: Foundation (Low Risk)
1. Create `VmaResources.h` with `VmaBuffer`, `VmaImage`
2. Create `VulkanHelpers.h` with `ScopeGuard`, `RenderPassScope`
3. Update includes in affected files to use new headers
4. Verify build compiles

### Batch 2: Barrier Migration
1. Create `VulkanSync.h` with vulkan-hpp barrier utilities
2. Migrate `VulkanBarriers.h` usages file-by-file:
   - Start with low-dependency files (atmosphere, water)
   - Move to core files (Renderer, TerrainSystem)
3. Delete `VulkanBarriers.h`

### Batch 3: RAII Type Migration
1. Replace `Managed*` non-VMA types with `vk::raii::*` (one type at a time):
   - `ManagedSampler` -> `vk::raii::Sampler`
   - `ManagedImageView` -> `vk::raii::ImageView`
   - `ManagedPipeline` -> `vk::raii::Pipeline`
   - etc.
2. Update all usages
3. Delete old types from VulkanRAII.h

### Batch 4: ResourceFactory Migration
1. Create `VulkanResources.h` with essential factory functions
2. Replace `VulkanResourceFactory::create*` calls with direct vulkan-hpp
3. Migrate remaining factory functions to `VulkanResources.h`
4. Delete `VulkanResourceFactory.h/cpp`

### Batch 5: Cleanup
1. Delete `VulkanRAII.h`
2. Update `#include` statements throughout codebase
3. Final build verification
4. Run application to verify functionality

---

## Testing Strategy

After each batch:
1. `cmake --preset debug && cmake --build build/debug`
2. `./run-debug.sh` - verify application runs without crashes
3. Visual inspection of rendering output

---

## Notes

- **VMA Integration**: vulkan-hpp doesn't directly support VMA, so we must keep thin wrappers for VMA-allocated resources
- **Builder Pattern**: All vulkan-hpp struct usage must use the builder pattern (`.set*()` methods) as positional constructors are disabled
- **Dynamic Dispatch**: Already configured in the project with `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC`
- **Gradual Migration**: Each batch should result in a working build - don't break the build mid-migration
