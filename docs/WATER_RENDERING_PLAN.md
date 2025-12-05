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
| 9 | Refraction & Caustics | ✅ Implemented |
| 10 | Screen-Space Reflections | ❌ Not started |
| 11 | Dual Depth Buffer | ❌ Not started |
| 12 | Material Blending | ❌ Not started |
| 13 | Jacobian Foam | ✅ Implemented |
| 14 | Temporal Foam Persistence | ✅ Implemented |
| 15 | Intersection Foam | ✅ Implemented |
| 16 | Wake/Trail System | ✅ Implemented |
| 17 | Enhanced SSS | ✅ Implemented |

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

## Phase 2: Enhanced Foam System ✅ (Superseded by Phase 13/14)

**Status:** Basic implementation complete. Texture-based foam with flow animation.

**Note:** This phase provided initial foam functionality. For higher quality, implement:
- **Phase 13** (Jacobian Foam) for physically-accurate wave crest foam
- **Phase 14** (Temporal Persistence) for foam that lingers and fades

### 2.1 Current Implementation
- ✅ Tileable Worley noise texture (512x512, generated at build time)
- ✅ Multi-scale texture sampling (3 scales with flow animation)
- ✅ Shore foam based on water depth
- ✅ Flow-speed modulated foam intensity

### 2.2 Remaining (Low Priority)
- SDF generation via jump flooding (currently using depth-based shore detection)

**Files:**
- ✅ `tools/foam_noise_gen.cpp` - Texture generator
- ✅ `shaders/foam.glsl` - Foam utilities
- ✅ `shaders/water.frag` - Texture-based foam sampling

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

## Phase 9: Refraction & Caustics ✅

**Status:** Implemented. Animated caustics with depth and sun angle modulation.

**Goal:** Underwater distortion and light patterns

### 9.1 Refraction
- ✅ Water color blending based on Fresnel (reflection vs refraction)
- ✅ Depth-based absorption using Beer-Lambert law

### 9.2 Animated Caustics
- ✅ Tileable caustics texture generated at build time
- ✅ Two-layer animation for shimmering effect
- ✅ Depth falloff (strongest in shallow water, fades by 15m)
- ✅ Sun angle modulation (stronger with overhead sun)
- ✅ Turbidity reduction (particles scatter caustic light)
- ✅ Uniforms: `causticsScale`, `causticsSpeed`, `causticsIntensity`

### 9.3 Implementation
```glsl
// Two-layer caustics for richer animation
vec2 causticsUV1 = fragWorldPos.xz * causticsScale + time * causticsSpeed;
vec2 causticsUV2 = fragWorldPos.xz * causticsScale * 1.5 - time * causticsSpeed;
float causticPattern = texture(causticsTexture, causticsUV1).r *
                       texture(causticsTexture, causticsUV2).r;
```

**Files:**
- ✅ `tools/caustics_gen.cpp` - Procedural caustics texture generator
- ✅ `shaders/water.frag` - Caustics sampling and modulation
- ✅ `src/WaterSystem.h/cpp` - Caustics texture and uniforms

---

## Phase 10: Screen-Space Reflections ✅

**Status:** Implemented. Compute-based SSR with temporal stability.

**Goal:** High-quality dynamic reflections

### 10.1 SSR Ray Marching
- ✅ Linear ray marching in screen space along reflection direction
- ✅ Binary search refinement for precise hit detection
- ✅ Adaptive step size (increases with distance)
- ✅ Fallback to environment map where SSR fails

### 10.2 SSR Features
- ✅ Half-resolution rendering for performance
- ✅ Temporal filtering with previous frame
- ✅ Distance-based confidence falloff
- ✅ Edge fading near screen borders
- ✅ Angle-based confidence (grazing angles preferred)

### 10.3 Implementation Details
```glsl
// SSR samples from previous frame for temporal stability
vec4 ssrSample = texture(ssrTexture, screenUV);
float ssrConfidence = ssrSample.a;
// Blend SSR with environment based on confidence
vec3 finalReflection = mix(envReflection, ssrSample.rgb, ssrConfidence);
```

**Files:**
- ✅ `src/SSRSystem.h/cpp` - SSR compute system
- ✅ `shaders/ssr.comp` - SSR compute shader
- ✅ `shaders/water.frag` - SSR sampling with environment fallback

