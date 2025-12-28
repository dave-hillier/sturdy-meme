# SpeedTree Rendering Enhancement Plan

Based on GPU Gems 3, Chapter 4: "Next-Generation SpeedTree Rendering"

## Current Implementation Assessment

The codebase already implements several advanced tree rendering techniques:

| Feature | Current Status | Notes |
|---------|---------------|-------|
| Multi-frequency wind | **Implemented** | 3-layer sine wave oscillation in `wind_animation_common.glsl` |
| Impostor LOD | **Implemented** | 8x8 octahedral atlas with frame blending |
| Cascaded shadows | **Implemented** | Per-cascade LOD with separate branch/leaf cutoffs |
| Leaf translucency | **Partially implemented** | Subsurface scattering in `tree_leaf.frag` |
| GPU-driven culling | **Implemented** | Hi-Z occlusion, frustum, spatial grid |
| Billboard leaves | **Implemented** | SSBO instancing with quaternion rotation |

## Enhancement Opportunities

### Phase 1: Improved Leaf Lighting

**Goal**: Enhanced two-sided lighting with view-dependent color transmission

#### 1.1 Backlit Transmission Effect
**Files**: `shaders/tree_leaf.frag`

The GPU Gems article describes how leaves appear yellowish when backlit due to light transmission. Currently the shader has basic translucency but lacks the color shift effect.

**Implementation**:
- Detect backlit condition: `dot(lightDir, viewDir) > 0`
- Lerp between diffuse color and warmer transmission tint based on angle
- Scale transmission by leaf thickness estimate (alpha channel)

```glsl
// Pseudocode for backlit transmission
float backlit = max(0.0, dot(normalize(lightDir), normalize(viewDir)));
vec3 transmissionTint = leafColor * vec3(1.2, 1.1, 0.7); // Warm yellow shift
vec3 finalColor = mix(diffuseColor, transmissionTint, backlit * translucency);
```

#### 1.2 Specular Refinement
**Files**: `shaders/tree_leaf.frag`

Add V-shaped normal bias along leaf axis to simulate the central vein's effect on specular highlights.

**Implementation**:
- Use texture coordinates to bias normal toward leaf axis
- Reduce mipmap level for specular calculations to reduce aliasing

---

### Phase 2: Shadow Quality Improvements

**Goal**: Eliminate leaf shadow artifacts and optimize cascade updates

#### 2.1 Leaf Self-Shadowing Offset
**Files**: `shaders/tree_leaf_shadow.vert`, `shaders/tree_leaf.frag`

The article describes a technique where leaf cards are translated toward the light during shadow map generation, then offset during shadow lookup to eliminate streak artifacts.

**Implementation**:
- In shadow pass: Offset leaf quad positions slightly toward light source
- In main pass: Apply corresponding offset when sampling shadow map
- Scale offset by leaf size to maintain proper occlusion

#### 2.2 Variable Cascade Update Rates (Optional)
**Files**: `src/renderer/ShadowRenderer.cpp` or equivalent

The article uses different update frequencies per cascade:
- Cascade 0: Every frame (full detail)
- Cascade 1: Every 2 frames (exclude fronds)
- Cascade 2: Every 4 frames (leaves only)

**Implementation**:
- Track frame counter per cascade
- Skip geometry types based on cascade and frame parity
- Requires temporal reprojection for smooth transitions

---

### Phase 3: Silhouette Enhancement

**Goal**: Add depth and irregularity to branch silhouettes

#### 3.1 Silhouette Fin Extrusion
**Files**: New `shaders/tree_silhouette.geom`, modify `tree.vert`

The GPU Gems technique extrudes fins from silhouette edges perpendicular to the view vector, then uses height map tracing to create irregular outlines.

**Implementation Steps**:

1. **Silhouette Detection** (Geometry Shader)
   - Identify edges where `dot(normal, viewDir)` changes sign
   - Extrude quads perpendicular to view vector

2. **Height Map Tracing**
   - Sample bark height/displacement map along fin
   - Clip fin at height map boundary for irregular silhouette

3. **Distance Fade**
   - Reduce silhouette width with distance (performance + visual)
   - Disable silhouettes beyond impostor transition distance

**Complexity**: High - requires geometry shader pipeline variant

#### 3.2 Relief/Parallax Mapping for Bark (Alternative)
**Files**: `shaders/tree.frag`

If silhouette fins are too complex, relief mapping can add perceived depth to bark at glancing angles.

**Implementation**:
- Add height map texture to bark material
- Implement steep parallax mapping with 4-8 ray steps
- Apply only at close distances (< 20m)

---

### Phase 4: LOD Transition Improvements

