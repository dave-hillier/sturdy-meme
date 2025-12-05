# Advanced Water Rendering Plan

Based on:
- GDC 2018: "Water Rendering in Far Cry 5" - Branislav Grujic, Christian Coto Charis
- GDC 2018: "The Technical Art of Sea of Thieves" - Rare

## Overview

This plan implements screen-space water rendering with flow maps, procedural foam, PBR lighting, and screen-space tessellation.

## Current Status

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Flow Maps | ✅ Implemented |
| 2 | Enhanced Foam | ✅ Implemented (texture-based) |
| 3 | Mini G-Buffer | ✅ Implemented |
| 4 | Vector Displacement | ✅ Implemented (fallback mode) |
| 5 | FBM Surface Detail | ✅ Implemented |
| 6 | Variance Specular Filtering | ✅ Implemented |
| 7 | Screen-Space Tessellation | ❌ Not started |
| 8 | PBR Light Transport | ✅ Implemented |
| 9 | Refraction & Caustics | ⚠️ Partial (refraction only) |
| 10 | Screen-Space Reflections | ❌ Not started |
| 11 | Dual Depth Buffer | ❌ Not started |
| 12 | Material Blending | ❌ Not started |
| 13 | Jacobian Foam (NEW) | ❌ Not started |
| 14 | Temporal Foam Persistence (NEW) | ❌ Not started |
| 15 | Intersection Foam (NEW) | ❌ Not started |
| 16 | Wake/Trail System (NEW) | ❌ Not started |

---

## Phase 1: Flow Map System

**Goal:** Replace static UV scrolling with proper flow-based water movement

### 1.1 Flow Map Generation (Offline/Bake Tool)
- Implement flood-fill algorithm guided by signed distance fields
- Generate flow direction texture from terrain slopes and river paths
- Create world-space flow atlas (low-res for distant water, high-res streamable for nearby)
- Store: flow direction (RG), flow speed (B), signed distance to shore (A)

### 1.2 Flow Map Sampling in Shader
- Implement two-phase offset sampling to eliminate pulsing artifacts
- Add flow-based UV animation in `water.frag`
- Modulate foam generation by flow speed

**Files:**
- New: `src/FlowMapGenerator.h/cpp`
- New: `shaders/flow_common.glsl`
- Modify: `shaders/water.frag`
- Modify: `src/WaterSystem.h/cpp`

---

## Phase 2: Enhanced Foam System

**Goal:** Physically-based foam following flow and shore contours

### 2.1 Signed Distance Field for Shores/Rocks
- Generate SDF from terrain heightmap at water level
- Store distance to nearest shore/rock per texel
- Use jump flooding algorithm for GPU-accelerated SDF generation

### 2.2 Flow-Aware Foam
- Foam intensity driven by: flow speed, shore proximity, wave peaks
- Noise-modulated foam texture sampling along flow direction
- Smooth foam transitions between water bodies

**Files:**
- New: `src/SDFGenerator.h/cpp`
- New: `shaders/foam.glsl`
- Modify: `shaders/water.frag`

---

## Phase 3: Screen-Space Mini G-Buffer

**Goal:** Per-pixel water data for deferred water compositing

### 3.1 Water Data Textures
- Data texture: shader ID (8-bit), material index, LOD level, foam amount
- Mesh normal texture: store low-res mesh normal
- Water-only depth buffer (separate from scene depth)

### 3.2 Position Pass
- Render water meshes to mini G-buffer at reduced resolution
- Store per-pixel material properties for later lighting
- Pack texture array indices for material lookups

**Files:**
- New: `src/WaterGBuffer.h/cpp`
- New: `shaders/water_position.vert/frag`

---

## Phase 4: Vector Displacement Maps (Interactive Splashes)

**Goal:** Dynamic water displacement from objects/particles

### 4.1 Displacement Particle System
- Render projected box decals for each splash particle
- Sample water depth buffer to project onto water surface
- Animated displacement textures (multi-frame)
- Edge fading to prevent displacement explosion at box edges

### 4.2 Displacement Blending
- Max alpha blend: clear texture to -FLT_MAX, blend with max
- Combine splashes with FBM surface noise

