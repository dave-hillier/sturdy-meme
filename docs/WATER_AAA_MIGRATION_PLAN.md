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

### Phase 1: FFT Ocean ✅ COMPLETE
**Goal:** Replace Gerstner waves with FFT simulation

Implemented:
- Phillips spectrum generator with wind-driven parameters
- Radix-2 Cooley-Tukey FFT compute pipeline
- 3 cascaded FFT for multi-scale waves (256m, 64m, 16m patches)
- Displacement, normal, and Jacobian-based foam maps
- Toggle between FFT and Gerstner via push constant

---

## Unified Architecture (Phases 2-8 Consolidated)

Analysis revealed that the original 8 phases share significant overlap and can be
unified into 3 coherent systems plus 1 extension:

```
┌─────────────────────────────────────────────────────────────────────┐
│                     WATER VOLUME RENDERER                          │
│  (Unifies: Underwater, Volumetric Caustics, Advanced Refraction)   │
│                                                                     │
│  • Single ray-march system for above/below water                   │
│  • Volumetric light transport with Beer-Lambert                    │
│  • Caustics as part of volume lighting                             │
│  • Camera-agnostic rendering                                       │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                   UNIFIED REFLECTION SYSTEM                         │
│         (Unifies: Planar Reflections with existing SSR)            │
│                                                                     │
│  • SSR → Planar → Environment fallback chain                       │
│  • Per-pixel confidence blending                                   │
│  • Shared temporal filtering                                       │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                  DEPTH-AWARE SHORE SYSTEM                          │
│     (Unifies: Breaking Waves with existing shore foam)             │
│                                                                     │
│  • Wave shoaling from bathymetry                                   │
│  • Breaking triggers foam AND wave deformation                     │
│  • Unified shore interaction model                                 │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                      FLOW NETWORK                                   │
│         (Extends existing flow maps for rivers)                    │
│                                                                     │
│  • Spline-guided flow regions                                      │
│  • Waterfall = vertical flow + splash particles                    │
│  • Rapids/obstacle foam integration                                │
└─────────────────────────────────────────────────────────────────────┘
```

---

### Phase 2: Water Volume Renderer
**Goal:** Unified above/below water rendering with volumetric effects

This single system replaces separate underwater, caustics, and refraction phases.

**Core Architecture:**
```
WaterVolume {
    SDF waterShape;           // Signed distance to water surface
    vec3 absorption;          // Per-channel light absorption (existing)
    float scattering;         // Turbidity/scattering (existing)

    // Render modes determined by camera position relative to SDF
    bool isUnderwater();      // SDF(cameraPos) < 0
}
```

**Implementation Steps:**

1. **Water SDF Generation**
   - Generate SDF from water mesh + FFT displacement
   - Store in 3D texture or compute on-the-fly
   - Enables smooth above/below transitions

2. **Volumetric Ray March** (single pass handles refraction + caustics + fog)
   ```glsl
   // Pseudo-code for unified volume rendering
   vec3 rayMarchWaterVolume(vec3 rayOrigin, vec3 rayDir) {
       float waterEntry = intersectWaterSDF(rayOrigin, rayDir);
       float waterExit = findExitPoint(rayOrigin + rayDir * waterEntry, rayDir);
       float pathLength = waterExit - waterEntry;

       // Beer-Lambert absorption
       vec3 transmission = exp(-absorption * pathLength);

       // Accumulate caustics along path
       vec3 caustics = integrateWaterCaustics(waterEntry, waterExit, rayDir);

       // Refracted scene color
       vec3 refractDir = refract(rayDir, waterNormal, 1.0/1.33);
       vec3 sceneColor = sampleScene(refractDir);

       return sceneColor * transmission + caustics;
   }
   ```

3. **Caustics Integration**
   - Sample FFT normal map to compute caustic intensity
   - Project onto underwater geometry via ray march
   - Single computation serves both surface caustics AND underwater projection

4. **Camera Transition**
   - Snell's window effect when looking up from underwater
   - Total internal reflection at grazing angles
   - Smooth fog transition at surface crossing

**Output:** `WaterVolume.h/cpp`, `water_volume.comp`, `water_volume.frag`

---

### Phase 3: Unified Reflection System
**Goal:** Multi-source reflection with intelligent blending

Extends existing SSR with planar reflections and better fallback.

**Architecture:**
```
ReflectionManager {
    SSRSystem ssr;                    // Existing
    PlanarReflection planar;          // New
    EnvironmentProbe environment;     // Existing (sky)

    // Returns blended reflection based on confidence
    vec3 sampleReflection(vec2 uv, vec3 worldPos, vec3 normal);
}
```

**Implementation Steps:**

