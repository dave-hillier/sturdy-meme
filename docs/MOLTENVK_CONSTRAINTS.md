# MoltenVK Constraints & Platform Considerations

This document outlines platform-specific constraints when implementing the lighting system on macOS/iOS via MoltenVK, which layers Vulkan over Apple's Metal API.

## MoltenVK Version Requirements

- **Minimum:** MoltenVK 1.4+ (Vulkan 1.4 support)
- **Recommended SDK:** LunarG Vulkan SDK 1.4.x or later

## Supported Features

The following features from LIGHTING_IMPLEMENTATION_PLAN.md are fully supported:

| Feature | Notes |
|---------|-------|
| Compute shaders | Full support via Metal compute |
| 3D textures | Supported for froxel volumes |
| Texture arrays | Supported for cascaded shadow maps |
| Depth comparison samplers | `sampler2DShadow`, `sampler2DArrayShadow` work correctly |
| Storage images | `imageLoad`/`imageStore` supported |
| Shared memory | `shared` qualifier in compute shaders |
| Scalar atomics | `atomicAdd` on `uint`/`int` in shared memory |
| Push constants | Supported |
| Cubemaps | Supported for reflection probes |
| HDR formats | R16G16B16A16_SFLOAT, B10G11R11_UFLOAT_PACK32 |

---

## Constraints & Workarounds

### 1. Geometry Shaders

**Issue:** Metal has no native geometry shader support. MoltenVK emulates geometry shaders by transforming them to vertex shaders with buffer feedback, which has performance overhead.

**Affected Section:** 2.2.3 Cascaded Shadow Maps

The plan mentions:
> "Create separate framebuffers per cascade, or use layered rendering with gl_Layer in geometry shader"

**Recommendation:** Use separate framebuffers per cascade instead of geometry shader layered rendering.

```cpp
// Preferred approach for MoltenVK
for (uint32_t cascade = 0; cascade < NUM_CASCADES; cascade++) {
    vkCmdBeginRenderPass(cmd, &shadowRenderPassInfo[cascade], ...);
    // Render scene for this cascade
    vkCmdEndRenderPass(cmd);
}
```

---

### 2. Image Atomics on Storage Images

**Issue:** The bilateral grid build (Section 5.3.3) uses a read-modify-write pattern that requires true atomic operations on image texels:

```glsl
// Problematic pattern - race condition without true atomics
vec4 existing = imageLoad(bilateralGrid, coord);
imageStore(bilateralGrid, coord, existing + vec4(logLum * weight, weight, 0.0, 0.0));
```

MoltenVK does not support `VK_EXT_shader_atomic_float` for floating-point image atomics.

**Workarounds:**

**Option A: Multi-pass accumulation**
```glsl
// Pass 1: Clear grid
// Pass 2-N: Each thread writes to a unique location, then reduce
```

**Option B: Fixed-point atomics**
```glsl
// Use R32UI format and pack/unpack floats
layout(binding = 1, r32ui) uniform uimage3D bilateralGridPacked;

void AccumulateToGrid(ivec3 coord, float logLum, float weight) {
    // Pack two float16 values into uint32
    uint packed = packHalf2x16(vec2(logLum * weight, weight));
    imageAtomicAdd(bilateralGridPacked, coord, packed);
}
```

**Option C: Compute shader with groupshared accumulation**
```glsl
// Accumulate in shared memory first, then write once per workgroup
shared vec4 localGrid[GRID_SIZE];
// ... accumulate locally ...
barrier();
if (gl_LocalInvocationIndex == 0) {
    imageStore(bilateralGrid, coord, localGrid[...]);
}
```

---

### 3. Async Compute

**Issue:** The plan uses async compute for parallel execution of histogram building and bilateral grid updates. While MoltenVK exposes multiple queue families, true async execution depends on the Metal device.

**Behaviour by Hardware:**
- **Apple Silicon (M1+):** Good async compute support
- **Intel Macs:** May serialize compute and graphics work
- **iOS devices (A11+):** Good async compute support

**Recommendation:** Structure code to work correctly whether async or serialized:

```cpp
// Submit compute work
vkQueueSubmit(computeQueue, 1, &computeSubmitInfo, computeFence);

// Don't assume parallel execution - use proper synchronization
VkSemaphore computeComplete = ...;

// Graphics work waits on compute
VkSubmitInfo graphicsSubmit = {};
graphicsSubmit.waitSemaphoreCount = 1;
graphicsSubmit.pWaitSemaphores = &computeComplete;
```