**Files:**
- New: `src/WaterDisplacement.h/cpp`
- New: `shaders/water_displacement.comp`
- Modify: `shaders/water.vert`

---

## Phase 5: Fractional Brownian Motion (FBM) Surface Detail

**Goal:** High-quality procedural surface normals with LOD

### 5.1 FBM Implementation
- 9 iterations close to camera (high frequency detail)
- 3 iterations in distance (preserve reflection quality)
- Smooth LOD transition based on depth/distance

### 5.2 Screen-Space Normal Generation
- Generate normals from displaced positions using position-from-depth
- Increase sampling radius near camera to reduce artifacts
- Blend with mesh normal to smooth discontinuities

**Files:**
- New: `shaders/fbm_common.glsl`
- Modify: `shaders/water.frag`

---

## Phase 6: Variance-Based Specular Filtering

**Goal:** Eliminate specular aliasing

### 6.1 Gaussian Normal Filtering
- Compute per-pixel gaussian normal
- Solve for smoothness based on variance
- Scale back using per-pixel material properties

**Files:**
- Modify: `shaders/water.frag`

---

## Phase 7: Screen-Space Tessellation

**Goal:** Constant-density tessellation without artist-provided high-res meshes

### 7.1 Tile-Based Visibility
- Divide screen into 32x32 tiles
- Compute pass: count water pixels per tile (atomic adds)
- Build indirect draw arguments from visible tiles
- Write tile IDs to structured buffer

### 7.2 Per-Tile Mesh Rendering
- 16x16 quads per tile (512/32 = 16)
- Vertex shader: decode tile ID → UV → sample depth → world position
- Sample displacement texture (FBM + splashes)
- Clip vertices outside water using NaN
- Project with regular FOV after larger FOV tessellation

**Files:**
- New: `src/WaterTessellation.h/cpp`
- New: `shaders/water_tile_cull.comp`
- New: `shaders/water_tessellated.vert/frag`

---

## Phase 8: PBR Light Transport

**Goal:** Physically-based water lighting with scattering coefficients

### 8.1 Scattering Coefficient System
- Replace artist color picker with RGB scattering coefficients + turbidity
- Water types table: ocean, muddy river, clear stream, sulfur pond
- Calculate absorption/scattering based on physical properties

### 8.2 Full Tiled Deferred Lighting
- Support directional, point, spot lights on water surface
- GI reflections (environment probe sampling)
- Light transport based on depth (refraction path length)

**Files:**
- Modify: `src/WaterSystem.h`
- Modify: `shaders/water.frag`

---

## Phase 9: Refraction & Caustics

**Goal:** Underwater distortion and light patterns

### 9.1 Refraction
- Sample scene color behind water with UV offset based on water normal
- Depth-based blending (shallow = more refraction visible)

### 9.2 Fractioned Caustics
- Animated caustics texture projected onto underwater surfaces
- Modulate by water depth and light angle

**Files:**
- Modify: `shaders/water.frag`
- New: texture asset for caustics pattern

---

## Phase 10: Screen-Space Reflections

**Goal:** High-quality dynamic reflections

### 10.1 SSR Ray Marching
- Ray march in screen space along reflection direction
- Hierarchical tracing for performance
- Fallback to environment map where SSR fails

### 10.2 Reflection Compositing
- Blend SSR with low-res environment map (64x64)
- Fresnel-weighted reflection intensity

**Files:**
- New: `shaders/water_ssr.comp`
- Modify: `shaders/water.frag`

---

## Phase 11: Dual Depth Buffer System

**Goal:** Proper depth handling for water-scene interaction

### 11.1 Depth Buffer Management
- Maintain depth buffer without water (for refraction sampling)
- Maintain depth buffer with water (for scene compositing)
- Handle stencil propagation between buffers

**Files:**
- Modify: `src/Renderer.cpp`

---

## Phase 12: Material Blending System

**Goal:** Smooth transitions between different water bodies

### 12.1 Per-Pixel Material Interpolation
- Store blend material ID per pixel
- Interpolate specific material properties (not all)
- Artist-controllable blend distance (3m, 6m, 12m)

**Files:**
- Modify: `src/WaterSystem.h/cpp`
- Modify: `shaders/water.frag`