**Goal**: Smoother, less noticeable LOD transitions

#### 4.1 Alpha Fizzle LOD
**Files**: `shaders/tree.vert`, `shaders/tree.frag`, `shaders/tree_impostor.frag`

The article uses noise-based alpha testing with offset thresholds to create a "fizzle" dissolve between LOD levels.

**Implementation**:
- Generate or use existing noise texture
- During LOD transition, offset alpha test threshold based on LOD blend factor
- Out-going LOD: Increase threshold (more pixels fail)
- In-coming LOD: Decrease threshold (more pixels pass)

```glsl
// Pseudocode for fizzle LOD
float noise = texture(noiseMap, screenUV).r;
float threshold = lodBlend * 0.5; // 0 to 0.5 range
if (isOutgoing && noise < threshold) discard;
if (isIncoming && noise > (1.0 - threshold)) discard;
```

#### 4.2 Alpha-to-Coverage
**Files**: Pipeline creation, `shaders/tree_leaf.frag`

Use MSAA alpha-to-coverage for order-independent leaf transparency.

**Implementation**:
- Enable `alphaToCoverageEnable` in pipeline blend state
- Output alpha as coverage mask instead of pure discard
- Requires MSAA render target

**Benefits**:
- Eliminates scintillation on animated vegetation
- Order-independent transparency for overlapping leaves

---

### Phase 5: Additional Effects

#### 5.1 Ambient Occlusion Per-Leaf
**Files**: Leaf generation, `shaders/tree_leaf.frag`

Pre-compute leaf AO based on position within tree canopy.

**Implementation**:
- During tree generation, raycast from leaf toward sky
- Store AO factor in leaf instance data (or vertex color)
- Darken leaves deeper in the canopy

#### 5.2 Seasonal Color Variation
**Files**: Already partially implemented in impostor system

Extend autumn hue shift to full-detail leaves.

**Implementation**:
- Pass `autumnHueShift` uniform to leaf shader
- Apply same hue rotation as impostors

---

## Implementation Priority

| Priority | Feature | Impact | Effort |
|----------|---------|--------|--------|
| 1 | Backlit transmission | High visual quality | Low |
| 2 | Alpha-to-coverage | Antialiasing quality | Low |
| 3 | Leaf shadow offset | Shadow quality | Medium |
| 4 | Alpha fizzle LOD | LOD pop reduction | Medium |
| 5 | Parallax bark mapping | Branch detail | Medium |
| 6 | Silhouette fins | High visual quality | High |
| 7 | Variable cascade updates | Performance | Medium |
| 8 | Per-leaf AO | Canopy realism | Medium |

## Testing Each Enhancement

### Phase 1 Testing
- Position camera inside tree canopy looking toward sun
- Verify leaves show warm transmission when backlit
- Compare against standard diffuse-only lighting

### Phase 2 Testing
- Enable shadows and observe leaf card shadows
- Verify no elongated streak artifacts on flat leaf quads
- Test shadow quality at various sun angles

### Phase 3 Testing
- View tree branches at grazing angles
- Verify irregular silhouette edges (not flat polygon edges)
- Check performance with multiple trees

### Phase 4 Testing
- Walk toward/away from trees at LOD transition distance
- Verify smooth dissolve without obvious "pop"
- Check alpha-to-coverage reduces leaf shimmering

## Files to Modify

### Shader Files
- `shaders/tree_leaf.frag` - Transmission, AO, seasonal color
- `shaders/tree_leaf_shadow.vert` - Shadow offset
- `shaders/tree.frag` - Parallax mapping, fizzle LOD
- `shaders/tree_impostor.frag` - Fizzle LOD matching
- New: `shaders/tree_silhouette.geom` - Silhouette extrusion

### C++ Files
- `src/vegetation/TreeRenderer.cpp` - Pipeline modifications
- `src/vegetation/TreeLODSystem.cpp` - Fizzle parameters
- `src/vegetation/TreeGenerator.cpp` - Per-leaf AO calculation

### Asset Files
- Bark height/displacement maps for parallax
- Noise texture for fizzle LOD (or procedural)

## Dependencies

- Phase 1 (Leaf Lighting): No dependencies
- Phase 2 (Shadows): No dependencies
- Phase 3 (Silhouettes): Requires geometry shader support
- Phase 4 (LOD): Alpha-to-coverage requires MSAA
- Phase 5 (Effects): Depends on Phase 1 completion

## Reference

- GPU Gems 3, Chapter 4: https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-4-next-generation-speedtree-rendering
- Current wind implementation: `shaders/wind_animation_common.glsl`
- Current LOD system: `src/vegetation/TreeLODSystem.cpp`
