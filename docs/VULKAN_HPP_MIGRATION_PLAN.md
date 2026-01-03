# Vulkan-hpp Migration

## Status: Complete

The migration from raw Vulkan C API to vulkan-hpp is complete. All major systems use vulkan-hpp builder patterns and `vk::raii::*` types.

### Current State

| Metric | Count | Notes |
|--------|-------|-------|
| Raw Vulkan function calls | ~400 | Mostly VMA integration points |
| Raw Vulkan structs | ~230 | VkFormat, VkExtent2D config types + VMA |
| Custom RAII wrappers | ~100 | VmaBuffer/VmaImage (intentionally kept) |

### Core Helper Files

| File | Purpose | Status |
|------|---------|--------|
| `VulkanHelpers.h` | Render passes, depth resources, framebuffers | Uses vulkan-hpp builder pattern |
| `VmaResources.h` | RAII wrappers for VMA resources | VmaBuffer, VmaImage (kept for VMA integration) |
| `CommandBufferUtils.h` | CommandScope, RenderPassScope | Fully vulkan-hpp |
| `VulkanContext.h/cpp` | Core context, vk::raii::Device | Complete |

### Deprecated Files (Removed)

The following files were deprecated and removed during migration:
- `VulkanBarriers.h` - Barriers now inline with `cmd.pipelineBarrier()`
- `VulkanRAII.h` - Replaced with `vk::raii::*` types and `VmaResources.h`
- `VulkanResourceFactory.cpp/h` - Functionality absorbed into `VulkanHelpers.h`

---

## Architecture

### VMA Integration

VMA (Vulkan Memory Allocator) requires raw Vulkan types. We maintain custom wrappers in `VmaResources.h`:

```cpp
// VmaBuffer - RAII wrapper for VMA-allocated buffers
class VmaBuffer : public UniqueVmaBuffer {
public:
    static bool create(VmaAllocator allocator,
                       const vk::BufferCreateInfo& bufferInfo,  // vulkan-hpp input
                       const VmaAllocationCreateInfo& allocInfo,
                       VmaBuffer& outBuffer);
    // ...
};
```

Pattern for VMA calls:
```cpp
auto bufferInfo = vk::BufferCreateInfo{}
    .setSize(size)
    .setUsage(vk::BufferUsageFlagBits::eStorageBuffer);

vmaCreateBuffer(allocator,
    reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),  // Cast for VMA
    &allocInfo, &buffer, &allocation, nullptr);
```

### Builder Pattern

All vulkan-hpp code uses the builder pattern with `.set*()` methods:

```cpp
auto imageInfo = vk::ImageCreateInfo{}
    .setImageType(vk::ImageType::e2D)
    .setExtent(vk::Extent3D{width, height, 1})
    .setMipLevels(1)
    .setArrayLayers(1)
    .setFormat(vk::Format::eR8G8B8A8Srgb)
    .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
```

### Command Buffer Recording

Command recording uses vulkan-hpp method calls:

```cpp
cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, descriptorSets, {});
cmd.draw(vertexCount, 1, 0, 0);
```

### RAII Types

Use `vk::raii::*` types for automatic resource cleanup:

```cpp
auto renderPass = vk::raii::RenderPass(device, renderPassInfo);
auto framebuffer = vk::raii::Framebuffer(device, framebufferInfo);
auto pipeline = vk::raii::Pipeline(device, cache, pipelineInfo);
```

---

## Remaining Raw Vulkan Usage

The remaining ~400 raw Vulkan calls are intentional and fall into these categories:

1. **VMA Integration** - VMA functions require raw Vulkan types
2. **Configuration Types** - VkFormat, VkExtent2D used in structs
3. **Handle Storage** - Some member variables store raw handles alongside VMA allocations

These are not migration targets - they're necessary for VMA compatibility.

---

## Reference

Run analysis script to see current metrics:
```bash
./scripts/analyze-vulkan-usage.sh
```
