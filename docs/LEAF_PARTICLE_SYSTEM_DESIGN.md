# Leaf Particle System Design

A GPU-driven falling leaf particle system with ground accumulation, player disruption, and wind/gust interaction. Inspired by the existing grass and weather systems.

---

## Overview

The leaf particle system manages two distinct leaf states:

1. **Falling Leaves** - Leaves descending from trees with tumbling physics
2. **Grounded Leaves** - Leaves resting on terrain that can be disturbed by player movement or strong gusts

Unlike billboard particles, leaves use flat quad geometry with proper 3D orientation, allowing realistic tumbling during fall and natural resting poses on the ground.

---

## Architecture

### Dual-Buffer Particle System

Following the established pattern from the grass and weather systems, the leaf system uses double-buffered GPU storage to prevent stalls between compute and graphics pipelines.

**Buffer Sets:**
- Set A: Written by compute, read by previous frame's graphics
- Set B: Read by graphics, written by previous frame's compute
- Swap occurs each frame after synchronization barriers

### Particle State Machine

Each leaf particle transitions through states:

```
SPAWNING → FALLING → LANDING → GROUNDED → [DISTURBED → SETTLING → GROUNDED]
                                       ↓
                                   BLOWN_AWAY → FALLING (re-enters cycle)
```

**State Descriptions:**
- **SPAWNING**: Initial frame, sets starting position and velocity
- **FALLING**: Active physics simulation with tumbling
- **LANDING**: Transitional state when leaf contacts ground (1-2 frames)
- **GROUNDED**: At rest on terrain surface, minimal simulation
- **DISTURBED**: Player proximity triggered lift-off
- **SETTLING**: Returning to ground after disturbance
- **BLOWN_AWAY**: Strong gust lifted leaf, re-enters falling state

---

## Data Structures

### LeafParticle (GPU Buffer)

| Field | Type | Size | Description |
|-------|------|------|-------------|
| position | vec3 | 12B | World position |
| state | uint | 4B | Current state enum |
| velocity | vec3 | 12B | Linear velocity |
| groundTime | float | 4B | Time spent grounded |
| orientation | vec4 | 16B | Quaternion rotation |
| angularVelocity | vec3 | 12B | Tumbling rate (radians/sec) |
| size | float | 4B | Leaf scale (0.02-0.08m) |
| hash | float | 4B | Per-particle random seed |
| leafType | uint | 4B | Leaf variety index (0-3) |
| flags | uint | 4B | Bit flags (active, visible, etc.) |
| **Total** | | **76B** | Padded to **80B** for alignment |

### LeafUniforms (Uniform Buffer)

| Field | Description |
|-------|-------------|
| viewProj | Combined view-projection matrix |
| cameraPos | Camera world position |
| playerPos | Player world position for disruption |
| playerVelocity | Player movement direction and speed |
| groundLevel | Terrain base height |
| deltaTime | Frame delta time |
| time | Accumulated time for animation |
| spawnRegionMin | AABB minimum for spawn volume |
| spawnRegionMax | AABB maximum for spawn volume |
| maxFallingLeaves | Cap on airborne particles |
| maxGroundedLeaves | Cap on resting particles |
| disruptionRadius | Player interaction range |
| disruptionStrength | Force applied on disruption |
| gustThreshold | Wind strength to lift grounded leaves |

---

## Compute Shader Pipeline

### Dispatch Configuration

- **Workgroup Size**: 256 threads (1D layout)
- **Max Particles**: 50,000 (falling) + 100,000 (grounded) = 150,000 total
- **Dispatch Groups**: ceil(150,000 / 256) = 586 workgroups

### Phase 1: Counter Reset

Single workgroup resets atomic counters for indirect draw buffer:
- `fallingInstanceCount = 0`
- `groundedInstanceCount = 0`

### Phase 2: Particle Simulation

Each thread processes one particle based on its state:

**Falling Leaves:**
1. Apply gravity (reduced: 0.4 × g for floaty descent)
2. Apply wind force from WindSystem
3. Apply drag (separate horizontal/vertical coefficients)
4. Update angular velocity with damping
5. Integrate position and orientation
6. Check ground collision
7. If collided, transition to LANDING state

**Grounded Leaves:**
1. Check player proximity for disruption
2. Check wind strength for gust lift-off
3. If disturbed, transition to DISTURBED state with upward impulse
4. Otherwise, increment groundTime counter
5. Apply subtle settling rotation toward terrain normal

**Disturbed/Settling Leaves:**
1. Apply reduced gravity
2. Apply outward force from player position
3. Swirl around player based on velocity direction
4. Gradually reduce energy and transition to SETTLING
5. When velocity drops below threshold, transition to GROUNDED

### Phase 3: Visibility and Culling

For each active particle:
1. Distance culling (falling: 80m, grounded: 40m)
2. Frustum culling with margin
3. LOD-based probability culling at distance
4. If visible, atomically increment appropriate draw count
5. Write to output instance buffer

### Phase 4: Respawn Management

