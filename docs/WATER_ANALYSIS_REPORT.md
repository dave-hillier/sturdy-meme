# Water System Analysis Report

**Date:** December 2024
**Compared Against:** Far Cry 5, Unreal Engine 5, Sea of Thieves

## Executive Summary

The current water system is sophisticated, implementing many AAA techniques. However, several issues prevent it from achieving modern game quality. The most impactful problems are:

1. Only 1 of 3 FFT cascades is being used
2. Hardcoded "English estuary" colors override material settings
3. No environment cubemap for reflection fallback
4. Missing GPU tessellation for wave geometry detail

---

## Current Implementation Strengths

| Feature | Implementation | Source Reference |
|---------|---------------|------------------|
| FFT Ocean | Tessendorf Phillips spectrum, 3 cascades | Tessendorf 2001 |
| Flow Maps | Two-phase sampling (no pulsing) | Far Cry 5 GDC 2018 |
| Foam System | Jacobian + temporal persistence + wakes | Sea of Thieves GDC 2018 |
| Light Transport | Beer-Lambert absorption + turbidity | Far Cry 5 GDC 2018 |
| SSS | Wave slope + back-lighting | Sea of Thieves GDC 2018 |
| SSR | Half-res with temporal filtering | SIGGRAPH 2015 |
| Material Blending | Multi-water-type transitions | Far Cry 5 |

---

## Issues Identified

### 1. FFT Cascades Not Fully Utilized (HIGH IMPACT)

**Location:** `shaders/water.vert:178-186`

**Problem:** Only cascade 0 (256m large swells) is sampled. Cascades 1 (64m medium waves) and 2 (16m ripples) are computed but discarded.

```glsl
// Current code - only uses cascade 0
vec4 disp0 = sampleFFTOcean(pos, push.oceanSize0);
totalDisplacement = disp0.xyz;
// Cascades 1 and 2 are NEVER sampled!
```

**Impact:** Water lacks medium and high-frequency detail. Appears too smooth/blobby.

**Fix:** Sample all cascades and sum displacements:
```glsl
displacement = cascade0 + cascade1 + cascade2;
normal = blendNormals(normal0, normal1, normal2);
```

---

### 2. Hardcoded "English Estuary" Colors (HIGH IMPACT)

**Location:** `shaders/water.frag:479-495`

**Problem:** Hardcoded muddy colors override the material blending system:
```glsl
vec3 shoreColor = vec3(0.35, 0.38, 0.32);   // Muddy brown-green
vec3 surfaceColor = vec3(0.15, 0.22, 0.25); // Grey-green
vec3 depthColor = vec3(0.05, 0.1, 0.15);    // Dark grey-blue
```

**Impact:** Water always looks murky regardless of `WaterType` settings.

**Fix:** Derive colors from material properties or make configurable via uniforms.

---

### 3. Simplified Environment Reflections (MEDIUM IMPACT)

**Location:** `shaders/water.frag:192-211`

**Problem:** Fallback when SSR fails is a procedural sky gradient, not a proper environment cubemap.

**Impact:** Reflections look flat/artificial when there's no SSR hit.

**Fix:** Add environment cubemap sampling as fallback between SSR and procedural sky.

---

### 4. No Wave Geometry Tessellation (MEDIUM IMPACT)

**Location:** Tile culling exists (`WaterTileCull`) but no GPU tessellation.

**Problem:** Wave geometry detail limited by fixed mesh subdivision.

**Impact:** Waves look either too smooth (distance) or polygon-y (close up).

**Fix:** Add distance-based tessellation shader stage.

---

### 5. Caustics Are Surface-Only (MEDIUM IMPACT)

**Location:** `shaders/water.frag:554-585`

**Problem:** Caustics sampled on water surface based on depth, not projected onto underwater terrain.

**Impact:** Caustics don't appear on underwater rocks/sand as in real life.

**Fix:** Project caustics using terrain depth in a deferred pass.

---

### 6. No Underwater Rendering (MEDIUM IMPACT)

**Location:** Planned in `WATER_AAA_MIGRATION_PLAN.md` Phase 2, not implemented.

**Missing Features:**
- Volumetric fog absorption
- Color shift based on depth
- Snell's window effect
- God rays

---

### 7. No Breaking Waves (MEDIUM IMPACT)

**Location:** Shore foam is depth-based only.

**Problem:** No wave shoaling or breaking wave detection.

**Physics:** Waves break when: `waveHeight > 0.78 × waterDepth`

**Missing:**
- Wave height amplification in shallow water
- Breaking condition detection
- Spray particles at break points

---

### 8. Specular Roughness Defaults (LOW IMPACT)

**Recommendation:** Ensure default `specularRoughness` is ~0.02-0.05 for calm water. Higher values make sun specular too diffuse.

---

## Comparison Matrix

| Feature | Far Cry 5 | Unreal 5 | Sea of Thieves | Current |
|---------|-----------|----------|----------------|---------|
| Wave Simulation | FFT + Gerstner | FFT multi-cascade | FFT Tessendorf | FFT (1 cascade) |
| Tessellation | Screen-space | Quadtree GPU | Hardware | None |
| Reflections | SSR + Planar + Probe | SSR + Lumen + Probe | SSR + Cubemap | SSR + Procedural |
| Foam | Jacobian + SDF | Jacobian + Distance | Jacobian + Temporal | Jacobian + Temporal ✓ |
| Caustics | Projected | Volumetric | Surface | Surface |
| Underwater | Full volume | Full volume | Full volume | Not implemented |
| Breaking Waves | Depth-based | Physics sim | Foam bursts | Not implemented |
| SSS | Back-lighting | GGX-based | Slope + back-light | Slope + back-light ✓ |

---

## Recommended Fix Priority

### Phase 1: Quick Wins (Immediate Visual Improvement)
1. ✅ Use all 3 FFT cascades in vertex shader
2. ✅ Remove hardcoded estuary colors - use material system
3. ✅ Verify specular roughness defaults

### Phase 2: Medium Effort
4. Add environment cubemap for reflection fallback
5. Project caustics onto underwater terrain
6. Add breaking wave foam detection

### Phase 3: Larger Effort
7. GPU tessellation for wave geometry
8. Underwater volume rendering (Phase 2 of AAA Migration Plan)
9. Planar reflections as SSR fallback

---

## References

- GDC 2018: "Water Rendering in Far Cry 5" - Branislav Grujic, Christian Coto Charis
- GDC 2018: "The Technical Art of Sea of Thieves" - Rare
- Tessendorf 2001: "Simulating Ocean Water"
- SIGGRAPH 2015: Screen-Space Reflections
- Unreal Engine 5 Water Documentation
