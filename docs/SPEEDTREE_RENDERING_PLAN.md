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

## Priority Fix: Leaf Flickering and LOD Clarity

### Problem 1: Leaf Flickering

**Root cause**: Leaf culling uses continuous `lodBlendFactor` for stochastic culling. Even tiny floating point variations cause different leaves to be culled each frame.

In `tree_leaf_cull_common.glsl`:
```glsl
// This flickers because lodBlendFactor has floating point noise!
if (lodBlendFactor > 0.0) {
    float adjustedBlend = sqrt(lodBlendFactor);
    if (instanceHash < adjustedBlend) {  // Different leaves each frame!
        return true;
    }
}
```

Even if the camera is still, `lodBlendFactor` computed from screen-space error may vary slightly due to floating point precision in the chain:
`tanHalfFOV` → `screenError` → `blendFactor` → `adjustedBlend`

A change from 0.249 to 0.251 flips leaves with `instanceHash` around 0.5.

**Solution: Quantized Distance Bands with Hysteresis**

Replace continuous LOD factor with discrete distance bands:

```glsl
// Quantize distance into bands (e.g., every 10 meters)
float bandSize = 10.0;
float quantizedDist = floor(distToCamera / bandSize) * bandSize;

// Add hysteresis: use different thresholds for appearing vs disappearing
// Higher lodFactor = more leaves dropped, so:
// - To keep a visible leaf visible longer: use LOWER effective distance
// - To delay a hidden leaf appearing: use HIGHER effective distance
float hysteresis = bandSize * 0.3;  // 30% overlap
float effectiveDist = wasVisibleLastFrame
    ? quantizedDist - hysteresis   // Pretend closer → lower lodFactor → stays visible
    : quantizedDist + hysteresis;  // Pretend farther → higher lodFactor → stays hidden

float lodFactor = calculateLodFactor(effectiveDist, lodStart, lodEnd);
```

However, tracking per-leaf state is expensive. **Simpler alternative**:

```glsl
// Use leaf hash to create per-leaf distance offsets (pseudo-hysteresis)
float leafDistOffset = (instanceHash - 0.5) * hysteresisRange;
float effectiveDist = distToCamera + leafDistOffset;
float lodFactor = calculateLodFactor(effectiveDist, lodStart, lodEnd);
```

This spreads transitions across a range rather than having all leaves switch at the same distance.

**Simplest fix: Quantize the blend factor on CPU**

Before passing `lodBlendFactor` to the GPU, quantize it to discrete steps:

```cpp
// In TreeLODSystem::update() or TreeLeafCulling::recordCulling()
// Quantize to 10 discrete levels (0.0, 0.1, 0.2, ... 1.0)
float quantizedBlend = std::round(lodBlendFactor * 10.0f) / 10.0f;
treeData.lodBlendFactor = quantizedBlend;
```

This ensures `lodBlendFactor` only changes when crossing a 0.1 threshold, not on every tiny floating point variation.

**Files to modify:**
- `src/vegetation/TreeLeafCulling.cpp` - Quantize `lodBlendFactor` before GPU upload
- Or: `src/vegetation/TreeLODSystem.cpp` - Quantize in `update()` before storing

---

### Problem 2: Confusing Screen-Space Error Metrics

**Current state**: Abstract pixel-based thresholds that are hard to reason about.

**Solution: Add Simple Distance Mode as Alternative**

Keep screen-space error for advanced users, but add a simpler distance-based mode:

```cpp
struct TreeLODSettings {
    // Simple mode: direct distance control
    bool useSimpleDistanceMode = true;
    float fullDetailDistance = 50.0f;    // Meters - full geometry below this
    float impostorStartDistance = 80.0f; // Meters - start blending to impostor
    float impostorOnlyDistance = 120.0f; // Meters - impostor only beyond this

    // Advanced mode: screen-space error (existing)
    bool useScreenSpaceError = false;
    float errorThresholdFull = 2.0f;     // Pixels
    // ...
};
```