---

## Phase 11: Dual Depth Buffer System ✅

**Status:** Implemented. Scene depth available for intersection detection and soft edges.

**Goal:** Proper depth handling for water-scene interaction

### 11.1 Scene Depth Access
- ✅ Scene depth texture (HDR depth) bound to water shader (binding 10)
- ✅ Depth linearization for proper distance calculation
- ✅ Scene depth sampling for intersection detection

### 11.2 Intersection Detection
- ✅ Soft edge calculation at water-geometry intersection
- ✅ Foam generation at any geometry intersection (not just terrain)
- ✅ Depth difference-based soft particle effect

### 11.3 Implementation Details
```glsl
// Get scene depth and water depth
float sceneLinearDepth = getSceneDepth(screenUV, near, far);
float depthDiff = sceneLinearDepth - waterLinearDepth;

// Soft edge factor: 0 at intersection, 1 away from geometry
float softEdge = smoothstep(0.0, softEdgeDist, depthDiff);

// Add foam at intersections
float geometryFoam = (1.0 - softEdge) * foamNoise;
```

**Files:**
- ✅ `src/WaterSystem.h/cpp` - Scene depth texture binding
- ✅ `shaders/water.frag` - Depth-based intersection foam
- ✅ `src/Renderer.cpp` - Pass HDR depth to water system

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

## Phase 15: Intersection Foam System (Sea of Thieves) ✅

**Status:** Implemented using terrain depth data and flow advection.

**Goal:** Real-time foam generation where water meets geometry

### 15.1 Intersection Detection (Simplified Approach)
- ✅ Uses existing terrain heightmap for depth comparison
- ✅ Wave-modulated intersection detection (waves push/pull waterline)
- ✅ Intersection strength based on water depth proximity

### 15.2 Foam Advection
- ✅ Flow-advected foam UVs - foam appears at intersection and flows away
- ✅ Combined with temporal foam buffer (Phase 14) for persistence
- ✅ Dynamic noise blending for organic appearance

### 15.3 Implementation Features
- ✅ Wave-influenced effective depth for dynamic waterline
- ✅ Multiple foam bands with different characteristics
- ✅ Intersection foam boost near geometry (< 0.5m water depth)
- ✅ Splash effect at shoreline based on wave height
- ✅ Enhanced flow-based foam in fast-flowing areas

### 15.4 Code
```glsl
// Wave-modulated intersection detection
float waveInfluence = sin(fragWaveHeight * 10.0 + time * 2.0) * 0.3 + 0.7;
float effectiveDepth = waterDepth / waveInfluence;

// Flow-advected foam
vec2 advectedUV = fragWorldPos.xz * 0.1 + flowSample.flowDir * time * 0.3;
float advectedNoise = texture(foamNoiseTexture, advectedUV * 0.5).r;

// Intersection boost near geometry
float intersectionStrength = smoothstep(0.5, 0.0, waterDepth);
```

**Files:**
- ✅ `shaders/water.frag` - Enhanced intersection foam in shore foam section

**Note:** Full Sea of Thieves approach (separate depth comparison pass) not implemented. Current implementation uses terrain depth data which works well for shorelines. For dynamic objects (boats), Phase 16 (Wake System) would be needed.

---

## Phase 16: Wake/Trail System ✅

**Status:** Implemented. V-shaped wakes with Kelvin angle and bow waves.

**Goal:** Persistent foam trails behind moving objects

### 16.1 Wake Injection
- ✅ `addWakeSource(position, velocity, radius, intensity)` API
- ✅ `addWake(position, radius, intensity)` for simple splashes
- ✅ Up to 16 wake sources per frame
- ✅ Wake data passed via uniform buffer to compute shader

### 16.2 Wake Persistence
- ✅ Uses same foam buffer as Phase 14 (ping-pong)
- ✅ Advect with flow, blur over time
- ✅ Foam trails naturally dissipate with decay rate

### 16.3 Wake Patterns
- ✅ Circular wake for stationary/slow objects
- ✅ V-shaped wake behind moving objects
- ✅ Kelvin wake angle (19.47°) for realistic spread
- ✅ Bow wave at front of fast-moving objects
- ✅ Speed-based intensity scaling