---

## Performance Budget Target

Based on Far Cry 5's numbers:
- **Total budget:** ~2ms GPU
- Position pass: ~0.1ms
- Tile culling: ~0.05ms
- Tessellation: ~0.22ms
- FBM/Displacement: ~0.15ms
- Composite/Lighting: ~0.87ms (most expensive)
- Potential async compute savings: ~30%

---

## Implementation Order

1. **Phase 1** (Flow Maps) - Biggest visual improvement
2. **Phase 2** (Enhanced Foam) - Works with flow maps
3. **Phase 5** (FBM Detail) - Better surface quality
4. **Phase 6** (Specular Filtering) - Fixes aliasing
5. **Phase 8** (PBR Lighting) - Physical accuracy
6. **Phase 9** (Refraction) - Underwater visibility
7. **Phase 3** (Mini G-Buffer) - Setup for advanced features
8. **Phase 4** (Displacement) - Interactivity
9. **Phase 7** (Screen-Space Tessellation) - Performance/quality
10. **Phase 10** (SSR) - Polish
11. **Phase 11** (Dual Depth) - Correctness
12. **Phase 12** (Material Blending) - Multi-water-body support

---

## Phase 13: Jacobian-Based Foam (Sea of Thieves)

**Goal:** Physically-accurate foam from wave folding/overlap

Sea of Thieves generates foam where wave peaks overlap by computing the Jacobian determinant of the wave displacement field.

### 13.1 Jacobian Calculation
- Compute Jacobian determinant of Gerstner wave displacement
- Foam appears where Jacobian goes negative (wave folding)
- Bias threshold to control foam amount (more bias = stormier)

### 13.2 Implementation
```glsl
// In vertex shader or compute: calculate wave displacement Jacobian
// J = 1 - steepness * cos(phase) for each wave, multiply together
float jacobian = 1.0;
for each wave:
    jacobian *= (1.0 - steepness * cos(k * x - omega * t));

// Negative jacobian = wave folding = foam
float foamMask = smoothstep(foamBias, 0.0, jacobian);
```

### 13.3 Storm Mode
- Increase wave amplitude
- Bias Jacobian threshold further negative for more foam coverage
- Add foam "bias" uniform for runtime control

**Files:**
- Modify: `shaders/water.vert` (compute Jacobian per-vertex)
- Modify: `shaders/water.frag` (use Jacobian foam mask)
- Modify: `src/WaterSystem.h` (add foam bias parameter)

---

## Phase 14: Temporal Foam Persistence (Sea of Thieves)

**Goal:** Foam that persists and dissipates over time

Sea of Thieves progressively blurs the foam render target frame-by-frame, creating foam that lingers at wave crests then gradually fades.

### 14.1 Foam Render Target
- Render foam intensity to separate R16F texture
- Tile across water surface (512x512 sufficient)
- Inject foam where Jacobian indicates wave peaks

### 14.2 Progressive Blur
- Each frame: blur the foam texture slightly
- Blend new foam in additively at wave peaks
- Result: sharp foam at crests, gradual dissipation

### 14.3 Multi-Scale Foam Textures
- High-frequency foam texture at wave crests
- Low-frequency foam texture as foam dissipates
- Blend between them based on foam age/intensity

**Files:**
- New: `src/FoamBuffer.h/cpp`
- New: `shaders/foam_blur.comp`
- Modify: `shaders/water.frag`

---

## Phase 15: Intersection Foam System (Sea of Thieves)

**Goal:** Real-time foam generation where water meets geometry

Sea of Thieves generates foam by comparing water mesh depth against scene depth in a separate render pass.

### 15.1 Intersection Detection Pass
- Render water geometry to separate render target
- Each pixel: compare water depth vs scene depth
- If depths are close → intersection → write foam
- UV-unwrap approach allows per-mesh foam masks

### 15.2 Foam Advection
- Advect intersection foam using flow map
- Progressive blur over time (same as Phase 14)
- Foam flows away from intersection points

### 15.3 Applications
- Foam around islands/rocks
- Foam where boats touch water
- Waterfall spray occlusion

