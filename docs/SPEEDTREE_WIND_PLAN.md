# SpeedTree Wind Rendering Improvement Plan

## Current Implementation Analysis

### What We Have

The current wind system implements a **flat, leaf-only animation model**:

| Feature | Current State |
|---------|--------------|
| Trunk Motion | ❌ None |
| Branch Motion | ❌ None (explicitly disabled) |
| Leaf Motion | ✅ Multi-frequency sine oscillation |
| Hierarchical Propagation | ❌ None |
| Per-tree Variation | ✅ Wind phase offset per tree |
| Per-branch Variation | ❌ None |
| Spatial Wind Variation | ✅ Simplex noise sampling |

**Current leaf shader formula** (`shaders/tree_leaf.vert:84-88`):
```glsl
vec3 windSway = swayFactor * windStrength * vec3(windDir.x, 0.0, windDir.y) * (
    0.5 * sin(windTime * gustFreq + windOffset) +
    0.3 * sin(2.0 * windTime * gustFreq + 1.3 * windOffset) +
    0.2 * sin(5.0 * windTime * gustFreq + 1.5 * windOffset)
);
```

This gives pleasant multi-frequency leaf flutter but lacks the **hierarchical motion propagation** that makes real trees convincing.

---

## GPU Gems 3 SpeedTree Approach Summary

The SpeedTree technique models trees as a **hierarchical kinematic chain**:

```
         Wind Force
              ↓
        ┌─────────────┐
        │    TRUNK    │  ← Slow, large amplitude sway
        └──────┬──────┘
               ↓ (propagated rotation)
        ┌─────────────┐
        │  BRANCHES   │  ← Medium frequency, phase-shifted
        └──────┬──────┘
               ↓ (propagated rotation)
        ┌─────────────┐
        │   LEAVES    │  ← High frequency flutter
        └─────────────┘
```

### Key Techniques

1. **Hierarchical Motion Accumulation**: Each vertex is transformed by concatenated rotations from root to tip
2. **Quaternion-based Rotation**: Avoids gimbal lock, enables smooth interpolation
3. **Per-branch Data Encoding**: Origin, axis, stiffness stored per branch
4. **Vertex Weight Blending**: Vertices store weights for parent/child branch influence
5. **Phase Shifts**: Random per-branch phase for natural asynchronous motion
6. **Spring-Damper Model**: Underlying physics simulation for trunk/branch dynamics

---

## Improvement Plan

### Phase 1: Add Trunk Sway (Foundation)

**Goal**: Make the entire tree sway gently in the wind as a single rigid body.

**Changes Required**:

1. **Modify `tree.vert`** - Add trunk sway calculation:
   - Calculate trunk rotation axis perpendicular to wind direction
   - Apply low-frequency, large-amplitude sine oscillation
   - Transform entire mesh by trunk rotation matrix

2. **Modify `tree_leaf.vert`** - Apply same trunk sway to leaf base positions:
   - Leaves must inherit trunk motion before adding their own flutter

3. **Add trunk parameters to WindUniforms**:
   - `trunkSwayAmplitude`: Maximum trunk rotation angle (radians)
   - `trunkSwayFrequency`: Oscillation speed (Hz)

**Vertex Shader Pseudocode**:
```glsl
// Trunk sway (applied to ALL tree geometry)
vec3 trunkAxis = vec3(-windDir.y, 0.0, windDir.x);  // Perpendicular to wind
float trunkAngle = trunkSwayAmplitude * windStrength * sin(windTime * trunkSwayFreq + treePhase);
mat3 trunkRotation = rotationMatrix(trunkAxis, trunkAngle);

// Apply trunk rotation around tree base
vec3 pivotPoint = vec3(0.0, 0.0, 0.0);  // Tree base in model space
localPos = trunkRotation * (localPos - pivotPoint) + pivotPoint;
```

**Testing**: Trees should visibly sway at the trunk level. The entire silhouette should move.

---

### Phase 2: Add Branch Animation

**Goal**: Branches bend independently with phase-shifted oscillation relative to their parent.

**Data Requirements**:

1. **Branch Level** (already stored): `inColor.a` contains branch level 0-3
2. **Branch Pivot Point** (already stored): `inColor.rgb` contains pivot position
3. **New**: Per-branch phase offset (can derive from pivot position hash)

**Changes Required**:

1. **Enhance `tree.vert`**:
   - Calculate branch rotation based on branch level
   - Higher branch levels = higher frequency, smaller amplitude
   - Accumulate rotations: trunk → primary branch → secondary branch

2. **Branch animation formula** (from GPU Gems):
   ```glsl
   float branchPhase = hash(branchPivot);  // Deterministic from pivot position
   float branchFreq = trunkSwayFreq * (1.0 + branchLevel * 0.5);
   float branchAmp = trunkSwayAmplitude * (0.5 / (1.0 + branchLevel));

   float branchAngle = branchAmp * windStrength * (
       cos(windTime * branchFreq + branchPhase) +
       0.3 * cos(2.0 * windTime * branchFreq + branchPhase * 1.3)
   );
   ```

