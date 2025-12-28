# Vulkan-HPP Migration Plan

This plan focuses on migrating raw Vulkan C API usage to vulkan-hpp throughout the codebase. The eventual goal is to remove `VulkanBarriers.h`, `VulkanRAII.h`, and `VulkanResourceFactory.cpp` and use vulkan-hpp natives, but this plan focuses on the other migration work first.

## Current State

Based on `./scripts/analyze-vulkan-usage.sh`:
- **904** raw Vulkan function calls (vk*)
- **506** raw Vulkan struct usages (Vk*Info)
- **658** custom RAII wrapper usages

## Migration Strategy

### Phase 1: Foundation - Handle Type Conversion

Convert raw Vulkan handle types to vk:: equivalents. These are binary-compatible so can be done incrementally.

**Priority files:**
1. `src/core/Renderer.cpp` - Central orchestrator
2. `src/core/RendererInit.cpp` - Initialization code
3. `src/core/vulkan/VulkanContext.cpp` - Already has RAII device

**Handle conversions:**
| C Type | vulkan-hpp Type |
|--------|-----------------|
| `VkDevice` | `vk::Device` |
| `VkCommandBuffer` | `vk::CommandBuffer` |
| `VkQueue` | `vk::Queue` |
| `VkPhysicalDevice` | `vk::PhysicalDevice` |
| `VkPipeline` | `vk::Pipeline` |
| `VkPipelineLayout` | `vk::PipelineLayout` |
| `VkDescriptorSet` | `vk::DescriptorSet` |
| `VkBuffer` | `vk::Buffer` |
| `VkImage` | `vk::Image` |
| `VkImageView` | `vk::ImageView` |
| `VkSampler` | `vk::Sampler` |
| `VkRenderPass` | `vk::RenderPass` |
| `VkFramebuffer` | `vk::Framebuffer` |

**Note:** VmaAllocator integration means we keep VMA-managed resources as raw handles for now.

---

### Phase 2: Command Buffer Recording (Biggest Impact)

Migrate command buffer recording from C API to vulkan-hpp member functions. This eliminates ~500+ raw vkCmd* calls.

**Target files by usage count:**
1. `src/vegetation/TreeLODSystem.cpp` - 121 usages
2. `src/vegetation/TreeRenderer.cpp` - 103 usages
3. `src/terrain/TerrainSystem.cpp` - 91 usages
4. `src/postprocess/PostProcessSystem.cpp` - 86 usages
5. `src/vegetation/GrassSystem.cpp` - 81 usages
6. `src/subdivision/CatmullClarkSystem.cpp` - 81 usages
7. `src/core/Renderer.cpp` - 81 usages

**Command conversions:**
```cpp
// BEFORE
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(data), &data);
vkCmdDraw(cmd, 3, 1, 0, 0);

// AFTER
vk::CommandBuffer vkCmd(cmd);  // or pass vk::CommandBuffer directly
vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, set, {});
vkCmd.pushConstants(layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(data), &data);
vkCmd.draw(3, 1, 0, 0);
```

**Sub-phases:**
1. Add `vk::CommandBuffer` parameter to render functions alongside `VkCommandBuffer`
2. Convert internal command recording to vulkan-hpp
3. Update callers to pass `vk::CommandBuffer`
4. Remove old `VkCommandBuffer` parameter

---

### Phase 3: Create Info Structures

Convert C-style create info structs to vulkan-hpp builder pattern. This improves type safety and reduces boilerplate.

**High-frequency structs:**
| Count | C Struct | vulkan-hpp |
|-------|----------|------------|
| 52 | `VkBufferCreateInfo` | `vk::BufferCreateInfo{}` |
| 46 | `VkImageCreateInfo` | `vk::ImageCreateInfo{}` |
| 45 | `VkImageViewCreateInfo` | `vk::ImageViewCreateInfo{}` |
| 43 | `VkComputePipelineCreateInfo` | `vk::ComputePipelineCreateInfo{}` |
| 40 | `VkDescriptorBufferInfo` | `vk::DescriptorBufferInfo{}` |
| 33 | `VkPipelineShaderStageCreateInfo` | `vk::PipelineShaderStageCreateInfo{}` |
| 28 | `VkPipelineLayoutCreateInfo` | `vk::PipelineLayoutCreateInfo{}` |
| 22 | `VkGraphicsPipelineCreateInfo` | `vk::GraphicsPipelineCreateInfo{}` |
| 21 | `VkSamplerCreateInfo` | `vk::SamplerCreateInfo{}` |

**Example conversion:**
```cpp
// BEFORE
VkImageViewCreateInfo viewInfo{};
viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
viewInfo.image = image;
viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
viewInfo.subresourceRange.levelCount = 1;
viewInfo.subresourceRange.layerCount = 1;

// AFTER
auto viewInfo = vk::ImageViewCreateInfo{}
    .setImage(image)
    .setViewType(vk::ImageViewType::e2D)
    .setFormat(vk::Format::eR8G8B8A8Srgb)
    .setSubresourceRange(vk::ImageSubresourceRange{}
        .setAspectMask(vk::ImageAspectFlagBits::eColor)
        .setLevelCount(1)
        .setLayerCount(1));
```

---

### Phase 4: Pipeline Creation

Update `GraphicsPipelineFactory` and compute pipeline creation to use vulkan-hpp throughout.

**Target files:**
- `src/core/pipeline/GraphicsPipelineFactory.cpp`
- `src/core/pipeline/PipelineBuilder.cpp`
- `src/core/pipeline/RenderPipelineFactory.cpp`
- Systems creating pipelines directly