### 16.4 Implementation
```glsl
// V-shaped wake calculation
float behind = -dot(toPos, moveDir);
float wakeWidth = behind * tan(wake.wakeAngle);
float wakeFalloff = smoothstep(wakeWidth + radius, wakeWidth * 0.5, perpDist);

// Bow wave
float bowWave = smoothstep(radius * 1.2, radius * 0.3, bowDist) * speedBoost;
```

### 16.5 Usage
```cpp
// From game code, each frame:
foamBuffer.addWakeSource(
    glm::vec2(boat.x, boat.z),  // Position
    glm::vec2(boat.vx, boat.vz), // Velocity
    boatRadius,                   // Hull radius
    1.0f                          // Intensity
);
```

**Files:**
- ✅ `src/FoamBuffer.h/cpp` - Wake source API and uniform buffer
- ✅ `shaders/foam_blur.comp` - Wake pattern calculation and injection

---

## Phase 17: Enhanced Subsurface Scattering (Sea of Thieves) ✅

**Status:** Implemented. Wave geometry-based SSS with back-lighting effect.

**Goal:** Light transmission through thin wave peaks

Sea of Thieves uses wave "choppiness" (displacement amplitude) to mask where SSS should appear.

### 17.1 SSS Mask from Wave Geometry
- ✅ Calculate wave slope/steepness per-vertex (1 - abs(normal.y))
- ✅ Steep slopes = thin water = more light transmission
- ✅ Use dot(sunDir, -viewDir) for back-lighting contribution

### 17.2 Implementation
- ✅ Wave slope passed from vertex shader as `fragWaveSlope`
- ✅ Back-lighting factor from sun direction relative to view
- ✅ Height-based boost at wave peaks
- ✅ Shallow water enhancement for visibility
- ✅ Turbidity reduction (particles scatter light)
- ✅ `sssIntensity` uniform for artist control

### 17.3 SSS Calculation
```glsl
float backLighting = max(0.0, dot(sunDir, -V));
float thinWaterFactor = max(waveSlope, heightFactor * 0.5);
float sssStrength = thinWaterFactor * backLighting * backLighting;
vec3 waveSSS = sssTint * sunColor * sssStrength * sssIntensity;
```

**Files:**
- ✅ `shaders/water.vert` - Wave slope output
- ✅ `shaders/water.frag` - Enhanced SSS calculation
- ✅ `src/WaterSystem.h/cpp` - sssIntensity uniform

---

## Recommended Implementation Order (Updated)

### High Impact / Lower Complexity (Do First):
1. ✅ **Phase 13** (Jacobian Foam) - Much better foam placement than height threshold
2. ✅ **Phase 17** (Enhanced SSS) - Quick win for visual quality
3. ✅ **Phase 14** (Temporal Foam) - Foam that looks alive, not static

### Medium Impact:
4. ✅ **Phase 9** (Caustics) - Animated underwater light patterns
5. ✅ **Phase 15** (Intersection Foam) - Foam around geometry
6. ✅ **Phase 16** (Wake System) - Interactivity

### Performance/Polish:
7. **Phase 7** (Screen-Space Tessellation) - Performance optimization
8. ✅ **Phase 10** (SSR) - High-quality reflections
9. ✅ **Phase 11** (Dual Depth) - Correctness for complex scenes
10. **Phase 12** (Material Blending) - Multiple water types

---

## Next Steps for Quality Improvement

Based on current state, the highest-impact remaining improvements are:

### ✅ Completed High-Impact Features
- **Phase 13** (Jacobian Foam) - Physically-accurate foam at wave crests
- **Phase 14** (Temporal Foam) - Foam that persists and fades naturally
- **Phase 17** (Enhanced SSS) - Light glowing through thin wave peaks
- **Phase 9** (Caustics) - Animated underwater light patterns
- **Phase 15** (Intersection Foam) - Dynamic foam at shorelines and geometry
- **Phase 16** (Wake System) - V-shaped wakes with Kelvin angle and bow waves
- **Phase 10** (SSR) - Screen-space reflections with temporal filtering
- **Phase 11** (Dual Depth) - Scene depth for geometry intersection foam

### 1. Foam Texture Quality
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
