# Water Shader AAA Migration Plan

## Current System Summary

The water implementation is production-quality, drawing from several respected AAA references:
- **Far Cry 5** (GDC 2018) - Flow maps, scattering, foam
- **Sea of Thieves** (GDC 2018) - Subsurface scattering, temporal foam, wakes
- **Portal 2** - Two-phase flow sampling
- **SIGGRAPH 2015** - Screen-space reflections

### Existing Features

| Feature | Implementation | Quality |
|---------|---------------|---------|
| **Wave Animation** | 4-layer Gerstner waves with dispersion | Good |
| **Reflections** | SSR + environment fallback | Good |
| **Foam System** | Jacobian-based + shore + flow + wakes | Excellent |
| **Flow Animation** | Two-phase sampling (no pulsing) | Excellent |
| **Light Transport** | Beer-Lambert absorption + turbidity | Good |
| **SSS** | Wave slope + back-lighting | Good |
| **Caustics** | Dual-layer animated patterns | Basic |
| **Interactive** | Splash/ripple displacement | Good |
| **Material Blending** | Multi-water-type transitions | Good |
| **Performance** | Tile culling, LOD-aware FBM | Good |

---

## Migration Phases

### Phase 1: FFT Ocean (Highest Impact) ⬅️ CURRENT
**Goal:** Replace Gerstner waves with FFT simulation

1. Implement Phillips/JONSWAP spectrum generator (compute shader)
2. Create 2D FFT compute pipeline (256×256 or 512×512)
3. Generate displacement map (height + horizontal XZ)
4. Generate derivative maps (normals, Jacobian, foam)
5. Sample FFT maps in vertex shader instead of analytical Gerstner
6. Add wind-driven parameters (direction, speed, fetch)
7. Implement cascaded FFT for multi-scale detail

**Output:** `fft_spectrum.comp`, `fft_butterfly.comp`, `fft_displacement.comp`

### Phase 2: Planar Reflections
**Goal:** Capture full-scene reflections for water

1. Create reflection render pass with flipped camera
2. Clip geometry below water plane (oblique near plane)
3. Render at 1/2 or 1/4 resolution
4. Store in reflection texture
5. Blend SSR + Planar based on SSR confidence
6. Add temporal jitter and accumulation

**Output:** `WaterReflection.h/cpp`, `water_reflection.frag`

### Phase 3: Underwater System
**Goal:** Support camera under water surface

1. Detect camera below water level
2. Implement underwater fog (depth-based absorption)
3. Project caustics onto underwater surfaces (deferred decal)
4. Add underwater light shafts (extend god rays)
5. Render water surface from below (invert normals, Snell's window)
6. Add underwater particle effects (bubbles, sediment)

**Output:** `UnderwaterSystem.h/cpp`, `underwater_fog.frag`, `underwater_caustics.comp`

### Phase 4: Breaking Waves
**Goal:** Realistic shore wave behavior

1. Calculate wave shoaling based on water depth
2. Detect breaking condition (height/depth ratio)
3. Deform wave mesh to show forward-leaning crest
4. Trigger spray particle emitter at break point
5. Add splash and foam injection
6. Implement wash-up foam trails on beach

**Output:** `WaveBreaking.h/cpp`, modifications to `water.vert`

### Phase 5: Volumetric Caustics
**Goal:** Light patterns on underwater objects

1. Generate caustic mesh from water surface normals
2. Project caustic pattern as deferred light volume
3. Animate caustic projection based on wave state
4. Fade with water depth (absorption)
5. Integrate with underwater god rays

**Output:** `VolumetricCaustics.h/cpp`, `caustics_projection.comp`

### Phase 6: Advanced Refraction
**Goal:** Physically correct distortion

1. Calculate proper refraction vector using IOR (1.33 for water)
2. Ray march through screen-space depth for refraction
3. Add chromatic aberration at edges
4. Handle depth discontinuities (don't sample sky through shallow)
5. Add temporal stability

**Output:** `water_refraction.comp` or integrate into `water.frag`

### Phase 7: River System
**Goal:** Proper flowing water bodies

1. Create river mesh from spline curves
2. Generate per-river flow maps
3. Implement waterfall rendering with particle splash
4. Add river-specific foam (rapids, rocks)
5. Integrate with existing flow system

**Output:** `RiverSystem.h/cpp`, `river.vert/frag`

### Phase 8: Particle Foam (Optional, High Cost)
**Goal:** Individual bubble simulation

1. Create foam particle buffer (GPU particles)
2. Spawn particles at wave crests, impacts, wakes
3. Simulate particle movement (advection, spreading)
4. Handle particle lifecycle (fade, pop)
5. Render as instanced quads or point sprites

**Output:** `FoamParticles.h/cpp`, `foam_particles.comp/vert/frag`

---

## Priority Matrix

| Phase | Impact | Complexity | Priority |
|-------|--------|------------|----------|
| 1. FFT Ocean | ★★★★★ | High | **P0** |
| 2. Planar Reflections | ★★★★☆ | Medium | **P0** |
| 3. Underwater | ★★★★☆ | Medium-High | **P1** |
| 4. Breaking Waves | ★★★☆☆ | Medium | **P1** |
| 5. Volumetric Caustics | ★★★☆☆ | Medium | **P2** |
| 6. Advanced Refraction | ★★☆☆☆ | Low-Medium | **P2** |
| 7. River System | ★★★☆☆ | High | **P2** |
| 8. Particle Foam | ★★☆☆☆ | High | **P3** |

---

## Key References

- **FFT Ocean:** "Simulating Ocean Water" (Tessendorf, 2001)
- **Planar Reflections:** Common technique, see Unity/Unreal implementations
- **Underwater:** Sea of Thieves GDC 2018, Subnautica postmortems
- **Breaking Waves:** "Real-time Breaking Waves" (SIGGRAPH 2012)
- **Caustics:** "Water Caustics" (GPU Gems Chapter 2)
