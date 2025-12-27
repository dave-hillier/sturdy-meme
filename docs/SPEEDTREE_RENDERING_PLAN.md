# SpeedTree Rendering Enhancement Plan

This document analyzes the GPU Gems 3 Chapter 4 "Next-Generation SpeedTree Rendering" techniques against the current tree rendering implementation and proposes enhancements.

## Current Implementation Summary

The codebase has a comprehensive tree rendering system with:
- Procedural tree generation (SpeedTree-style recursive branching)
- Full LOD system with dithered cross-fade blending
- Billboard impostors (legacy 17-view + octahedral 64-view)
- GPU-driven leaf culling (three-phase compute)
- Branch shadow instancing with cascade awareness
- Wind animation via Perlin noise
- PBR bark textures (albedo, normal, roughness, AO)

## GPU Gems 3 Techniques Analysis

### Already Implemented

| Technique | GPU Gems Description | Current Implementation |
|-----------|---------------------|------------------------|
| **Fizzle LOD Cross-Fade** | Two LODs rendered with noise-based alpha discard | `shouldDiscardForLOD()` in `dither_common.glsl` uses Bayer dithering |
| **Cascaded Shadow Maps** | Multiple shadow cascades for quality/performance | 4-cascade system in `ShadowSystem` |
| **Leaf Cards** | Flat 2D billboards for leaves | Quad-based leaves with instancing |
| **LOD Distance Falloff** | Smooth transitions based on distance | `TreeLODSystem` with hysteresis and blend curves |
| **Normal Mapping** | Detail on bark surfaces | `perturbNormal()` in `tree.frag` |
| **Double-Sided Leaves** | Leaves visible from both sides | `gl_FrontFacing` check in `tree_leaf.frag` |

### Gap Analysis - Potential Enhancements

#### 1. Silhouette Clipping (High Impact, High Effort)

**GPU Gems Technique:**
- Geometry shader extrudes fins from silhouette edges
- Detects silhouettes via dot product of vertex normals with view vector
- Ray-marches through height maps for proper occlusion

**Current State:** Not implemented

**Proposed Implementation:**
1. Create `tree_silhouette.geom` geometry shader
2. For each input triangle:
   - Compute dot product of each vertex normal with view direction
   - If signs differ, an edge crosses the silhouette
   - Emit additional triangles extruding perpendicular to view
3. Add height-tracing in fragment shader for fin detail
4. Use branch-level filtering (only level 0-1 trunks need silhouettes)

**Files to modify:**
- New: `shaders/tree_silhouette.geom`
- Modify: `src/vegetation/TreeRenderer.cpp` (add silhouette pipeline)
- Modify: `tree.vert` (pass data for geometry shader)

---

#### 2. Relief/Parallax Mapping on Bark (Medium Impact, Medium Effort)

**GPU Gems Technique:**
- Height mapping adds interior detail to flat trunk surfaces
- Ray marching through height map for depth

**Current State:** Only normal mapping, no parallax/relief

**Proposed Implementation:**
1. Add height map sampling to `tree.frag`
2. Implement parallax occlusion mapping (POM):
   - Offset texture coordinates based on view angle and height
   - Optional: steep parallax mapping for better quality
3. Generate or source bark height maps for existing bark textures

**Files to modify:**
- New: `shaders/parallax_common.glsl`
- Modify: `shaders/tree.frag` (add POM UV offset)
- Add: bark height textures (`assets/textures/bark/*_height.png`)

---

#### 3. Two-Sided Leaf Lighting / Subsurface Scattering (High Impact, Low Effort)

**GPU Gems Technique:**
- Backlit leaves show yellow/orange tint (light transmitting through)
- Lerp between original and modified color based on light-view angle

**Current State:** Double-sided but no translucency effect

**Proposed Implementation:**
1. In `tree_leaf.frag`, detect back-facing condition
2. When `dot(L, -V) > 0` (light behind leaf from viewer), add warm translucency:
   ```glsl
   float backlight = max(0.0, dot(L, -V));
   vec3 translucent = baseColor * vec3(1.2, 1.0, 0.4) * backlight;
   color += translucent * (1.0 - shadow) * sunIntensity;
   ```
3. Optionally add thickness map for per-leaf variation

**Files to modify:**
- Modify: `shaders/tree_leaf.frag`
- Modify: `shaders/tree_lighting_common.glsl` (add `calculateLeafTranslucency()`)

---

#### 4. Leaf Self-Shadowing Offset (Medium Impact, Low Effort)

**GPU Gems Technique:**
- During shadow map generation, translate leaf cards toward light
- Offset by ~half leaf height to avoid planar self-shadowing artifacts

**Current State:** Standard shadow projection without offset

