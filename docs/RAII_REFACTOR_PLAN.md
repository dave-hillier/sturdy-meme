# RAII Refactor Plan: unique_ptr with Custom Deleters

## Goal

Reduce boilerplate in VulkanRAII.h by using `std::unique_ptr` with custom deleters for simple device-only Vulkan handles, while keeping custom classes for complex resources (ManagedBuffer, ManagedImage) that need extra state.

## Current State

VulkanRAII.h has ~1400 lines with repetitive patterns:
- Each wrapper class has identical: constructor, destructor, move semantics, `get()`, `release()`, `operator bool()`
- Simple handles (Pipeline, RenderPass, Sampler, etc.) only need `VkDevice` + handle
- Complex handles (Buffer, Image) need additional state (VmaAllocator, mapped state, etc.)

## Proposed Design

### 1. Generic Deleter Template

```cpp
template<typename Handle, void (*DestroyFn)(VkDevice, Handle, const VkAllocationCallbacks*)>
struct VkHandleDeleter {
    VkDevice device = VK_NULL_HANDLE;

    using pointer = Handle;  // Required for unique_ptr

    void operator()(Handle handle) const noexcept {
        if (handle != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            DestroyFn(device, handle, nullptr);
        }
    }
};
```

### 2. Type Aliases

```cpp
// Simple device-only handles become type aliases
using UniquePipeline = std::unique_ptr<
    std::remove_pointer_t<VkPipeline>,
    VkHandleDeleter<VkPipeline, vkDestroyPipeline>>;

using UniqueRenderPass = std::unique_ptr<
    std::remove_pointer_t<VkRenderPass>,
    VkHandleDeleter<VkRenderPass, vkDestroyRenderPass>>;

using UniquePipelineLayout = std::unique_ptr<
    std::remove_pointer_t<VkPipelineLayout>,
    VkHandleDeleter<VkPipelineLayout, vkDestroyPipelineLayout>>;

using UniqueDescriptorSetLayout = std::unique_ptr<
    std::remove_pointer_t<VkDescriptorSetLayout>,
    VkHandleDeleter<VkDescriptorSetLayout, vkDestroyDescriptorSetLayout>>;

using UniqueSampler = std::unique_ptr<
    std::remove_pointer_t<VkSampler>,
    VkHandleDeleter<VkSampler, vkDestroySampler>>;

using UniqueImageView = std::unique_ptr<
    std::remove_pointer_t<VkImageView>,
    VkHandleDeleter<VkImageView, vkDestroyImageView>>;

using UniqueFramebuffer = std::unique_ptr<
    std::remove_pointer_t<VkFramebuffer>,
    VkHandleDeleter<VkFramebuffer, vkDestroyFramebuffer>>;

using UniqueFence = std::unique_ptr<
    std::remove_pointer_t<VkFence>,
    VkHandleDeleter<VkFence, vkDestroyFence>>;

using UniqueSemaphore = std::unique_ptr<
    std::remove_pointer_t<VkSemaphore>,
    VkHandleDeleter<VkSemaphore, vkDestroySemaphore>>;

using UniqueCommandPool = std::unique_ptr<
    std::remove_pointer_t<VkCommandPool>,
    VkHandleDeleter<VkCommandPool, vkDestroyCommandPool>>;

using UniqueDescriptorPool = std::unique_ptr<
    std::remove_pointer_t<VkDescriptorPool>,
    VkHandleDeleter<VkDescriptorPool, vkDestroyDescriptorPool>>;
```

### 3. Factory Functions

```cpp
// Factory functions for clean creation
inline UniquePipeline makeUniquePipeline(VkDevice device, VkPipeline pipeline) {
    return UniquePipeline(pipeline, {device});
}

inline UniqueRenderPass makeUniqueRenderPass(VkDevice device, VkRenderPass renderPass) {
    return UniqueRenderPass(renderPass, {device});
}

// ... similar for other types
```

### 4. Builder Integration

PipelineBuilder would return `unique_ptr` directly:

```cpp
// Before (out parameter)
bool buildComputePipeline(VkPipelineLayout layout, VkPipeline& pipeline);

// After (returns ownership)
UniquePipeline buildComputePipeline(VkPipelineLayout layout);
// Returns nullptr on failure
```

## What Stays as Custom Classes

These need custom classes due to extra state:

| Class | Reason |
|-------|--------|
| `ManagedBuffer` | Needs VmaAllocator, VmaAllocation, mapped state tracking |
| `ManagedImage` | Needs VmaAllocator, VmaAllocation |
| `ManagedCommandBuffer` | Needs VkCommandPool for freeing |

## Migration Strategy

### Phase 1: Add New Infrastructure (Non-Breaking)
1. Add `VkHandleDeleter` template
2. Add `Unique*` type aliases
3. Add `makeUnique*` factory functions
4. Keep existing `Managed*` classes as deprecated aliases

### Phase 2: Update Builders
1. Add new builder methods returning `unique_ptr`
2. PipelineBuilder::buildComputePipeline() -> UniquePipeline
3. Keep old methods for backward compatibility initially

### Phase 3: Migrate Callers (Incremental)
1. Update callers one file at a time
2. Replace `ManagedPipeline` with `UniquePipeline`
3. Replace `pipeline.get()` with `pipeline.get()` (same API)
4. Replace `pipeline.destroy()` with `pipeline.reset()`

### Phase 4: Remove Deprecated Classes
1. Remove old `ManagedPipeline`, `ManagedRenderPass`, etc.
2. Remove deprecated builder methods

## API Comparison

| Operation | Current (`ManagedPipeline`) | New (`UniquePipeline`) |
|-----------|----------------------------|------------------------|
| Declare | `ManagedPipeline pipeline;` | `UniquePipeline pipeline;` |
| Create | `builder.buildManagedComputePipeline(layout, pipeline);` | `auto pipeline = builder.buildComputePipeline(layout);` |
| Get handle | `pipeline.get()` | `pipeline.get()` |
| Check valid | `if (pipeline)` | `if (pipeline)` |
| Release | `pipeline.release()` | `pipeline.release()` |
| Destroy | `pipeline.destroy()` | `pipeline.reset()` |
| Move | `std::move(pipeline)` | `std::move(pipeline)` |

## Benefits

1. **Less code**: ~500 lines of boilerplate removed
2. **Standard library**: Uses familiar `unique_ptr` semantics
3. **Composable**: Works with `std::vector`, `std::optional`, algorithms
4. **Type-safe**: Each handle type is distinct
5. **Return by value**: Builders can return ownership directly

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Breaking API changes | Phase migration, keep aliases during transition |
| Vulkan handle as pointer type | Use `std::remove_pointer_t` in template |
| Performance | Deleter is stateful but small (one pointer) |
| Debuggability | Factory functions have clear names |

## Files to Modify

1. `src/core/VulkanRAII.h` - Add templates, type aliases, factories
2. `src/core/PipelineBuilder.h/cpp` - Add returning-unique methods
3. Callers (incremental):
   - `src/postprocess/PostProcessSystem.cpp`
   - `src/debug/DebugLineSystem.cpp`
   - `src/vegetation/TreeEditSystem.cpp`
   - `src/subdivision/CatmullClarkSystem.cpp`
   - ... and others using `ManagedPipeline`, `ManagedRenderPass`, etc.

## Testing

1. Build succeeds after each phase
2. Application runs without crashes
3. No Vulkan validation errors
4. Memory: no leaks (handles destroyed on scope exit)
