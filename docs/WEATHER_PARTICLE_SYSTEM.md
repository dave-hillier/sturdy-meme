# Weather Particle System Design Document

## Overview

A GPU-driven particle system for rendering rain and snow effects, inspired by the grass system architecture. Uses compute shaders for particle simulation and culling, with double-buffered rendering for optimal performance.

---

## Architecture

### Core Components

| Component | File | Purpose |
|-----------|------|---------|
| WeatherSystem | WeatherSystem.h/cpp | Main system manager |
| Compute Shader | weather.comp | Particle simulation, spawning, culling |
| Vertex Shader | weather.vert | Particle geometry generation |
| Fragment Shader | weather.frag | Particle appearance and lighting |

### Rendering Pipeline Integration

1. **Compute Pass** - Simulate and cull particles
2. **Shadow Pass** - Optional shadow casting for snow (rain typically skips)
3. **Main Pass** - Render particles with transparency after opaque geometry
4. **Buffer Swap** - Advance double-buffer sets

---

## Data Structures

### WeatherParticle (32 bytes)

| Field | Type | Description |
|-------|------|-------------|
| position | vec3 | World-space particle position |
| velocity | vec3 | Current velocity vector |
| lifetime | float | Remaining lifetime in seconds |
| size | float | Particle scale factor |
| rotation | float | For snow tumbling, rain splash angle |
| hash | float | Per-particle random seed |
| type | uint | 0 = rain, 1 = snow, 2 = splash |
| flags | uint | State flags (active, collided, etc.) |

### WeatherUniforms (256 bytes)

| Field | Type | Description |
|-------|------|-------------|
| cameraPosition | vec4 | Camera world position |
| frustumPlanes | vec4[6] | Frustum planes for culling |
| windDirectionStrength | vec4 | xy = direction, z = strength, w = turbulence |
| gravity | vec4 | Gravity vector (xyz) and terminal velocity (w) |
| spawnRegion | vec4 | xyz = center, w = radius |
| spawnHeight | float | Height above camera to spawn particles |
| groundLevel | float | Y coordinate of ground plane |
| particleDensity | float | Particles per cubic meter |
| maxDrawDistance | float | Culling distance |
| time | float | Current simulation time |
| deltaTime | float | Frame delta time |
| weatherType | uint | 0 = rain, 1 = snow, 2 = mixed |
| intensity | float | 0.0-1.0 precipitation strength |

### WeatherPushConstants (16 bytes)

| Field | Type | Description |
|-------|------|-------------|
| time | float | Current time for animation |
| deltaTime | float | Frame delta |
| cascadeIndex | int | For shadow rendering |
| padding | int | Alignment |

### WetnessUniforms (32 bytes)

Bound by all material shaders for wet surface rendering:

| Field | Type | Description |
|-------|------|-------------|
| wetnessIntensity | float | Current global wetness 0.0-1.0 |
| rainIntensity | float | Current precipitation rate |
| timeSinceRainStopped | float | Seconds since rain ended |
| dryingRate | float | Evaporation speed multiplier |
| puddleThreshold | float | Wetness level for puddle formation |
| rippleIntensity | float | Strength of puddle ripples |
| rippleSpeed | float | Animation speed for ripples |
| padding | float | Alignment |

---

## Particle Simulation

### Spawning Strategy

Following the grass system approach, particles spawn within the **camera frustum** with density prioritization for near-field detail:

**Dual-Zone Spawn System**

The spawn region is divided into two zones to ensure both visual coverage and near-field detail:

| Zone | Radius | Density | Purpose |
|------|--------|---------|---------|
| Near Zone | 0-8m | 100% density | Guaranteed detail around player/focus |
| Frustum Zone | 8m-max | Distance-scaled | Fill visible area efficiently |

**Near Zone (Player Bubble)**
- Spherical region centered on camera/player position
- Always spawns at full density regardless of view direction
- Ensures rain/snow visibly falls on and around the player character
- Particles here are never LOD-culled
- Critical for first-person and third-person camera perspectives

**Frustum Zone**
- Particles spawn within expanded camera frustum (add margin for wind drift)
- Spawn plane positioned at configurable height above camera (100-200m)
- Density falls off with distance from camera using smoothstep
- Frustum culling applied during simulation, not just rendering

