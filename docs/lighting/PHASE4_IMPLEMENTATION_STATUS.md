# Phase 4: Atmospheric Scattering - Implementation Status

This document tracks the implementation status of features described in [LIGHTING_PHASE4_ATMOSPHERE.md](LIGHTING_PHASE4_ATMOSPHERE.md).

---

## Summary

| Category | Implemented | Partially Implemented | Missing |
|----------|-------------|----------------------|---------|
| Sky Model (4.1) | 8 | 2 | 2 |
| Volumetric Clouds (4.2) | 5 | 0 | 5 |
| Volumetric Haze/Fog (4.3) | 7 | 2 | 3 |
| Light Shafts (4.4) | 1 | 0 | 0 |

---

## 4.1 Physically-Based Sky Model

### Fully Implemented

| Feature | Doc Section | Implementation | Notes |
|---------|-------------|----------------|-------|
| Atmospheric Parameters | 4.1.1 | `shaders/sky.frag:30-45` | Planet radius, Rayleigh/Mie coefficients, ozone layer parameters |
| Rayleigh Phase Function | 4.1.6 | `shaders/sky.frag:275-277` | Standard `3/(16π)(1+cos²θ)` formula |
| Cornette-Shanks Phase | 4.1.6 | `shaders/sky.frag:279-284` | More accurate than Henyey-Greenstein for Mie |
| LMS Color Space | 4.1.7 | `shaders/sky.frag:53-68` | RGB↔LMS matrices for accurate sunset colors |
| Blended Rayleigh Scattering | 4.1.7 | `shaders/sky.frag:363-376` | LMS used at low sun angles, RGB at high sun |
| Earth Shadow | 4.1.8 | `shaders/sky.frag:328-355` | Penumbra calculation at sunrise/sunset |
| Ozone Absorption | 4.1.1 | `shaders/sky.frag:42-44, 286-289` | Gaussian distribution, affects horizon blue |
| Solar Irradiance | 4.1.1 | `shaders/sky.frag:73` | `vec3(1.474, 1.8504, 1.91198)` W/m² |

### Partially Implemented

| Feature | Doc Section | Implementation | Issue |
|---------|-------------|----------------|-------|
| Transmittance LUT | 4.1.3 | `src/AtmosphereLUTSystem.cpp`, `shaders/transmittance_lut.comp`, `shaders/sky.frag` | **FIXED** - LUT now sampled via `sampleTransmittanceLUT()` |
| Multi-Scatter LUT | 4.1.4 | `src/AtmosphereLUTSystem.cpp`, `shaders/multiscatter_lut.comp`, `shaders/sky.frag` | **FIXED** - LUT now sampled via `sampleMultiScatterLUT()` |

### Not Implemented

| Feature | Doc Section | Notes |
|---------|-------------|-------|
| Sky-View LUT Runtime Updates | 4.1.5 | LUT exists but not updated per-frame with sun angle changes |
| Irradiance LUTs | 4.1.9 | No separate Rayleigh/Mie irradiance textures for cloud/haze lighting |

### Integration Status: Atmosphere LUTs (FIXED)

The `AtmosphereLUTSystem` (`src/AtmosphereLUTSystem.h`) creates three LUTs at startup and they are now properly integrated:

```cpp
// From Renderer.cpp - LUTs computed at startup
atmosphereLUTSystem.computeTransmittanceLUT(cmdBuffer);
atmosphereLUTSystem.computeMultiScatterLUT(cmdBuffer);
atmosphereLUTSystem.computeSkyViewLUT(cmdBuffer, sunDir, glm::vec3(0.0f), 0.0f);
```

The sky shader now has proper sampler bindings:

```glsl
// shaders/sky.frag bindings
layout(binding = 0) uniform UniformBufferObject { ... };
layout(binding = 1) uniform sampler2D transmittanceLUT;  // 256x64, RGBA16F
layout(binding = 2) uniform sampler2D multiScatterLUT;   // 32x32, RG16F
```

**Implementation details:**
- `sampleTransmittanceLUT(r, mu)` - samples transmittance for altitude r and zenith angle mu
- `sampleMultiScatterLUT(altitude, cosSunZenith)` - samples multi-scatter approximation
- `getTransmittanceToSunLUT(worldPos, sunDir)` - fast path for sun visibility
- `computeTransmittanceToLight()` now uses LUT instead of ray marching

---

## 4.2 Volumetric Clouds

### Fully Implemented

