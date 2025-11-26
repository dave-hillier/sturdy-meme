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

---

## Particle Simulation

### Spawning Strategy

Unlike grass which uses a fixed grid, weather particles spawn in a **cylindrical volume** centered on the camera:

**Spawn Region**
- Horizontal: Cylinder with radius matching draw distance (50m)
- Vertical: Spawn at fixed height above camera (100-200m)
- Density: Controlled by intensity parameter (light drizzle to heavy storm)

**Spawn Distribution**
- Use hash-based pseudo-random distribution across spawn region
- Maintain particle pool with recycling (particles respawn when hitting ground or leaving volume)
- Stagger spawning across frames to avoid burst patterns

**Target Particle Counts**

| Weather | Light | Medium | Heavy |
|---------|-------|--------|-------|
| Rain | 10,000 | 50,000 | 150,000 |
| Snow | 5,000 | 25,000 | 75,000 |

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
├── WeatherSystem.h          # Class declaration
├── WeatherSystem.cpp        # Implementation
└── shaders/
    ├── weather.comp         # Simulation compute shader
    ├── weather.vert         # Particle vertex shader
    └── weather.frag         # Particle fragment shader
```

---

## Implementation Phases

### Phase 1: Basic Rain
- Particle buffer and compute simulation
- Gravity and terminal velocity
- Simple billboard rendering
- Distance culling

### Phase 2: Wind Integration
- Bind WindSystem uniforms
- Apply wind force in compute
- Perlin noise turbulence

### Phase 3: Snow Support
- Second particle type with different physics
- Tumbling animation
- Slower, more wind-reactive movement

### Phase 4: Visual Polish
- Proper lighting integration
- Splash particles for rain
- Soft particle depth fading
- Weather transitions

### Phase 5: Optimization
- LOD particle dropping
- Full frustum culling
- Performance profiling and tuning