If falling leaf count is below target:
1. Find inactive particle slots
2. Spawn at random position within spawn region (above trees)
3. Set initial tumbling rotation
4. Assign random leaf type and size

---

## Physics Model

### Falling Leaf Dynamics

**Forces Applied:**
- Gravity: `F_g = mass × (0, -4.0, 0)` (reduced gravity for slow descent)
- Wind: `F_w = windSample × windStrength × windSusceptibility`
- Drag: `F_d = -velocity × dragCoefficient × |velocity|`

**Drag Coefficients:**
- Horizontal: 0.8 (high air resistance side-on)
- Vertical: 0.4 (lower resistance falling flat)
- Angular: 0.92 per-frame damping

**Tumbling Behavior:**
- Initial angular velocity: random axis, 1-4 rad/sec
- Orientation affects drag (flat leaf falls slower)
- Wind applies torque based on leaf orientation
- Tumble rate decreases as leaf approaches ground

### Terminal Velocity

Leaves reach terminal velocity based on orientation:
- Flat falling: 0.5-1.0 m/s
- Edge-on: 2.0-3.0 m/s
- Average descent: ~1.5 m/s

### Ground Collision

**Detection:**
- Sample terrain heightmap at leaf XZ position
- Compare leaf Y against terrain height + small offset
- Account for leaf size (center vs. edge contact)

**Response:**
- Zero vertical velocity
- Apply friction to horizontal velocity (0.95× per frame)
- Align leaf orientation toward terrain normal over several frames
- Randomize final yaw rotation for natural scatter

### Player Disruption

**Trigger Conditions:**
- Player within disruption radius (default: 2.0m)
- Player is moving (velocity magnitude > 0.5 m/s)

**Disruption Force:**
- Direction: Outward from player + slight upward component
- Magnitude: `disruptionStrength × (1 - distance/radius)²`
- Added swirl component perpendicular to player velocity
- Random variation per-leaf (±30%)

**Cascade Effect:**
- Disturbed leaves can trigger nearby grounded leaves
- Secondary disruption has reduced strength (50%)
- Maximum cascade depth of 2 to prevent runaway

### Gust Response

**Threshold Check:**
- Sample wind strength at leaf position
- If strength exceeds gustThreshold, leaf becomes eligible
- Probability of lift-off increases with excess strength

**Lift-Off Behavior:**
- Apply strong upward impulse (3-5 m/s)
- Apply horizontal force in wind direction
- Transition to FALLING state
- Leaf rejoins normal falling simulation

---

## Rendering Pipeline

### Geometry Generation (Vertex Shader)

Unlike billboards, leaves use oriented quads that respect their 3D rotation.

**Vertex Layout:**
- 4 vertices per leaf (triangle strip or indexed quad)
- Positions calculated from orientation quaternion
- UV coordinates for leaf texture sampling

**Vertex Transformation:**
1. Start with unit quad in local space
2. Apply size scaling
3. Rotate by orientation quaternion
4. Translate to world position
5. Transform to clip space

**Normal Calculation:**
- Leaf normal = orientation × (0, 1, 0)
- Used for lighting calculations
- Enables proper shadow receiving

### Leaf Varieties

Four leaf types with distinct silhouettes:

| Type | Shape | Size Range | Color Tint |
|------|-------|------------|------------|
| 0 | Oval (oak-like) | 0.03-0.06m | Orange-brown |
| 1 | Pointed (maple-like) | 0.04-0.08m | Red-orange |
| 2 | Round (aspen-like) | 0.02-0.04m | Yellow-gold |
| 3 | Long (willow-like) | 0.05-0.07m | Brown-green |

**Texture Atlas:**
- Single 512×512 texture with 2×2 leaf types
- Alpha-tested edges for silhouette
- Color variation baked into texture
- Additional color tint applied in shader

### Fragment Shader

**Lighting Model:**
- Diffuse: Lambert with leaf normal
- Translucency: Light passing through leaf (subsurface approximation)
- Ambient: Stronger contribution for organic look

**Translucency Calculation:**
- Sample light direction vs. view direction
- If viewing backlit leaf, add transmitted light
- Transmitted light colored by leaf texture (warm glow)
- Strength: 0.3 × saturate(dot(-lightDir, viewDir))

**Shadow Integration:**
- Sample cascade shadow maps
- Apply PCF filtering for soft shadows
- Leaves both cast and receive shadows

### Render Order

1. Render opaque geometry (terrain, objects)
2. Render grass system
3. Render grounded leaves (opaque pass, alpha-tested)
4. Render falling leaves (alpha-blended, back-to-front sorting optional)
5. Render weather particles

---

## Shadow Casting

### Shadow Pass Configuration

**Separate Pipeline:**
- Simplified vertex shader (no lighting calculations)
- Fragment shader outputs depth only
- Alpha test threshold: 0.5

**Cascade Assignment:**
- Falling leaves: Render to all cascades (visible at distance)
- Grounded leaves: Near cascades only (cascade 0 and 1)

**Depth Bias:**
- Constant factor: 1.0 (higher than grass due to thin geometry)
- Slope factor: 2.0