1. **Planar Reflection Pass**
   - Render scene with camera mirrored across water plane
   - Use oblique near-plane clipping
   - Half or quarter resolution with temporal upscale

2. **Confidence-Based Blending**
   ```glsl
   vec3 getReflection(vec2 uv, vec3 worldPos, vec3 reflectDir) {
       // SSR has highest priority where valid
       vec4 ssrResult = sampleSSR(uv);  // rgb + confidence

       // Planar fills SSR gaps
       vec4 planarResult = samplePlanar(worldPos);

       // Environment is final fallback
       vec3 envResult = sampleEnvironment(reflectDir);

       // Blend by confidence
       vec3 reflection = envResult;
       reflection = mix(reflection, planarResult.rgb, planarResult.a);
       reflection = mix(reflection, ssrResult.rgb, ssrResult.a);

       return reflection;
   }
   ```

3. **Shared Temporal Filtering**
   - Single temporal buffer for all reflection sources
   - Reduces ghosting and improves stability

**Output:** `ReflectionManager.h/cpp`, `planar_reflection.vert/frag`

---

### Phase 4: Depth-Aware Shore System
**Goal:** Physically-based wave breaking and shore interaction

Unifies breaking waves with existing shore foam system.

**Architecture:**
```
ShoreInteraction {
    sampler2D bathymetry;     // Water depth map (existing terrain height)

    // Wave modification based on depth
    WaveState computeShoreWave(vec2 worldPos, float baseWaveHeight);

    // Breaking detection and foam injection
    BreakInfo detectBreaking(WaveState wave, float depth);
}
```

**Implementation Steps:**

1. **Wave Shoaling**
   - Waves slow down and grow taller as depth decreases
   - Modify FFT displacement based on local depth:
   ```glsl
   float shoalingFactor = sqrt(deepWaterDepth / localDepth);
   waveHeight *= shoalingFactor;
   waveLength /= shoalingFactor;
   ```

2. **Breaking Detection**
   - Wave breaks when: `waveHeight > 0.78 * waterDepth`
   - Track breaking state per-vertex for consistent foam

3. **Unified Breaking Response**
   - Foam injection (connects to existing Jacobian foam)
   - Wave crest deformation (forward lean before break)
   - Spray particle emission at break point

4. **Integration with Existing Shore Foam**
   - Breaking foam feeds into `temporalFoamMap`
   - Shore proximity foam already in place
   - Unified foam buffer handles all sources

**Output:** Modifications to `water.vert`, `shore_interaction.glsl`

---

### Phase 5: Flow Network Extension
**Goal:** Rivers and waterfalls as flow map extensions

Builds on existing flow map system rather than separate river rendering.

**Architecture:**
```
FlowNetwork {
    // Existing
    sampler2D globalFlowMap;

    // Extensions
    RiverSpline[] rivers;           // Spline-defined flow corridors
    WaterfallRegion[] waterfalls;   // Vertical flow transitions
}
```

**Implementation Steps:**

1. **River Flow Injection**
   - Rivers defined as splines with width profile
   - Generate flow direction/speed along spline
   - Blend into global flow map

2. **Waterfall Handling**
   - Vertical flow = particle system (not mesh deformation)
   - Splash pool at base injects into foam buffer
   - Mist particles for spray effect

3. **Rapids/Obstacle Foam**
   - Detect high flow speed + obstacle proximity
   - Already partially implemented in `foam.glsl`
   - Extend with more aggressive foam at rapids

**Output:** `FlowNetwork.h/cpp`, modifications to `FlowMapGenerator`

---

## Revised Priority Matrix

| Phase | Description | Impact | Complexity | Priority |
|-------|-------------|--------|------------|----------|
| 2 | Water Volume Renderer | ★★★★★ | High | **P0** |
| 3 | Unified Reflection | ★★★★☆ | Medium | **P1** |
| 4 | Shore System | ★★★☆☆ | Medium | **P1** |
| 5 | Flow Network | ★★☆☆☆ | Low | **P2** |

**Key Insight:** The Water Volume Renderer (Phase 2) is now the highest priority because it:
- Provides underwater rendering (high player impact)
- Includes volumetric caustics (visual quality)
- Handles advanced refraction (realism)
- All in ONE coherent system instead of THREE separate features

---

## Key References

- **FFT Ocean:** "Simulating Ocean Water" (Tessendorf, 2001)
- **Planar Reflections:** Common technique, see Unity/Unreal implementations
- **Underwater:** Sea of Thieves GDC 2018, Subnautica postmortems
- **Breaking Waves:** "Real-time Breaking Waves" (SIGGRAPH 2012)
- **Caustics:** "Water Caustics" (GPU Gems Chapter 2)