**Documentation for screen-space error** (add to comments):
```cpp
// Screen-space error measures: "How many pixels would fine detail occupy?"
// Formula: screenError = detailSize × screenHeight / (2 × distance × tan(fov/2))
//
// At 1080p with 90° FOV (tan(45°)=1) and 10cm (0.1m) branch detail:
//   screenError = 0.1 × 1080 / (2 × distance × 1) = 54 / distance
//
//   - 13m  → 4.2 pixels (clearly visible branches)
//   - 27m  → 2.0 pixels (threshold - switch to impostor blend)
//   - 54m  → 1.0 pixels (full impostor)
//   - 108m → 0.5 pixels (could cull entirely)
//
// errorThresholdFull=2.0 means: "Switch to impostor when branches are <2 pixels"
// This happens at ~27 meters for 10cm detail at 1080p/90° FOV
```

**Files to modify:**
- `src/vegetation/TreeImpostorAtlas.h` - Add simple distance mode to `TreeLODSettings`
- `src/vegetation/TreeLODSystem.cpp` - Implement simple distance mode path
- `shaders/tree_impostor_cull.comp` - Support both modes

---

### Problem 3: Screen-Space Error Not Adaptive

**Current state**: Fixed thresholds regardless of scene complexity. A single tree uses the same aggressive LOD as a forest of 10,000 trees.

**Issue**: With one tree visible:
- Viewer attention is focused on it
- Performance isn't constrained
- Quality reduction is very noticeable

**Solution: Performance Budget-Based LOD**

Use a leaf count budget from the previous frame to adapt quality:
```cpp
// In TreeLODSystem or a new AdaptiveLOD component
struct AdaptiveLODState {
    uint32_t leafBudget = 500000;          // Target max leaves
    uint32_t lastFrameLeafCount = 0;       // From indirect draw readback
    float adaptiveScale = 1.0f;            // Current scale factor
    float scaleSmoothing = 0.1f;           // Smooth transitions
};

void updateAdaptiveScale(AdaptiveLODState& state, uint32_t renderedLeaves) {
    state.lastFrameLeafCount = renderedLeaves;
    float budgetRatio = float(renderedLeaves) / float(state.leafBudget);

    // Target scale based on budget utilization
    float targetScale = 1.0f;
    if (budgetRatio < 0.3f) {
        // Way under budget - render at maximum quality
        targetScale = 3.0f;  // 3x quality for single tree scenarios
    } else if (budgetRatio < 0.6f) {
        // Under budget - render higher quality
        targetScale = 1.5f;
    } else if (budgetRatio > 0.95f) {
        // Over budget - reduce quality
        targetScale = 0.7f;
    }

    // Smooth transition to avoid popping
    state.adaptiveScale = glm::mix(state.adaptiveScale, targetScale, state.scaleSmoothing);
}

// Apply to LOD thresholds:
float effectiveErrorThreshold = settings.errorThresholdFull * state.adaptiveScale;
// Higher threshold = larger screen error allowed = more full-detail trees
```

**Benefits of budget approach:**
- Automatically adapts to different tree types (pine needles vs oak leaves)
- Handles mixed scenes (some trees close, some far)
- Self-adjusting to hardware capabilities
- Single tree gets ~3x quality boost automatically

**Files to modify:**
- `src/vegetation/TreeLODSystem.h` - Add `AdaptiveLODState`
- `src/vegetation/TreeLODSystem.cpp` - Implement budget tracking and scaling
- `src/vegetation/TreeLeafCulling.cpp` - Readback leaf count from indirect buffer

---

### Implementation Order

1. **Fix leaf flickering first** (most noticeable issue) ✓
   - Quantize lodBlendFactor to prevent floating point noise

2. **Add performance budget LOD** (context-aware quality)
   - Track rendered leaf count from previous frame
   - Scale LOD thresholds based on budget utilization
   - Improves single-tree and sparse scene quality

3. **Add simple distance mode** (easier to tune)
   - Add settings to `TreeLODSettings`
   - Implement in CPU and GPU LOD paths

4. **Document screen-space error** (for advanced users)
   - Add clear comments explaining the formula
   - Include distance examples at common resolutions

---

## References

- [GPU Gems 3 Chapter 4: Next-Generation SpeedTree Rendering](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-4-next-generation-speedtree-rendering)
- Current implementation: `src/vegetation/Tree*.cpp|h`, `shaders/tree*.glsl`