---

## Integration with Existing Systems

### WindSystem Integration

**Uniform Buffer Sharing:**
- Leaf system reads from same wind uniform buffer as grass
- Ensures consistent wind behavior across systems

**Wind Sampling:**
- Same Perlin noise implementation
- Same octave weights (0.7, 0.2, 0.1)
- Additional vertical component for lift (grass only uses horizontal)

**Gust Synchronization:**
- Leaf gusts trigger when grass shows strong bending
- Visual coherence between grass waves and leaf lift-off

### WeatherSystem Coordination

**Rain Interaction:**
- During rain, increase leaf fall rate
- Wet leaves have reduced angular velocity (stick together)
- Grounded leaves less likely to be disturbed (wet weight)

**Snow Consideration:**
- During snow, gradually reduce visible grounded leaves
- Simulate burial effect

### Player System Hook

**Required Interface:**
- Player position (vec3)
- Player velocity (vec3)
- Player bounding radius (float)

**Update Frequency:**
- Every frame via uniform buffer update
- Low latency critical for responsive disruption

---

## Spawn Management

### Tree Canopy Integration

**Spawn Region Definition:**
- Defined by AABB encompassing tree canopy areas
- Multiple disjoint regions supported
- Spawn density weighted by region volume

**Spawn Position Selection:**
- Random XZ within region bounds
- Y at region top (canopy height)
- Bias toward tree trunk positions for realism

### Density Control

**Target Counts:**
- Calm: 100-500 falling, 5000-10000 grounded
- Breezy: 500-2000 falling, 3000-8000 grounded
- Gusty: 2000-5000 falling, 1000-5000 grounded (more airborne)

**Seasonal Variation:**
- Autumn: Maximum density, warm colors
- Summer: Minimal falling, few grounded (green tints)
- Winter: Very sparse, brown/dry colors

### Ground Coverage

**Distribution Goals:**
- Natural clustering under trees
- Sparse in open areas
- Accumulation against obstacles (future: collision volumes)

**Coverage Texture (Optional Enhancement):**
- Low-res texture marking valid grounding areas
- Prevents leaves on water, roads, etc.
- Sampled during ground collision

---

## Performance Considerations

### Memory Budget

| Buffer | Size | Count | Total |
|--------|------|-------|-------|
| Particle Buffer A | 80B | 150,000 | 12 MB |
| Particle Buffer B | 80B | 150,000 | 12 MB |
| Indirect Draw | 20B | 2 | 40 B |
| Uniform Buffer | 256B | 2 | 512 B |
| **Total** | | | ~24 MB |

### Compute Budget

**Per-Frame Operations:**
- 150,000 particles × state machine evaluation
- Physics integration for ~50,000 active falling
- Proximity checks for ~100,000 grounded
- Culling and visibility for all

**Optimization Strategies:**
- Early-out for inactive particles
- Spatial hashing for disruption checks (only test nearby grounded)
- LOD reduces distant particle processing
- Skip grounded leaf simulation when player distant

### Draw Call Efficiency

**Instanced Rendering:**
- Single draw call for all falling leaves
- Single draw call for all grounded leaves
- Indirect draw eliminates CPU readback

**Batching:**
- All leaf types in single atlas
- Type selection via instance data
- No material switches during draw

---

## Implementation Phases

### Phase 1: Core Falling System
- Particle buffer setup and double-buffering
- Basic compute shader with gravity and drag
- Simple quad rendering with orientation
- Single leaf type

### Phase 2: Ground Accumulation
- Ground collision detection
- State machine for landing transition
- Grounded leaf rendering
- Terrain normal alignment

### Phase 3: Player Disruption
- Player position uniform integration
- Proximity detection in compute
- Disruption force application
- Settling behavior

### Phase 4: Wind and Gust Integration
- WindSystem uniform buffer sharing
- Wind force application to falling leaves
- Gust-triggered lift-off for grounded
- Visual synchronization with grass

### Phase 5: Visual Polish
- Multiple leaf types and atlas
- Translucency lighting
- Shadow casting and receiving
- Color variation and seasonal tints

### Phase 6: Optimization
- Spatial hashing for disruption
- LOD system tuning
- Memory and compute profiling
- Draw call optimization

---

## File Structure

**Source Files:**
- `src/LeafSystem.h` - Class declaration, structures, constants
- `src/LeafSystem.cpp` - Vulkan resource management, buffer setup

**Shader Files:**
- `shaders/leaf.comp` - Particle simulation and state machine
- `shaders/leaf.vert` - Oriented quad generation
- `shaders/leaf.frag` - Leaf material and lighting

**Assets:**
- `textures/leaf_atlas.png` - 512×512 leaf type atlas

---

## Debug Visualization

**Debug Modes:**
1. State coloring (falling=blue, grounded=green, disturbed=red)
2. Velocity vectors
3. Disruption radius visualization
4. Wind force vectors
5. Spawn region bounds

**Performance Overlay:**
- Active particle counts by state
- Compute shader timing
- Draw call instance counts
- Memory buffer utilization