| Feature | Doc Section | Implementation | Notes |
|---------|-------------|----------------|-------|
| Cloud Layer Parameters | 4.2.3 | `shaders/sky.frag:76-81` | Bottom 1.5km, top 4.0km, coverage 0.5, density 0.3 |
| Height Gradient | 4.2.3 | `shaders/sky.frag:119-125` | Cumulus shape - rounded bottom, flat top |
| Depth-Dependent Phase | 4.2.4 | `shaders/sky.frag:194-219` | Ghost of Tsushima technique - g varies with optical depth |
| Cloud Ray Marching | 4.2.5 | `shaders/sky.frag:552-662` | 32 steps with energy-conserving integration |
| Light Transmittance Sampling | 4.2.6 | `shaders/sky.frag:222-241` | 6 samples toward sun with early-out |

### Not Implemented

| Feature | Doc Section | Notes |
|---------|-------------|-------|
| Paraboloid Cloud Map | 4.2.1 | No triple-buffer pre-rendering to hemisphere texture |
| Density Anti-Aliasing | 4.2.2 | No derivative-based density reduction for low-res textures |
| Perlin-Worley Noise | 4.2.3 | Uses FBM value noise instead of proper Perlin-Worley 3D textures |
| Curl Noise | 4.2.3 | No wispy detail distortion texture |
| Temporal Reprojection | 4.2.7 | No cloud history blending or time-sliced updates |

### Current Cloud Rendering Approach

Clouds are ray-marched directly in `sky.frag`:

```glsl
// shaders/sky.frag:552-662
CloudResult marchClouds(vec3 origin, vec3 dir) {
    // 32 steps through cloud layer
    for (int i = 0; i < CLOUD_MARCH_STEPS; i++) {
        float density = sampleCloudDensity(pos);  // FBM noise
        // ... lighting calculations
    }
}
```

This is simpler than the documented paraboloid approach but:
- Cannot amortize cost over multiple frames
- No temporal stability/reprojection
- Full cost every frame regardless of camera motion

---

## 4.3 Volumetric Haze/Fog

### Fully Implemented

| Feature | Doc Section | Implementation | Notes |
|---------|-------------|----------------|-------|
| Froxel Grid | 4.3.1 | `src/FroxelSystem.h:38-43` | 128×64×64, depth distribution 1.2 |
| Exponential Height Density | 4.3.3 | `shaders/froxel_update.comp:40-46` | Configurable base/scale height |
| Sigmoidal Layer Density | 4.3.3 | `shaders/froxel_update.comp:49-55` | Ground fog layer support |
| Froxel Update Compute | 4.3.4 | `shaders/froxel_update.comp` | Density, phase function, scattering |
| Front-to-Back Integration | 4.3.5 | `shaders/froxel_integrate.comp` | Beer-Lambert with early termination |
| L/α Storage | 4.3.6 | `shaders/froxel_integrate.comp:78-79` | Anti-aliased compositing technique |
| Scene Compositing | 4.3.10 | `shaders/postprocess.frag:51-69` | Depth-based froxel sampling |

### Partially Implemented

| Feature | Doc Section | Implementation | Issue |
|---------|-------------|----------------|-------|
| Shadow Map Integration | 4.3.4 | `shaders/froxel_update.comp:133-138` | **Sampler bound but NOT used** - uses sun altitude approximation |
| Temporal Filtering | 4.3.4 | `src/FroxelSystem.h:153` | `prevViewProj` stored but reprojection not implemented |

### Not Implemented

| Feature | Doc Section | Notes |
|---------|-------------|-------|
| Tricubic Filtering | 4.3.7 | Uses trilinear; no 8-tap B-spline implementation |
| Local Light Contribution | 4.3.8 | No point/spot light scattering in froxels |
| Fog Particle Lighting | 4.3.9 | Weather particles don't sample froxel lighting |

### Integration Gap: Froxel Shadows

The froxel update shader has a shadow map sampler bound but doesn't use it:

```glsl
// shaders/froxel_update.comp:23
layout(binding = 3) uniform sampler2DArrayShadow shadowMap;

// Lines 132-138 - simplified shadow:
float sunVisibility = smoothstep(-0.1, 0.2, sunDir.y);
// Full shadow sampling would require cascade matrix transforms
float shadow = sunVisibility;  // SIMPLIFIED - NOT ACTUAL CASCADE SHADOWS
```

**To fix:** Add cascade view-projection matrices to uniforms and implement proper shadow sampling with cascade selection.