**Spawn Distribution**
- Hash-based pseudo-random distribution seeded by grid cell and frame
- Particle pool recycling: particles respawn when hitting ground or exiting frustum
- Hybrid grid approach: subdivide frustum into cells, spawn N particles per visible cell
- Stagger spawning across frames to avoid burst patterns
- Wind prediction: offset spawn positions to account for horizontal drift during fall time

**Frustum-Aware Spawning Algorithm**
1. Project spawn plane (at spawn height) onto camera frustum
2. Divide projected area into grid cells
3. For each cell, determine visibility and distance from camera
4. Spawn particles proportional to cell's density weight
5. Near zone particles spawned separately with guaranteed allocation

**Target Particle Counts**

| Weather | Light | Medium | Heavy |
|---------|-------|--------|-------|
| Rain | 10,000 | 50,000 | 150,000 |
| Snow | 5,000 | 25,000 | 75,000 |

**Particle Budget Allocation**

| Zone | Budget % | Notes |
|------|----------|-------|
| Near Zone | 15-20% | Fixed allocation, never reduced |
| Frustum Near (8-30m) | 40-50% | High detail, moderate LOD |
| Frustum Far (30m+) | 30-40% | Heavy LOD, fills background |

### Physics Simulation

**Rain Behavior**
- High terminal velocity (9-13 m/s)
- Minimal wind resistance on vertical component
- Strong horizontal wind influence
- Near-vertical streaks
- Collision with ground spawns splash particles

**Snow Behavior**
- Low terminal velocity (1-3 m/s)
- High wind susceptibility
- Tumbling rotation animation
- Perlin noise-based drift (inspired by grass wind system)
- Gradual settling, no splash on impact
- Optional accumulation tracking

**Shared Physics**
- Gravity acceleration with terminal velocity clamping
- Wind force application using same WindSystem as grass
- Per-particle drag coefficient variation
- Turbulence via multi-octave Perlin noise

### Culling Pipeline

Following the grass system approach:

1. **Distance Culling** - Remove particles beyond max draw distance
2. **Frustum Culling** - Six-plane test with margin for particle size
3. **Behind Camera Culling** - Skip particles more than 2m behind camera
4. **LOD Particle Dropping** - Reduce count at distance using smoothstep

**Culling Parameters**

| Parameter | Rain | Snow |
|-----------|------|------|
| Max Distance | 100m | 80m |
| LOD Start | 50m | 40m |
| LOD End | 100m | 80m |
| Max Drop % | 80% | 70% |
| Frustum Margin | 1.0m | 2.0m |

---

## Rendering

### Particle Geometry

**Rain Particles**
- Stretched billboard quad (4 vertices, 2 triangles)
- Length based on velocity (motion blur effect)
- Width: 1-3mm
- Length: 20-100mm depending on fall speed
- Aligned to velocity direction

**Snow Particles**
- Camera-facing billboard (4 vertices, 2 triangles)
- Slight tumble rotation over time
- Size: 5-20mm diameter
- Optional: Multi-point star geometry for close-up detail (6 vertices)

**Splash Particles (Rain Only)**
- Horizontal ring or radial burst geometry
- Short lifetime (0.1-0.3 seconds)
- Spawned at collision point
- Fade out with lifetime

### Visual Effects

**Rain Appearance**
- Semi-transparent white/light blue color
- Alpha based on velocity (faster = more visible)
- Streak effect via vertex shader stretching
- Fresnel-like edge highlighting
- Refraction distortion optional (requires separate pass)

**Snow Appearance**
- Soft white color with slight blue tint
- Alpha varies with size and distance
- Subtle sparkle effect using hash-based noise
- Ambient occlusion from proximity to surfaces optional

### Lighting Integration

**Rain**
- Specular highlights from light sources
- Backlit glow when between camera and light
- Minimal shadow receiving (mostly transparent)
- God ray interaction when applicable

**Snow**
- Diffuse lighting with soft normals
- Self-shadowing within dense snowfall
- Emissive sparkle highlights
- Full shadow receiving on settled particles

### Blending and Depth