**Files:**
- New: `src/IntersectionFoam.h/cpp`
- New: `shaders/intersection_foam.frag`
- Modify: `shaders/water.frag`

---

## Phase 16: Wake/Trail System

**Goal:** Persistent foam trails behind moving objects

### 16.1 Wake Injection
- Track moving objects in water (boats, characters, projectiles)
- Inject foam into foam buffer at object positions
- Shape based on object velocity and size

### 16.2 Wake Persistence
- Uses same foam buffer as Phase 14
- Advect with flow, blur over time
- Foam trails naturally dissipate

### 16.3 Bow Waves
- Special case for boats: directional wake pattern
- V-shaped foam at bow based on velocity
- Kelvin wake pattern for realistic appearance

**Files:**
- Modify: `src/FoamBuffer.h/cpp`
- Modify: `shaders/foam_blur.comp` (add injection points)

---

## Phase 17: Enhanced Subsurface Scattering (Sea of Thieves)

**Goal:** Light transmission through thin wave peaks

Sea of Thieves uses wave "choppiness" (displacement amplitude) to mask where SSS should appear.

### 17.1 SSS Mask from Wave Geometry
- Calculate wave slope/steepness per-vertex
- Steep slopes = thin water = more light transmission
- Use dot(lightDir, viewDir) for SSS contribution

### 17.2 Integration
```glsl
float sssStrength = waveSlope * max(0.0, dot(lightDir, -viewDir));
vec3 sssColor = waterColor * sunColor * sssStrength * sssIntensity;
```

**Files:**
- Modify: `shaders/water.frag`

---

## Recommended Implementation Order (Updated)

### High Impact / Lower Complexity (Do First):
1. **Phase 13** (Jacobian Foam) - Much better foam placement than height threshold
2. **Phase 17** (Enhanced SSS) - Quick win for visual quality
3. **Phase 14** (Temporal Foam) - Foam that looks alive, not static

### Medium Impact:
4. **Phase 9** (Caustics) - Complete the partial implementation
5. **Phase 15** (Intersection Foam) - Foam around geometry
6. **Phase 16** (Wake System) - Interactivity

### Performance/Polish:
7. **Phase 7** (Screen-Space Tessellation) - Performance optimization
8. **Phase 10** (SSR) - High-quality reflections
9. **Phase 11** (Dual Depth) - Correctness for complex scenes
10. **Phase 12** (Material Blending) - Multiple water types

---

## Next Steps for Quality Improvement

Based on current state, the highest-impact improvements are:

### 1. Jacobian Foam (Phase 13)
**Why:** Current foam uses height threshold which doesn't capture wave physics. Jacobian foam appears where waves actually fold/overlap, which is physically correct and looks much better.

### 2. Temporal Foam Persistence (Phase 14)
**Why:** Current foam is instantaneous - appears and disappears each frame. Persistent foam that fades over time looks more realistic and creates natural foam trails.

### 3. SSS Enhancement (Phase 17)
**Why:** Quick implementation, big visual impact. Light glowing through wave peaks adds life to the water.

### 4. Foam Texture Quality
**Why:** The generated Worley noise texture may need tuning. Consider:
- Higher resolution (1024x1024)
- More octaves for finer detail
- Different noise parameters

---

## Performance Budget Target

Based on Far Cry 5's numbers:
- **Total budget:** ~2ms GPU
- Position pass: ~0.1ms
- Tile culling: ~0.05ms
- Tessellation: ~0.22ms
- FBM/Displacement: ~0.15ms
- Composite/Lighting: ~0.87ms (most expensive)
- Potential async compute savings: ~30%

Sea of Thieves notes:
- Water rendering: ~8ms when full screen (acceptable as nothing else renders)
- Intersection foam: <0.1ms per waterfall
- Deck water sim: ~0.2ms per ship

---

## References

- GDC 2018: "Water Rendering in Far Cry 5" - Branislav Grujic, Christian Coto Charis
- GDC 2018: "The Technical Art of Sea of Thieves" - Rare
- Tessendorf 2001: "Simulating Ocean Water" (Jacobian foam)
- GPU Gems: Flow Map techniques
- Vlachos 2010: "Water Flow in Portal 2"