---

### 4. Subgroup Operations

**Issue:** Some advanced compute optimizations use subgroup operations (`subgroupAdd`, `subgroupBallot`). Support varies.

**Status:** MoltenVK 1.4 supports `VK_EXT_subgroup_size_control` and basic subgroup operations on supported hardware.

**Recommendation:** Query support at runtime:
```cpp
VkPhysicalDeviceSubgroupProperties subgroupProps = {};
subgroupProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
// ... query and check subgroupSupportedOperations
```

---

### 5. Sparse Resources

**Issue:** Sparse textures/buffers are not supported by MoltenVK.

**Affected Section:** None directly, but if extending the probe system to use sparse textures for streaming, this won't work.

**Workaround:** Use texture atlases or manual streaming with regular textures.

---

### 6. Pipeline Cache

**Issue:** `VkPipelineCache` works but the cache format is not portable. Metal shader compilation happens at pipeline creation.

**Recommendation:**
- Use pipeline cache for session-local benefit
- Consider pre-warming pipelines at load time
- Metal's shader compilation is generally fast

---

## Runtime Feature Detection

Query MoltenVK-specific capabilities:

```cpp
void CheckMoltenVKCapabilities(VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceVulkan12Features vulkan12 = {};
    vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features2.pNext = &vulkan12;

    vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);

    // Check specific features
    bool hasShaderInt64 = features2.features.shaderInt64;
    bool hasDescriptorIndexing = vulkan12.descriptorIndexing;
    // etc.
}
```

---

## Shader Considerations

### GLSL Intrinsics
All intrinsics used in the lighting plan are supported:
- `smoothstep`, `mix`, `clamp`
- `findLSB`, `findMSB`
- `barrier()`, `memoryBarrier()`
- `atomicAdd` (on shared memory)
- `texture`, `textureLod`, `texelFetch`
- `imageLoad`, `imageStore`
- `dFdx`, `dFdy`, `fwidth`

### Precision
Metal defaults to high precision. The `mediump` and `lowp` qualifiers in GLSL are ignored—all floats are 32-bit.

---

## Performance Notes

### Texture Bandwidth
Metal/MoltenVK handles texture bandwidth efficiently. The froxel volume (128x64x64 @ RGB11F) and bilateral grid (64x32x64 @ RGBA16F) sizes in the plan are reasonable.

### Compute Dispatch
Prefer workgroup sizes that are multiples of 32 (SIMD width on Apple GPUs):
```glsl
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;  // 64 threads - good
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;  // 64 threads - good
```

### Memory
Apple Silicon has unified memory. Large allocations (shadow atlases, probe data) share the same memory pool as system RAM.

---

## Summary Checklist

Before implementing each phase, verify:

### Lighting Implementation Plan

- [ ] **Phase 1 (Lighting):** No constraints - fully supported
- [ ] **Phase 2 (Shadows):** Use per-cascade framebuffers, not geometry shader layering
- [ ] **Phase 3 (Indirect):** SH probes work; reflection probe cubemaps supported
- [ ] **Phase 4 (Atmosphere):** Compute-heavy; test on target hardware
- [ ] **Phase 5 (Post-process):** Use workaround for bilateral grid atomics
- [ ] **Phase 6 (Integration):** Test async compute behaviour on target devices

### Procedural Grass Implementation Plan

- [ ] **Core System:** Fully supported - compute shaders, storage buffers, indirect draw all work
- [ ] **Subgroup Optimizations (Phase 12.3):** Query runtime support; provide fallback to per-thread atomics
- [ ] **Double-Buffer Strategy:** Works, but async compute may serialize on Intel Macs
- [ ] **Displacement Buffer:** `imageStore` supported; no float atomics needed
- [ ] **Workgroup Sizes:** Use 64 threads (8x8) - good for Apple's SIMD width of 32

See also: [Appendix D in PROCEDURAL_GRASS_IMPLEMENTATION_PLAN.md](./PROCEDURAL_GRASS_IMPLEMENTATION_PLAN.md#appendix-d-moltenvk--macos-compatibility)

---

## References

- [MoltenVK GitHub](https://github.com/KhronosGroup/MoltenVK)
- [MoltenVK Vulkan Feature Support](https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md)
- [Metal Feature Set Tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf)