- Render after all opaque geometry
- Additive blending for rain (bright streaks)
- Alpha blending for snow (soft accumulation)
- Depth test enabled, depth write disabled
- Soft particle fading near opaque surfaces (depth buffer sampling)

---

## Double-Buffered Implementation

Following the grass system pattern:

### Buffer Sets (A/B)

| Buffer | Purpose | Size per Set |
|--------|---------|--------------|
| particleBuffer | All particle state | MAX_PARTICLES × 32 bytes |
| indirectBuffer | Draw indirect commands | 20 bytes |
| counterBuffer | Atomic spawn/death counters | 16 bytes |

### Frame Lifecycle

**Frame N:**
1. Compute shader reads buffer set A, writes simulation results to set B
2. Graphics pipeline reads from set A (previous frame's culled output)
3. Advance: compute target becomes A, render source becomes B

**Synchronization**
- Memory barrier between compute write and vertex read
- Pipeline barrier between compute dispatch and draw indirect

### Indirect Draw Command

Structure matching VkDrawIndirectCommand:
- vertexCount: 4 (quad) or 6 (star)
- instanceCount: Written by compute shader (survived culling)
- firstVertex: 0
- firstInstance: 0

---

## Compute Shader Design

### Workgroup Layout

- Local size: 256 invocations per workgroup
- Dispatch: ceil(MAX_PARTICLES / 256) workgroups
- Each invocation handles one particle

### Per-Frame Operations

1. **Reset Phase**
   - Clear indirect draw instance count to 0
   - Reset spawn counter

2. **Simulation Phase**
   - Read particle state
   - Apply gravity, wind, turbulence
   - Update position: pos += velocity × deltaTime
   - Check ground collision
   - Decrement lifetime
   - Mark dead particles for recycling

3. **Respawn Phase**
   - Dead particles randomly respawn in spawn region
   - Use atomic counter to limit spawns per frame
   - Initialize with appropriate starting values

4. **Culling Phase**
   - Apply distance, frustum, LOD culling
   - Survivors written to output buffer
   - Atomic increment of instance count

### Wind Integration

Reuse WindSystem uniforms and Perlin noise approach from grass:
- Sample wind at particle position
- Apply multi-octave noise for turbulence
- Different noise scales for rain (fast, directional) vs snow (slow, swirling)
- Gust response based on particle mass (snow more reactive)

---

## CPU-Side Management

### WeatherSystem Class

**Initialization**
- Create particle buffers (2 sets for double buffering)
- Create descriptor sets binding wind, camera, weather uniforms
- Load and compile shaders
- Set up compute and graphics pipelines

**Per-Frame Update**
- Update weather uniforms (camera position, time, delta time)
- Record compute dispatch
- Record graphics draw commands
- Advance buffer sets

**Weather Transitions**
- Smoothly interpolate intensity over time
- Blend between weather types (rain → snow via mixed state)
- Gradual spawn rate changes to avoid pop-in

### Integration Points

**Renderer.cpp additions:**
- Call weatherSystem.updateUniforms() before compute pass
- Call weatherSystem.recordCompute() in compute command buffer
- Call weatherSystem.recordDraw() after opaque geometry, before transparent
- Call weatherSystem.advanceBufferSet() at frame end

**WindSystem coordination:**
- Weather system binds same wind uniform buffer as grass
- Shared Perlin noise implementation ensures coherent movement

---

## Performance Considerations

### Memory Budget

| Resource | Size | Notes |
|----------|------|-------|
| Particle Buffer (×2) | 2 × 4.8 MB | 150k particles × 32 bytes |
| Uniforms | 512 bytes | Shared across frames |
| Total VRAM | ~10 MB | Comfortable for most GPUs |

### Compute Workload

- 150,000 particles ÷ 256 = 586 workgroups
- Each invocation: ~50 ALU operations
- Bottleneck: Memory bandwidth on particle read/write

### Render Workload

- Instanced rendering: One draw call per weather type
- Vertex count: 4-6 per particle
- Fragment cost: Low (simple alpha blend, minimal lighting)
- Overdraw consideration: Many small particles overlap

### Optimization Strategies

- Early-out culling in compute (skip simulation if definitely culled)
- Temporal coherence: Particles rarely teleport, cache-friendly access
- LOD system reduces particle count at distance
- Consider separate near/far particle pools with different detail levels

---

## Configuration Parameters

### Runtime-Adjustable

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| weatherType | 0-2 | 0 | Rain, snow, or mixed |
| intensity | 0.0-1.0 | 0.5 | Precipitation amount |
| windInfluence | 0.0-2.0 | 1.0 | Multiplier for wind effect |
| particleScale | 0.5-2.0 | 1.0 | Size multiplier |
| drawDistance | 20-150 | 100 | Max render distance |

### Compile-Time Constants

| Constant | Value | Description |
|----------|-------|-------------|
| MAX_PARTICLES | 150,000 | Maximum active particles |
| WORKGROUP_SIZE | 256 | Compute local size |
| NUM_BUFFER_SETS | 2 | Double buffering |
| SPAWN_HEIGHT | 150.0 | Meters above camera |

---

## Wet Surfaces System

Rain should affect surface appearance throughout the scene. This requires coordination between the weather system and material shaders.

### Wetness Data Flow

**WeatherSystem outputs:**
- Global wetness intensity (0.0-1.0) based on rain duration and intensity
- Exposed vs sheltered factor available to shaders
- Time since rain stopped (for drying)

**Uniform Buffer Addition (WetnessUniforms)**

| Field | Type | Description |
|-------|------|-------------|
| wetnessIntensity | float | Current global wetness 0.0-1.0 |
| rainIntensity | float | Current rain rate (for active drips) |
| timeSinceRainStopped | float | Seconds since rain ended (for drying) |
| dryingRate | float | How fast surfaces dry |
| puddleThreshold | float | Wetness level where puddles form |

### Surface Wetness Effects

**Roughness Modification**
- Wet surfaces become smoother/more reflective
- Interpolate material roughness toward lower value based on wetness
- Typical range: dry roughness → wet roughness of 0.1-0.3
- PBR formula: effectiveRoughness = mix(baseRoughness, wetRoughness, wetnessIntensity)

**Darkening**
- Wet surfaces appear darker due to light absorption
- Multiply albedo by darkening factor (0.5-0.8) based on wetness
- More porous materials darken more (concrete > metal)
- Material-dependent darkening intensity stored in texture or material parameter

**Specular/Reflection Enhancement**
- Increase fresnel effect on wet surfaces
- Water film creates mirror-like reflections at grazing angles
- Blend toward higher F0 value when wet

**Normal Map Dampening**
- Water fills micro-surface details
- Reduce normal map intensity when wet
- effectiveNormal = mix(normalMapSample, vec3(0,0,1), wetnessIntensity * 0.5)

### Material Integration

**Per-Material Wetness Parameters**

| Parameter | Range | Description |
|-----------|-------|-------------|
| wetnessAbsorption | 0.0-1.0 | How much material absorbs water (concrete=0.8, metal=0.1) |
| wetDarkening | 0.0-1.0 | Albedo darkening multiplier when wet |
| wetRoughness | 0.0-1.0 | Target roughness when fully wet |
| porosity | 0.0-1.0 | Affects drying time and puddle formation |

**Shader Modification Pattern**
1. Sample wetness uniforms
2. Calculate local wetness based on surface normal (upward-facing = more wet)
3. Modify roughness, albedo, and fresnel based on wetness
4. Apply to existing PBR lighting calculation

### Puddle Rendering

**Puddle Formation**
- Form in areas where wetness exceeds threshold
- Prefer concave geometry (use world-space Y gradient or heightmap)
- Puddles have near-zero roughness (mirror-like)
- Render as modified material, not separate geometry

**Puddle Identification**
- Option A: Procedural based on world position noise and height
- Option B: Baked puddle mask texture for terrain
- Option C: Runtime accumulation in screen-space or world-space buffer

**Puddle Surface Effects**
- Ripples from falling raindrops (animated normal perturbation)
- Ripple intensity based on current rain rate
- Multiple overlapping ripple rings using procedural noise
- Reflection of sky and nearby objects

### Drip and Streak Effects

**Vertical Surface Drips**
- Surfaces with normal facing sideways show water streaks
- Animated UV distortion creating downward flow
- Concentrated along edges and protrusions
- Can be procedural (noise-based) or texture-driven

**Drip Spawning from Edges**
- Detect geometry edges (silhouette or marked in mesh data)
- Spawn drip particles that fall from overhangs
- Lower particle count than main rain, but visually important

### Drying Simulation

**Drying Behavior**
- When rain stops, wetness gradually decreases
- Drying rate affected by:
  - Material porosity (porous dries slower initially, faster later)
  - Surface orientation (horizontal dries slower - puddles)
  - Wind speed (increases evaporation)
  - Time of day/temperature (optional complexity)

**Drying Formula**
- wetnessIntensity decreases over time when rainIntensity = 0
- Sheltered areas dry slower (under overhangs)
- Puddles persist longer than surface wetness

### Implementation Approach

**Shared Uniform Buffer**
- WeatherSystem maintains wetness uniforms
- All material shaders bind this buffer
- Single update per frame from CPU

**Shader Modifications**
- Add wetness calculation function to common shader include
- Modify existing PBR shaders to call wetness functions
- Wetness affects final surface parameters before lighting

**Texture Requirements**
- Optional: Porosity/absorption map per material
- Optional: Puddle mask for terrain
- Ripple normal map (tileable, animated via UV offset)

### Performance Considerations

- Wetness calculation is cheap (few ALU ops per fragment)
- Puddle ripples add one texture sample + normal blend
- No additional render passes required
- Memory: One small uniform buffer shared across all materials

---

## Future Enhancements

### Collision Detection
- Sample depth buffer for dynamic collision
- Splash spawning at impact points
- Particles slide off angled surfaces

### Accumulation System
- Track snow depth per-terrain-cell
- Visual snow buildup over time
- Integration with terrain rendering

### Indoor/Outdoor Awareness
- Volume-based rain blocking
- Particles don't spawn inside buildings
- Drip effects at roof edges

### Audio Integration
- Particle density drives rain/snow sound intensity
- Spatial audio from particle clusters

### Performance Tiers
- Low: 25% particle count, no shadows
- Medium: 50% particle count, simplified lighting
- High: Full particle count, all effects
- Ultra: Increased draw distance, accumulation

---

## File Structure

```
src/
├── WeatherSystem.h          # Class declaration, uniforms, particle structs
├── WeatherSystem.cpp        # Particle system, wetness state management
└── shaders/
    ├── weather.comp         # Particle simulation compute shader
    ├── weather.vert         # Particle vertex shader
    ├── weather.frag         # Particle fragment shader
    ├── wetness_common.glsl  # Shared wetness functions (include file)
    └── ripple.glsl          # Puddle ripple generation functions
```

**Modified Existing Shaders**
- scene.frag - Include wetness_common.glsl, apply to surface materials
- terrain.frag - Wetness + puddle rendering for ground surfaces
- Any PBR material shaders - Wetness parameter integration

---

## Implementation Phases

### Phase 1: Basic Rain
- Particle buffer and compute simulation
- Gravity and terminal velocity
- Simple billboard rendering
- Frustum culling with near-zone prioritization

### Phase 2: Wind Integration
- Bind WindSystem uniforms
- Apply wind force in compute
- Perlin noise turbulence
- Wind-predicted spawn positions

### Phase 3: Snow Support
- Second particle type with different physics
- Tumbling animation
- Slower, more wind-reactive movement

### Phase 4: Wet Surfaces (Basic)
- Wetness uniform buffer
- Surface darkening based on wetness
- Roughness reduction when wet
- Global wetness ramp-up during rain

### Phase 5: Visual Polish
- Proper lighting integration for particles
- Splash particles for rain ground collision
- Soft particle depth fading
- Weather transitions (rain ↔ snow)

### Phase 6: Puddles and Ripples
- Procedural puddle identification
- Puddle surface rendering (low roughness, reflective)
- Animated ripple normals from rain impact
- Ripple intensity tied to rain rate

### Phase 7: Drying and Drips
- Drying simulation when rain stops
- Per-material drying rates
- Vertical surface drip/streak effects
- Edge drip particle spawning

### Phase 8: Optimization
- LOD particle dropping at distance
- Performance profiling and tuning
- Quality tier presets