---

## 4.4 Light Shafts (God Rays)

### Fully Implemented

| Feature | Doc Section | Implementation | Notes |
|---------|-------------|----------------|-------|
| Screen-Space God Rays | 4.4 | `shaders/postprocess.frag:168-230` | 64-sample radial blur toward sun |

The god ray implementation includes:
- Sun screen position tracking
- Depth-based sky detection (only sky pixels contribute)
- Exponential decay per sample
- Radial falloff from sun position
- Warm color tinting

```glsl
// shaders/postprocess.frag:172-230
vec3 computeGodRays(vec2 uv, vec2 sunPos) {
    // 64 samples along ray toward sun
    for (int i = 0; i < GOD_RAY_SAMPLES; i++) {
        // Only accumulate from sky pixels
        if (sampleDepth >= SKY_DEPTH_THRESHOLD) {
            illumination += brightness * weight;
        }
        weight *= ubo.godRayDecay;
    }
}
```

---

## Additional Implemented Features (Beyond Phase 4 Docs)

These features exist in the codebase but aren't documented in Phase 4:

| Feature | Implementation | Notes |
|---------|----------------|-------|
| Moon Rendering | `shaders/sky.frag:730-788` | Disc with lunar phase simulation |
| Lunar Phase Mask | `shaders/sky.frag:736-788` | Terminator calculation based on phase angle |
| Star Field | `shaders/sky.frag:687-728` | Procedural with sidereal rotation |
| Sidereal Rotation | `shaders/sky.frag:664-685` | Julian day driven, 360.9856°/day |
| Sky Irradiance Sampling | `shaders/sky.frag:467-548` | 6-direction hemisphere integration for cloud lighting |
| Atmospheric Transmittance to Light | `shaders/sky.frag:293-321` | Ray-marched transmittance for cloud lighting |
| Cloud-Atmosphere Compositing | `shaders/sky.frag:886-909` | Proper layering with haze in front of clouds |

---

## File Reference

### Shader Files

| File | Purpose |
|------|---------|
| `shaders/sky.frag` | Main sky rendering with atmosphere, clouds, moon, stars |
| `shaders/sky.vert` | Sky fullscreen quad vertex shader |
| `shaders/froxel_update.comp` | Froxel density and scattering calculation |
| `shaders/froxel_integrate.comp` | Front-to-back scattering integration |
| `shaders/transmittance_lut.comp` | Transmittance LUT generation (unused by sky) |
| `shaders/multiscatter_lut.comp` | Multi-scatter LUT generation (unused by sky) |
| `shaders/skyview_lut.comp` | Sky-view LUT generation (unused) |
| `shaders/postprocess.frag` | Froxel compositing, god rays, bloom, tone mapping |

### C++ Source Files

| File | Purpose |
|------|---------|
| `src/FroxelSystem.h/cpp` | Froxel grid management, compute dispatch |
| `src/AtmosphereLUTSystem.h/cpp` | LUT creation and computation |
| `src/PostProcessSystem.h/cpp` | Post-process pipeline management |
| `src/CelestialCalculator.h/cpp` | Sun/moon position, Julian day, time of day |

---

## Recommended Priorities for Completion

### High Priority (Performance/Quality Impact)

1. **Integrate Atmosphere LUTs into sky.frag**
   - Add sampler bindings for transmittance and multi-scatter LUTs
   - Replace `computeAtmosphericTransmittance()` with LUT lookups
   - Performance benefit: reduce per-pixel ray marching

2. **Implement Froxel Shadow Sampling**
   - Add cascade matrices to `FroxelUniforms`
   - Implement cascade selection in `froxel_update.comp`
   - Quality benefit: proper volumetric shadows through fog

### Medium Priority (Visual Quality)

3. **Add Temporal Reprojection to Froxels**
   - Implement reprojection using `prevViewProj`
   - Blend with history for stability
   - Quality benefit: reduced flickering in fog

4. **Tricubic Filtering for Froxels**
   - Implement 8-tap B-spline sampling
   - Quality benefit: smoother fog gradients

### Lower Priority (Advanced Features)

5. **Paraboloid Cloud Maps** - amortize cloud cost over frames
6. **Local Light Scattering in Fog** - point/spot lights affect fog
7. **Perlin-Worley Noise Textures** - better cloud shapes
8. **Aerial Perspective Volume** - distance-based atmospheric effects on geometry

---

*Last updated: Phase 4 implementation audit*