3. **Rotation accumulation**:
   ```glsl
   // Rotate around branch pivot point
   vec3 toBranchPivot = localPos - branchPivot;
   vec3 rotatedPos = branchRotation * toBranchPivot + branchPivot;

   // Then apply trunk rotation to the whole result
   rotatedPos = trunkRotation * rotatedPos;
   ```

**Testing**: Branches at different levels should sway at different rates. Higher branches should appear more "jittery" than the trunk.

---

### Phase 3: Enhanced Leaf Motion

**Goal**: Leaves inherit full branch hierarchy motion plus their own high-frequency flutter.

**Changes Required**:

1. **Propagate branch transforms to leaves**:
   - Compute shader culling must pass branch transform data
   - Or: encode branch index in leaf instance data

2. **Add leaf-specific high-frequency detail**:
   ```glsl
   // After applying inherited branch motion:
   vec3 leafFlutter = leafSize * 0.1 * vec3(
       sin(windTime * 15.0 + leafPhase),
       sin(windTime * 12.0 + leafPhase * 1.1),
       sin(windTime * 18.0 + leafPhase * 0.9)
   );
   ```

3. **Leaf orientation variation** - Leaves should also rotate slightly, not just translate:
   - Add small rotation to leaf orientation quaternion based on wind

**Testing**: Leaves should flutter rapidly while following their parent branch's slower sway.

---

### Phase 4: Wind Field Improvements (Optional)

**Goal**: Support localized wind effects (gusts, wakes behind objects).

**Enhancements**:

1. **Wind gust waves**: Traveling waves of increased wind strength
2. **Object wind shadows**: Reduced wind behind buildings/terrain
3. **Turbulence zones**: Random localized wind variation

**Implementation Options**:
- Texture-based wind field (sample 2D texture in shader)
- Analytical wind primitives (spheres, cones)
- Compute shader wind simulation

---

## Data Structure Changes

### WindUniforms Extension

```glsl
// Current (32 bytes):
layout(binding = X) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = dir, z = strength, w = speed
    vec4 windParams;                 // x = gustFreq, y = gustAmp, z = noiseScale, w = time
};

// Proposed (48 bytes):
layout(binding = X) uniform WindUniforms {
    vec4 windDirectionAndStrength;  // xy = dir, z = strength, w = speed
    vec4 windParams;                 // x = gustFreq, y = gustAmp, z = noiseScale, w = time
    vec4 trunkParams;                // x = swayAmplitude, y = swayFrequency, z = dampingFactor, w = reserved
};
```

### TreeRenderData Extension

```glsl
// Current:
struct TreeRenderData {
    mat4 model;
    vec4 tintAndParams;       // rgb = leaf tint, a = autumn hue
    vec4 windPhaseAndLOD;     // x = wind phase, y = LOD blend, zw = reserved
};

// Proposed (use reserved space):
struct TreeRenderData {
    mat4 model;
    vec4 tintAndParams;       // rgb = leaf tint, a = autumn hue
    vec4 windPhaseAndLOD;     // x = wind phase, y = LOD blend, z = trunk stiffness, w = height
};
```

---

## Implementation Order

| Phase | Scope | Complexity | Visual Impact |
|-------|-------|------------|---------------|
| 1. Trunk Sway | 2 shader files, 1 UBO | Low | High |
| 2. Branch Animation | 1 shader file | Medium | High |
| 3. Enhanced Leaves | 2 files (shader + compute) | Medium | Medium |
| 4. Wind Field | New system | High | Medium |

**Recommended Approach**: Complete Phase 1 first as it provides the highest visual improvement with lowest complexity. Each subsequent phase builds on the previous.

---

## Reference Materials

- GPU Gems 3, Chapter 6: "GPU-Generated Procedural Wind Animations for Trees"
- GPU Gems 3, Chapter 4: "Next Generation SpeedTree Rendering" (shadows/lighting)
- Ghost of Tsushima GDC Talk: Procedural wind field system
- Current codebase: `shaders/tree.vert`, `shaders/tree_leaf.vert`, `src/atmosphere/WindSystem.h`

---

## Testing Checklist

After each phase:

1. [ ] Build compiles without errors: `./build-claude.sh`
2. [ ] Application runs without crashes: `./run-debug.sh`
3. [ ] Visual verification:
   - Open Weather tab in ImGui
   - Adjust wind strength from 0 to 3
   - Verify trees respond appropriately
4. [ ] Performance check:
   - Frame time should not increase significantly
   - GPU usage should remain reasonable with many trees
5. [ ] Edge cases:
   - Wind strength = 0 (trees should be completely still)
   - Wind direction changes (smooth transition)
   - LOD transitions (animation should blend with impostors)