**Proposed Implementation:**
1. In `tree_leaf_shadow.vert`, offset vertex position toward light:
   ```glsl
   vec3 offsetPos = worldPos + sunDirection * (leafSize * 0.5);
   ```
2. Alternatively, use depth-offset texture per leaf type
3. Tune offset based on leaf size to minimize artifacts

**Files to modify:**
- Modify: `shaders/tree_leaf_shadow.vert`
- Modify: `src/vegetation/TreeRenderer.cpp` (pass leaf size to shadow pass)

---

#### 5. Alpha to Coverage (Medium Impact, Low Effort)

**GPU Gems Technique:**
- MSAA converts alpha to coverage mask at subpixel level
- Antialiases transparency edges without sorting

**Current State:** Hard alpha test with `discard`

**Proposed Implementation:**
1. Enable MSAA in render pass if not already
2. In `tree_leaf.frag`, set `gl_SampleMask` based on alpha:
   ```glsl
   // Convert alpha to coverage bits
   int coverage = int(albedo.a * float(gl_NumSamples));
   gl_SampleMask[0] = (1 << coverage) - 1;
   ```
3. Or use `alphaToCoverageEnable` in pipeline state

**Files to modify:**
- Modify: `src/vegetation/TreeRenderer.cpp` (enable alpha-to-coverage in pipeline)
- Modify: `shaders/tree_leaf.frag` (adjust alpha handling)

---

#### 6. V-Shaped Leaf Normal Maps (Low Impact, Low Effort)

**GPU Gems Technique:**
- Coarser V-shaped normal simulates axial leaf division
- Lower mipmaps reduce specular aliasing

**Current State:** Flat geometry normals for leaves

**Proposed Implementation:**
1. Generate or source V-shaped normal maps for leaf textures
2. Add normal map sampling to `tree_leaf.frag`:
   ```glsl
   vec3 leafNormal = texture(leafNormalMap, fragTexCoord).xyz * 2.0 - 1.0;
   N = perturbNormalWithTangent(N, tangent, leafNormal);
   ```
3. Ensure mipmaps are generated with proper filtering

**Files to modify:**
- Modify: `shaders/tree_leaf.frag` (add normal map sampling)
- Add: leaf normal textures (`assets/textures/leaf/*_normal.png`)
- Modify: `src/vegetation/TreeSystem.cpp` (load leaf normal maps)

---

#### 7. Staggered Cascade Shadow Updates (Medium Impact, Medium Effort)

**GPU Gems Technique:**
- Cascade 0: every frame
- Cascade 1: every 2 frames
- Cascade 2: every 4 frames
- Reduces CPU/GPU overhead

**Current State:** All cascades updated every frame

**Proposed Implementation:**
1. Add frame counter to `ShadowSystem`
2. Conditionally skip cascade updates based on frame parity:
   ```cpp
   bool shouldUpdateCascade(int cascade, uint32_t frameIndex) {
       int updateFrequency = 1 << cascade; // 1, 2, 4, 8
       return (frameIndex % updateFrequency) == 0;
   }
   ```
3. Cache previous cascade matrices for skipped frames
4. Add option to force full update when camera moves significantly

**Files to modify:**
- Modify: `src/lighting/ShadowSystem.h` (add frame tracking)
- Modify: `src/lighting/ShadowSystem.cpp` (conditional updates)

---

## Recommended Implementation Order

Based on impact vs effort analysis:

### Phase 1: Quick Wins (Low Effort, High Value)
1. **Two-Sided Leaf Lighting** - Dramatic visual improvement, shader-only change
2. **Leaf Self-Shadowing Offset** - Fixes common shadow artifacts
3. **Alpha to Coverage** - Better leaf antialiasing

### Phase 2: Medium Effort Enhancements
4. **V-Shaped Leaf Normal Maps** - Adds leaf detail cheaply
5. **Relief Mapping on Bark** - Enhanced trunk detail
6. **Staggered Cascade Updates** - Performance optimization

### Phase 3: Advanced Features (High Effort)
7. **Silhouette Clipping** - Complex geometry shader work, may have limited benefit for typical tree distances

---

## Testing Plan

For each enhancement:

1. **Visual Comparison**: Side-by-side before/after screenshots
2. **Performance Profiling**: GPU timing queries via existing debug overlay
3. **Edge Cases**:
   - Test at various LOD distances
   - Test with different tree types (oak, pine, willow)
   - Test under different lighting conditions (noon, sunset, overcast)
4. **Integration**: Ensure works with existing systems (wind, shadows, impostors)

---

## References

- [GPU Gems 3 Chapter 4: Next-Generation SpeedTree Rendering](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-4-next-generation-speedtree-rendering)
- Current implementation: `src/vegetation/Tree*.cpp|h`, `shaders/tree*.glsl`
