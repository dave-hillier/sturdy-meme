# Advanced Water Rendering Plan

Based on Far Cry 5's GDC talk "Water Rendering in Far Cry 5" by Branislav Grujic and Christian Coto Charis.

## Overview

This plan implements screen-space water rendering with flow maps, procedural foam, PBR lighting, and screen-space tessellation.

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

## References

- GDC 2018: "Water Rendering in Far Cry 5" - Branislav Grujic, Christian Coto Charis
- GPU Gems: Flow Map techniques
- Vlachos 2010: "Water Flow in Portal 2"
