# Phase 4: Atmospheric Scattering - Implementation Status

This document tracks the implementation status of features described in [LIGHTING_PHASE4_ATMOSPHERE.md](LIGHTING_PHASE4_ATMOSPHERE.md).

---

## Summary

| Category | Implemented | Partially Implemented | Missing |
|----------|-------------|----------------------|---------|
| Sky Model (4.1) | 11 | 0 | 1 |
| Volumetric Clouds (4.2) | 5 | 0 | 5 |
| Volumetric Haze/Fog (4.3) | 13 | 0 | 0 |
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

| Transmittance LUT | 4.1.3 | `src/AtmosphereLUTSystem.cpp`, `shaders/transmittance_lut.comp`, `shaders/sky.frag` | LUT sampled via `sampleTransmittanceLUT()` |
| Multi-Scatter LUT | 4.1.4 | `src/AtmosphereLUTSystem.cpp`, `shaders/multiscatter_lut.comp`, `shaders/sky.frag` | LUT sampled via `sampleMultiScatterLUT()` |

| Sky-View LUT Runtime Updates | 4.1.5 | `src/Renderer.cpp:2408-2410`, `shaders/sky.frag:96-122` | LUT updated per-frame with current sun direction |

### Not Implemented

| Feature | Doc Section | Notes |
|---------|-------------|-------|
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
| Temporal Reprojection | 4.2.7 | `src/CloudTemporalSystem.cpp` | Double-buffered cloud render with history blending |

### Not Implemented

| Feature | Doc Section | Notes |
|---------|-------------|-------|
| Paraboloid Cloud Map | 4.2.1 | No triple-buffer pre-rendering to hemisphere texture |
| Density Anti-Aliasing | 4.2.2 | No derivative-based density reduction for low-res textures |
| Perlin-Worley Noise | 4.2.3 | Uses FBM value noise instead of proper Perlin-Worley 3D textures |
| Curl Noise | 4.2.3 | No wispy detail distortion texture |

### Current Cloud Rendering Approach

Two rendering modes available, controlled by `Renderer::toggleCloudTemporal()`:

**Temporal Mode (default, cloudTemporal=1.0):**
- Pre-renders clouds to 512x512 paraboloid buffer in compute shader
- Double-buffered ping-pong for temporal blending (90% history, 10% current)
- Wind-based motion vector reprojection
- Transmittance-based rejection for disoccluded regions
- Reduces flickering significantly, amortizes cost over frames

**Direct Mode (cloudTemporal=0.0):**
- Ray marches clouds directly in `sky.frag` per pixel
- 32 steps through cloud layer with FBM noise
- Full cost every frame, no temporal stability
- Useful for comparison/debugging

```glsl
// shaders/sky.frag - conditional temporal sampling
CloudResult clouds;
if (ubo.cloudTemporal > 0.5) {
    clouds = sampleTemporalClouds(normDir);  // From pre-rendered buffer
} else {
    clouds = marchClouds(origin, normDir);   // Full ray march
}
```

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

| Shadow Map Integration | 4.3.4 | `shaders/froxel_update.comp:192-247` | Full cascade shadow sampling with PCF |
| Temporal Filtering | 4.3.4 | `shaders/froxel_update.comp:110-189, 396-430` | Reprojection with adaptive blend and ghosting rejection |
| Tricubic Filtering | 4.3.7 | `shaders/postprocess.frag:45-127` | 8-tap B-spline optimization |
| Local Light Contribution | 4.3.8 | `shaders/froxel_update.comp:249-317` | Point/spot lights with attenuation and phase function |
| Fog Particle Lighting | 4.3.9 | `shaders/weather.frag:51-78` | Weather particles sample froxel volume for fog lighting |

### Integration Status: Froxel Features

**Cascade Shadow Sampling:**

```glsl
// shaders/froxel_update.comp - cascade matrices in UBO
mat4 cascadeViewProj[NUM_CASCADES];  // Light-space matrices
vec4 cascadeSplits;                   // View-space split depths

// Shadow sampling functions:
// - selectCascade(viewSpaceDepth) - selects appropriate cascade
// - sampleShadowPCF(worldPos, cascade) - 2x2 PCF sampling
// - sampleCascadeShadow(worldPos, viewSpaceDepth) - full pipeline
```

**Local Light Contribution (4.3.8):**

```glsl
// shaders/froxel_update.comp - Light buffer SSBO
struct GPULight {
    vec4 positionAndType;    // xyz = position, w = type (0=point, 1=spot)
    vec4 directionAndCone;   // xyz = direction (for spot), w = outer cone angle
    vec4 colorAndIntensity;  // rgb = color, a = intensity
    vec4 radiusAndInnerCone; // x = radius, y = inner cone angle
};

// computeLocalLightScatter() iterates all lights:
// - Point light smooth quadratic attenuation
// - Spot light cone falloff
// - Henyey-Greenstein phase function per light
```

**Fog Particle Lighting (4.3.9):**

```glsl
// shaders/weather.frag - samples froxel volume
vec3 sampleFroxelFogLighting(vec3 worldPos) {
    // Convert world pos to froxel UVW coordinates
    // Sample froxelVolume texture
    // Returns in-scattered fog light at position
}

// Rain/snow particles add fog lighting contribution:
color += fogLight * 0.5;  // Rain scatters more
color += fogLight * 0.3;  // Snow scatters diffusely
```

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

1. **Paraboloid Cloud Maps** (4.2.1)
   - Pre-render clouds to hemisphere texture
   - Triple-buffer for temporal stability
   - Performance benefit: amortize cloud cost over frames

2. **Cloud Temporal Reprojection** (4.2.7)
   - History blending for cloud stability
   - Time-sliced updates to reduce per-frame cost
   - Quality benefit: reduced flickering in clouds

### Medium Priority (Visual Quality)

1. **Perlin-Worley Noise Textures** (4.2.3)
   - Replace FBM value noise with proper 3D noise textures
   - Quality benefit: more realistic cloud shapes

2. **Density Anti-Aliasing** (4.2.2)
   - Derivative-based density reduction for low-res textures
   - Quality benefit: reduced aliasing artifacts

### Lower Priority (Advanced Features)

1. **Curl Noise** (4.2.3) - wispy detail distortion for clouds
2. **Irradiance LUTs** (4.1.9) - separate Rayleigh/Mie irradiance textures