**Conversion approach:**
1. Update factory to store `vk::` structs internally
2. Use builder pattern for all pipeline state structs
3. Call `device.createGraphicsPipeline()` / `device.createComputePipeline()`
4. Update callers to use factory

---

### Phase 5: Descriptor Set Management

Migrate descriptor writing and allocation to vulkan-hpp.

**Target files:**
- `src/core/material/DescriptorManager.cpp`
- `src/core/material/MaterialDescriptorFactory.cpp`
- Systems with inline descriptor updates

**Conversion:**
```cpp
// BEFORE
VkDescriptorBufferInfo bufferInfo{};
bufferInfo.buffer = buffer;
bufferInfo.offset = 0;
bufferInfo.range = sizeof(UBO);

VkWriteDescriptorSet write{};
write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
write.dstSet = set;
write.dstBinding = 0;
write.descriptorCount = 1;
write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
write.pBufferInfo = &bufferInfo;
vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

// AFTER
auto bufferInfo = vk::DescriptorBufferInfo{}
    .setBuffer(buffer)
    .setRange(sizeof(UBO));

vk::Device(device).updateDescriptorSets(
    vk::WriteDescriptorSet{}
        .setDstSet(set)
        .setDstBinding(0)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setBufferInfo(bufferInfo),
    nullptr);
```

---

### Phase 6: Shader Module Loading

Update `ShaderLoader` to use vulkan-hpp.

**Target file:** `src/core/ShaderLoader.cpp`

```cpp
// BEFORE
VkShaderModuleCreateInfo createInfo{};
createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
createInfo.codeSize = code.size();
createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);

// AFTER
auto shaderModule = vk::Device(device).createShaderModule(
    vk::ShaderModuleCreateInfo{}
        .setCodeSize(code.size())
        .setPCode(reinterpret_cast<const uint32_t*>(code.data())));
```

---

### Phase 7: Image and Buffer Utilities

Update helper classes to use vulkan-hpp internally.

**Target files:**
- `src/core/ImageBuilder.cpp`
- `src/core/BufferUtils.cpp`
- `src/core/GlobalBufferManager.cpp`

**Note:** Keep VMA integration - cast to `VkImageCreateInfo*` / `VkBufferCreateInfo*` when calling VMA functions.

---

### Phase 8: Render Pass and Framebuffer Creation

Convert render pass and framebuffer creation code.

**Target files:**
- Systems with custom render passes (BloomSystem, WaterGBuffer, etc.)
- `src/core/Renderer.cpp` (main render pass)

---

### Phase 9: Synchronization

Convert barrier and sync code (separate from removing VulkanBarriers.h).

**Conversions:**
```cpp
// BEFORE
VkImageMemoryBarrier barrier{};
barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
// ... setup ...
vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

// AFTER
auto barrier = vk::ImageMemoryBarrier{}
    .setOldLayout(vk::ImageLayout::eUndefined)
    .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
    // ... etc ...

vk::CommandBuffer(cmd).pipelineBarrier(
    srcStage, dstStage, {},
    nullptr, nullptr, barrier);
```

---

## File-by-File Priority Order

Based on usage counts and impact, migrate in this order:

### Tier 1: Core Infrastructure
1. `src/core/Renderer.cpp` - 81 usages
2. `src/core/RendererInit.cpp`
3. `src/core/vulkan/VulkanContext.cpp`
4. `src/core/ShaderLoader.cpp`

### Tier 2: Rendering Systems (High Usage)
1. `src/vegetation/TreeLODSystem.cpp` - 121 usages
2. `src/vegetation/TreeRenderer.cpp` - 103 usages
3. `src/terrain/TerrainSystem.cpp` - 91 usages
4. `src/postprocess/PostProcessSystem.cpp` - 86 usages
5. `src/vegetation/GrassSystem.cpp` - 81 usages
6. `src/subdivision/CatmullClarkSystem.cpp` - 81 usages

### Tier 3: Pipeline Infrastructure
1. `src/core/pipeline/GraphicsPipelineFactory.cpp`
2. `src/core/pipeline/PipelineBuilder.cpp`
3. `src/core/material/DescriptorManager.cpp`

### Tier 4: Water & Atmosphere
1. `src/water/WaterSystem.cpp`
2. `src/water/OceanFFT.cpp`
3. `src/atmosphere/AtmosphereLUTSystem.cpp`
4. `src/atmosphere/SkySystem.cpp`
5. `src/lighting/FroxelSystem.cpp`
6. `src/lighting/ShadowSystem.cpp`

### Tier 5: Utilities
1. `src/core/ImageBuilder.cpp`
2. `src/core/BufferUtils.cpp`
3. `src/core/GlobalBufferManager.cpp`

### Tier 6: Remaining Systems
- All remaining systems with <50 usages

---

## Testing Strategy

After each file migration:
1. Build with `cmake --preset debug && cmake --build build/debug`
2. Run with `./run-debug.sh`
3. Verify the specific system renders correctly
4. Check for Vulkan validation layer errors

---

## Notes

### VMA Compatibility
VMA uses raw Vulkan types. When calling VMA functions, cast vulkan-hpp structs:
```cpp
auto bufferInfo = vk::BufferCreateInfo{}.setSize(size).setUsage(usage);
vmaCreateBuffer(allocator,
    reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
    &allocInfo, &buffer, &allocation, nullptr);
```

### Binary Compatibility
`vk::` types are binary compatible with `Vk` types, enabling incremental migration:
```cpp
VkPipeline rawPipeline = ...;
vk::Pipeline pipeline(rawPipeline);  // No copy, just type wrapper
```

### Dynamic Dispatch
The codebase already uses `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1`. The dispatcher is initialized in `VulkanContext.cpp`.
