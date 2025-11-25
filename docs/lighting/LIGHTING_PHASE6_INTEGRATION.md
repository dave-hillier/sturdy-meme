# Phase 6: Integration & Polish

[← Previous: Phase 5 - Post-Processing](LIGHTING_PHASE5_POSTPROCESS.md) | [Back to Overview](LIGHTING_OVERVIEW.md)

---

## 6.1 Time of Day System

**Tasks:**
1. Create sun/moon position calculator (astronomical or simplified)
2. Update sky LUTs based on sun angle
3. Transition lighting parameters smoothly
4. Update probe lighting in real-time

---

## 6.2 Weather System

**Tasks:**
1. Create weather state data (cloud density, haze, wind)
2. Blend between weather states
3. Adjust atmospheric parameters per-weather
4. Add rain/particle effects (future work)

---

## 6.3 Performance Optimization

**Techniques:**
1. Temporal reprojection for volumetrics
2. Async compute for histogram, bilateral grid
3. Checkerboard rendering for volumetrics
4. LOD for reflection probes
5. Streaming for probe data

---

## Appendix: Vulkan-Specific Considerations

### Render Pass Structure

```
Pass 1: Shadow Depth Pass
  - Render scene depth from light view
  - Output: Shadow map texture array

Pass 2: Main Scene Pass
  - Forward render with lighting
  - Sample shadow maps
  - Output: HDR color + depth

Pass 3: Volumetric Compute
  - Build froxel grid
  - Accumulate scattering
  - Output: Volumetric texture

Pass 4: Post-Process Pass
  - Composite volumetrics
  - Tone mapping
  - Color grading
  - Output: Final LDR to swapchain
```

### Descriptor Set Layout

```cpp
Set 0: Per-frame data
  - Binding 0: Camera UBO
  - Binding 1: Light UBO
  - Binding 2: Time/exposure UBO

Set 1: Shadow data
  - Binding 0: Shadow cascade matrices
  - Binding 1: Shadow map sampler (array)

Set 2: Material data
  - Binding 0: Albedo texture
  - Binding 1: Normal map
  - Binding 2: Roughness/metallic

Set 3: Environment data
  - Binding 0: Sky LUT
  - Binding 1: Irradiance SH buffer
  - Binding 2: Reflection cubemap
```

---

[← Previous: Phase 5 - Post-Processing](LIGHTING_PHASE5_POSTPROCESS.md) | [Back to Overview](LIGHTING_OVERVIEW.md)
