# Procedural Grass Rendering Implementation Plan

A step-by-step guide to implementing procedural grass rendering inspired by Ghost of Tsushima's grass systems, adapted for this Vulkan game engine. Based on Eric Woley's GDC talk "Procedural Grass in Ghost of Tsushima" and the Altera blog post "Procedural Grass Rendering."

## Table of Contents

1. [Current State](#current-state)
2. [Design Philosophy](#design-philosophy)
3. [Phase 1: Terrain and World Tiling](#phase-1-foundation---terrain-and-world-tiling)
4. [Phase 2: Compute Shader Infrastructure](#phase-2-compute-shader-infrastructure)
5. [Phase 3: Grass Generation Compute Shader](#phase-3-grass-generation-compute-shader)
6. [Phase 4: Grass Vertex Shader](#phase-4-grass-vertex-shader)
7. [Phase 5: Wind System](#phase-5-wind-system)
8. [Phase 6: Grass Type System](#phase-6-grass-type-system)
9. [Phase 7: Fragment Shader and Materials](#phase-7-fragment-shader-and-materials)
10. [Phase 8: Shadow Handling](#phase-8-shadow-handling)
11. [Phase 9: Far LOD and Terrain Blending](#phase-9-far-lod-and-terrain-blending)
12. [Phase 10: Player Interaction](#phase-10-player-interaction)
13. [Phase 11: Procedural Asset Placement](#phase-11-procedural-asset-placement)
14. [Phase 12: Performance Optimizations](#phase-12-performance-optimizations)
15. [Appendix D: MoltenVK / macOS Compatibility](#appendix-d-moltenvk--macos-compatibility)

---

## Current State

- Basic forward rendering pipeline with depth buffer
- No terrain system (single test cube)
- No compute shader infrastructure
- No instancing support
- No indirect draw call support
- No wind system

---

## Design Philosophy

### Why Not Traditional Grass Cards?

Traditional grass rendering uses "grass cards" - textured quads with alpha-tested grass blade textures. While this approach is simple and was used in many older games, it has significant drawbacks for modern, high-fidelity grass rendering:

**Animation Limitations:** With grass cards, the entire quad animates as a single unit. Wind causes the whole card to sway, which looks unnatural because real grass blades move independently. Sucker Punch wanted each blade to have its own animation, reacting to wind direction and speed individually.

**Overdraw Problems:** Alpha-tested grass cards create massive overdraw. Each blade rendered as a quad means large portions of transparent pixels are being processed by the fragment shader only to be discarded. When you want dense grass coverage (which Ghost of Tsushima needed for its sweeping fields), this overdraw becomes prohibitively expensive.

**Limited Stylistic Control:** The painted, stylized aesthetic Ghost of Tsushima aimed for required large uniform swaths of grass types, almost like brush strokes. Grass cards make it difficult to control the "feel" of a field at a granular level.

### The Procedural Blade Approach

Instead of grass cards, this system renders individual grass blades as procedurally-generated cubic bezier curves. Each blade is a thin strip of geometry (a triangle strip) that follows a bezier curve from base to tip.

**Independent Animation:** Because each blade is its own piece of geometry with its own parameters, each can animate independently. The per-blade hash value offsets the animation phase, so adjacent blades don't move in lockstep.

**Minimal Overdraw:** A thin blade strip produces almost no wasted fragment shader work. You're filling the exact pixels the blade covers, with no transparent portions to discard.

**Artist Configurability:** The same system can render short lawn grass, tall wild grass, pampas grass, burnt fields, and flowers. Artists configure parameters per grass type, and the same shaders produce dramatically different looks.

**Dynamic Interaction:** Because blade parameters are computed each frame, the grass can react to player movement, wind changes, and environmental effects in real-time.

### Performance Target

Ghost of Tsushima rendered approximately 100,000 blades on screen at roughly 2.5 milliseconds end-to-end on PS4. The compute shader considered over 1 million potential blade positions per frame but culled most of them. This gives us a target to aim for, though exact numbers will depend on our specific hardware and scene complexity.

---

## Phase 1: Foundation - Terrain and World Tiling

Before grass can grow, we need terrain for it to grow on. More importantly, we need a data representation that allows the GPU to efficiently query "what kind of grass should be here, and how tall?"

### 1.1 The Tiled World Architecture

The world is divided into large terrain tiles, each covering a significant area (Ghost of Tsushima used tiles that contained 512x512 textures). Each tile stores multiple textures that describe the terrain and its grass:

**Height Map:** A 512x512 texture storing terrain elevation. This lets the compute shader place grass blades at the correct Y position without CPU involvement. Format: R16_UNORM (16-bit normalized gives good precision for height).

**Material Map:** Stores which terrain material (rock, dirt, sand, etc.) exists at each position. Grass typically doesn't grow on rock or in water, so the compute shader can skip those positions.

**Grass Type Map:** A 512x512 texture where each texel is an 8-bit index (0-255) indicating which grass type grows at that position. Index 0 typically means "no grass." This is critical for the painted aesthetic - artists paint large regions with specific grass types.

**Grass Height Map:** A 512x512 texture storing a height multiplier (0.0 to 1.0). This multiplied by the grass type's base height gives the final height. Allows artists to create variation within a grass type region - perhaps the grass near a path is trampled shorter.

At 512x512 resolution over a 200-meter tile, each texel covers approximately 39 centimeters. This is fine for grass placement since we add random jitter to break up the grid pattern.

**Terrain Tile Data Structure:**

```cpp
struct TerrainTile {
    glm::vec2 worldPosition;      // World-space corner position
    float tileSize;               // World units (e.g., 200m)
    uint32_t textureResolution;   // 512x512 recommended

    // GPU textures (512x512 each)
    VkImage heightMap;            // R16_UNORM - terrain height
    VkImage materialMap;          // R8_UINT - terrain material index
    VkImage grassTypeMap;         // R8_UINT - grass type index (0-255)
    VkImage grassHeightMap;       // R8_UNORM - grass height multiplier

    VkImageView heightMapView;
    VkImageView materialMapView;
    VkImageView grassTypeMapView;
    VkImageView grassHeightMapView;

    // Render tiles subdivision
    static constexpr uint32_t RENDER_TILES_PER_AXIS = 8;
    // Results in 64 render tiles per terrain tile
};
```

### 1.2 Render Tile Subdivision

Large terrain tiles must be subdivided into smaller "render tiles" for efficient grass processing. Each render tile becomes one compute shader dispatch and one draw call.

**Why Subdivide?**

If we tried to process an entire 200m terrain tile in one compute dispatch, we'd have too many potential grass blade positions (the grid would be enormous). By subdividing into smaller render tiles, each compute dispatch handles a manageable number of positions.

Additionally, render tiles at different distances from the camera can use different LOD levels. Near tiles have high blade density; far tiles have lower density with larger tile areas.

**Render Tile Structure:**

```cpp
struct GrassRenderTile {
    glm::vec2 worldMin;           // World-space bounds
    glm::vec2 worldMax;
    uint32_t parentTileIndex;     // Index into terrain tile array

    // UV bounds into parent tile's textures
    glm::vec2 uvMin;
    glm::vec2 uvMax;

    // LOD level (0 = highest detail, 1 = half density, etc.)
    uint32_t lodLevel;
    float tileSize;               // Increases with LOD

    // Instance data buffer offset
    uint32_t instanceBufferOffset;
    uint32_t maxBlades;           // Pre-allocated blade count
};
```

### 1.3 Texture Sampling Considerations

**Height Map Sampling:** Use bilinear filtering. We want smooth terrain height interpolation so grass blades don't "pop" between height levels as the camera moves.

**Grass Type Map Sampling:** This is trickier. Standard bilinear filtering on an index texture produces meaningless results - interpolating between grass type 3 and grass type 7 gives you grass type 5, which is nonsense.

Point sampling creates obvious texel boundaries in the grass. You can clearly see the 39cm grid in the field.

**The Solution - Gather and Random Select:** Instead of filtering, we use `textureGather` to fetch all four neighboring texels. Then we randomly select one of the four types, with probability weighted by the blade's position within the texel. A blade near the top-left corner is more likely to get the top-left type. This creates a natural dithered transition between grass type regions without meaningful interpolation artifacts.

---

## Phase 2: Compute Shader Infrastructure

Vulkan compute shaders form the backbone of efficient grass generation. The compute shader examines many potential blade positions in parallel, culls most of them, and outputs per-blade instance data for the survivors.

### 2.1 Why Compute Shaders?

Traditional instanced rendering requires the CPU to determine how many instances to draw. For grass, we might consider a million potential positions but only render 100,000 blades after culling. If the CPU had to determine which blades survive culling, we'd have a massive CPU-GPU roundtrip bottleneck.

Compute shaders solve this by doing the culling on GPU. The compute shader runs, determines how many blades survive, and writes that count directly to an indirect draw arguments buffer. The subsequent draw call reads its instance count from that buffer - the CPU never needs to know the exact count.

### 2.2 The Shader Pipeline Flow

The grass rendering pipeline has several stages:

```
For each batch of tiles:
    1. Grass Generation Compute Shader (per tile)
       - Each thread considers one grid position
       - Performs culling (distance, frustum, occlusion)
       - Survivors write instance data to buffer
       - Atomic counter tracks how many survived

    2. Indirect Args Setup Compute Shader (per tile)
       - Single-thread shader (trivial)
       - Reads atomic counter, writes to indirect draw args
       - Resets counter for next frame

    3. Memory Barrier
       - Ensure compute writes complete before vertex reads

    4. Indirect Draw Call (per tile)
       - Vertex shader reads instance data
       - Generates bezier curve blade geometry
       - Fragment shader handles material
```

### 2.3 Vulkan Compute Resources

Setting up compute in Vulkan requires several resources:

**Compute Pipeline:** Similar to graphics pipelines but simpler - just one shader stage.

**Command Buffer and Queue:** Compute work can submit to the same queue as graphics (if the queue family supports both) or a dedicated compute queue. Using separate queues enables async compute, where compute and graphics run simultaneously.

**Synchronization:** Semaphores coordinate compute-to-graphics handoff. The graphics queue waits on a semaphore signaled when compute completes. Memory barriers ensure compute shader writes are visible to vertex shader reads.

**Compute Resources Structure:**

```cpp
struct ComputeResources {
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline grassGenerationPipeline;
    VkPipeline indirectArgsSetupPipeline;

    VkCommandPool computeCommandPool;
    VkCommandBuffer computeCommandBuffer;

    VkQueue computeQueue;         // May be same as graphics queue
    uint32_t computeQueueFamily;

    // Synchronization
    VkSemaphore computeFinishedSemaphore;
    VkFence computeFence;
};
```

### 2.4 The Instance Data Buffer

This is where the compute shader writes per-blade data for surviving blades. The vertex shader later reads this buffer to generate actual geometry.

**What Goes in Instance Data?**

Each blade needs enough information for the vertex shader to generate its geometry and animation:

- **Position (vec3):** World position of the blade's base
- **Facing (vec2):** 2D direction the blade faces (determines which way it bends)
- **Wind Strength (float):** Pre-sampled wind at this position
- **Hash (float):** Random value for animation phase offset
- **Grass Type (uint):** Index into parameter array
- **Clump Data:** Which clump this blade belongs to, distance to clump center
- **Shape Parameters:** Final height, width, tilt, bend after all influences applied

This totals 16 floats (64 bytes) per blade. For 16,384 blades per tile and 8 tiles in the buffer, we need 8MB.

**Instance Data Structure (GLSL):**

```glsl
struct GrassBladeInstance {
    vec3 position;        // World position of blade base (3 floats)
    vec2 facing;          // 2D facing direction (2 floats)
    float windStrength;   // Wind force at this position (1 float)
    float hash;           // Per-blade random hash (1 float)
    uint grassType;       // Index into grass parameters array

    // Clumping data
    float clumpId;        // Which clump this blade belongs to
    float clumpDistance;  // Distance to clump center
    vec2 clumpCenter;     // Position of clump center

    // Shape parameters (post-modification)
    float height;         // Final blade height
    float width;          // Blade width
    float tilt;           // Forward tilt amount
    float bend;           // Curvature amount
};
// Total: 16 floats = 64 bytes
```

### 2.5 Indirect Draw Arguments

Vulkan's `vkCmdDrawIndexedIndirect` reads draw parameters from a buffer rather than taking them as function arguments. This lets the GPU determine instance counts.

**Indirect Command Structure:**

```cpp
// Matches VkDrawIndexedIndirectCommand exactly
struct DrawIndexedIndirectCommand {
    uint32_t indexCount;      // Total indices to draw
    uint32_t instanceCount;   // Always 1 (we use vertex pulling, not instancing)
    uint32_t firstIndex;      // 0
    int32_t vertexOffset;     // 0
    uint32_t firstInstance;   // Offset into instance buffer for this tile
};
```

**Note on "Instancing":** We're not using traditional GPU instancing where each instance draws the same mesh. Instead, we draw a large number of vertices where each set of N vertices (one blade) reads its own data from the instance buffer. This is sometimes called "vertex pulling" - the vertex shader pulls its parameters from a buffer rather than using instanced vertex attributes.

### 2.6 Atomic Counter

Each compute thread that decides to emit a blade needs to claim a slot in the instance buffer. Atomic operations handle this safely:

```glsl
uint instanceIndex = atomicAdd(counter.bladeCount, 1u);
if (instanceIndex >= maxBladesPerTile) {
    return; // Buffer full, drop this blade
}
// Write to instances[instanceIndex]
```

The atomic counter is reset to 0 after each frame (or after the indirect args shader reads it).

---

## Phase 3: Grass Generation Compute Shader

This is where most of the magic happens. The compute shader determines which grass blades exist and calculates their parameters.

### 3.1 Thread Organization

Each compute shader thread processes one potential blade position. The shader dispatches with a 2D grid covering the render tile:

- Work group size: 8x8x1 (64 threads per group)
- Dispatch size: Enough groups to cover the desired blade density

For example, if a 25m render tile needs approximately 16,000 potential positions, we might dispatch 128x128 threads (16,384 total).

### 3.2 Position Determination

Each thread calculates its world position from its thread ID:

```glsl
uvec2 gridPos = gl_GlobalInvocationID.xy;
uvec2 gridSize = gl_NumWorkGroups.xy * gl_WorkGroupSize.xy;

vec2 normalizedPos = vec2(gridPos) / vec2(gridSize);
vec2 worldPos2D = tileWorldMin + normalizedPos * (tileWorldMax - tileWorldMin);
```

This produces a regular grid, which would be visible. To break it up, we add random jitter based on a hash of the grid position:

```glsl
vec2 cellSize = tileSize / vec2(gridSize);
vec2 jitter = (hash2(vec2(gridPos)) - 0.5) * cellSize;
worldPos2D += jitter;
```

The jitter moves each blade randomly within its grid cell, eliminating visible patterns.

### 3.3 Multi-Stage Culling

Not every potential position produces a visible blade. The compute shader culls positions in order of expense:

**Distance Culling (Cheapest):** Calculate distance to camera. If beyond max draw distance, exit immediately. This is a single distance calculation.

```glsl
float distToCamera = length(worldPos - cameraPosition);
if (distToCamera > maxDrawDistance) {
    return;
}
```

**Frustum Culling:** Test position against the six frustum planes. If outside, exit. This is six dot products.

```glsl
for (int i = 0; i < 6; i++) {
    if (dot(vec4(worldPos, 1.0), frustumPlanes[i]) < -bladeRadius) {
        return; // Outside frustum
    }
}
```

**Grass Type/Height Check:** Sample the grass type and height textures. If type is 0 (no grass) or height is 0, exit. This requires texture samples, so it comes after cheaper tests.

**Occlusion Culling (Most Expensive):** Project the position to screen space and sample the hierarchical Z-buffer. If something solid is already closer, the blade is occluded. This is optional and may not be worth the cost in all scenes.

### 3.4 Grass Type Sampling with Gather

As discussed earlier, we can't use standard filtering on the grass type texture. Instead, we use `textureGather`:

```glsl
uvec4 gatheredTypes = textureGather(grassTypeMap, uv, 0);
```

This returns the four texels at corners of the sampling quad. We then randomly select one, weighted by position:

```glsl
vec2 texelFrac = fract(uv * textureSize(grassTypeMap, 0));

// Bilinear weights (distance to each corner)
float w00 = (1.0 - texelFrac.x) * (1.0 - texelFrac.y); // Bottom-left
float w10 = texelFrac.x * (1.0 - texelFrac.y);         // Bottom-right
float w01 = (1.0 - texelFrac.x) * texelFrac.y;         // Top-left
float w11 = texelFrac.x * texelFrac.y;                 // Top-right

// Random selection weighted by position
float rand = hash(worldPos2D);
float cumulative = 0.0;
uint selectedType = gatheredTypes[0];

cumulative += w00;
if (rand < cumulative) selectedType = gatheredTypes[0];
cumulative += w10;
if (rand < cumulative) selectedType = gatheredTypes[1];
// ... etc.
```

This creates a smooth dithered transition between grass type regions.

### 3.5 The Clumping System (Voronoi)

Without clumping, grass fields look like golf course lawns - uniform and artificial. Real fields have natural variation: areas where grass grows taller, areas where it leans in a similar direction, subtle color variations.

**Why Clumps Exist in Nature:**

- Shadows: A tree's shadow reduces grass growth, creating a clump pattern
- Soil variation: Nitrogen-rich patches grow taller grass
- Wind patterns: Grass on hillsides leans consistently
- Animal paths: Trampled areas have shorter grass

**Procedural Voronoi for Clumps:**

We use a procedural Voronoi algorithm to divide the world into clumps. For any 2D position, we determine which clump it belongs to and how far it is from the clump center.

The algorithm:
1. Divide space into a grid
2. Each grid cell has one clump point (jittered from cell center)
3. For any query position, find the nearest clump point among the 3x3 neighboring cells
4. That's the clump this blade belongs to

```glsl
void calculateClump(vec2 worldPos, float clumpScale,
                    out float clumpId, out float clumpDist, out vec2 clumpCenter) {
    vec2 scaledPos = worldPos / clumpScale;
    vec2 cellId = floor(scaledPos);
    vec2 cellFrac = fract(scaledPos);

    float minDist = 10.0;
    vec2 nearestPoint;
    float nearestId;

    // Check 3x3 neighborhood (9 potential clump centers)
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighborCell = cellId + vec2(x, y);

            // Each cell's clump point is jittered based on hash
            vec2 jitter = hash2(neighborCell) * 0.8 + 0.1;
            vec2 clumpPoint = (neighborCell + jitter) * clumpScale;

            float dist = length(worldPos - clumpPoint);
            if (dist < minDist) {
                minDist = dist;
                nearestPoint = clumpPoint;
                nearestId = hash(neighborCell); // Unique ID per clump
            }
        }
    }

    clumpId = nearestId;
    clumpDist = minDist;
    clumpCenter = nearestPoint;
}
```

**Clump scale** controls how large the clumps are. A 2-meter scale means clumps are roughly 2 meters across.

**How Clumps Influence Blades:**

Once we know which clump a blade belongs to, we use that to modify blade parameters:

- **Height:** Blades in the same clump have similar heights (clumpId modulates height)
- **Facing:** Blades can point toward or away from clump center
- **Pull:** Blades can be physically closer to clump center
- **Color:** Fragment shader uses clumpId to select color variation

Each grass type has parameters controlling how much clumping affects it. Wild grass might have high clump influence; lawn grass might have almost none.

### 3.6 Computing Final Blade Parameters

After culling and clump calculation, we compute final blade shape:

```glsl
// Read base parameters from grass type
float baseHeight = grassTypes.typeData[typeOffset].x;
float heightVariance = grassTypes.typeData[typeOffset].y;
// ... etc.

// Apply clump influence
float clumpHeightMod = mix(1.0, 0.5 + clumpId, clumpHeightInfluence);

// Apply random variance
float finalHeight = baseHeight * heightMult * clumpHeightMod *
                    (1.0 + (bladeHash - 0.5) * heightVariance);

// Facing: blend between random and toward/away from clump
vec2 randomFacing = normalize(hash2(worldPos) - 0.5);
vec2 toClumpCenter = normalize(clumpCenter - worldPos);
vec2 facing = normalize(mix(randomFacing, toClumpCenter, clumpFacingInfluence));

// Similar for width, tilt, bend...
```

### 3.7 Writing Instance Data

Surviving blades claim a slot atomically and write their data:

```glsl
uint instanceIndex = atomicAdd(counter.bladeCount, 1u);
if (instanceIndex >= maxBladesPerTile) return;

uint baseOffset = (instanceBufferOffset + instanceIndex) * 4u;

grassData.instances[baseOffset + 0] = vec4(worldPos, facing.x);
grassData.instances[baseOffset + 1] = vec4(facing.y, windStrength, bladeHash,
                                            uintBitsToFloat(grassType));
grassData.instances[baseOffset + 2] = vec4(clumpId, clumpDist, clumpCenter);
grassData.instances[baseOffset + 3] = vec4(height, width, tilt, bend);
```

### 3.8 Indirect Args Setup Shader

After the main compute shader completes, a trivial single-thread shader copies the blade count to the indirect draw arguments:

```glsl
void main() {
    uint bladeCount = counter.bladeCount;
    uint indicesPerBlade = (vertsPerBlade - 2) * 3; // Triangle strip to triangles

    drawArgs.indexCount = bladeCount * indicesPerBlade;
    drawArgs.instanceCount = 1;
    drawArgs.firstIndex = 0;
    drawArgs.vertexOffset = 0;
    drawArgs.firstInstance = tileInstanceOffset;

    counter.bladeCount = 0; // Reset for next frame
}
```

---

## Phase 4: Grass Vertex Shader

The vertex shader transforms instance data into actual geometry. Each grass blade is a cubic bezier curve rendered as a triangle strip.

### 4.1 Why Cubic Bezier Curves?

A cubic bezier curve is defined by four control points (P0, P1, P2, P3). The curve passes through P0 and P3 (endpoints) and is influenced by P1 and P2 (control points that determine curvature).

**Advantages for Grass:**

- **Easy Position Calculation:** The parametric formula B(t) gives any point along the curve for t in [0,1]
- **Easy Derivatives:** The derivative B'(t) gives the tangent, which we need for normals
- **Intuitive Control:** The four control points give natural control over blade shape
- **Smooth Animation:** Moving control points smoothly animates the curve smoothly

**The Bezier Formula:**

```
B(t) = (1-t)³P0 + 3(1-t)²tP1 + 3(1-t)t²P2 + t³P3
```

In GLSL:

```glsl
vec3 evaluateBezier(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float mt = 1.0 - t;
    float mt2 = mt * mt;
    float mt3 = mt2 * mt;
    float t2 = t * t;
    float t3 = t2 * t;

    return mt3*p0 + 3.0*mt2*t*p1 + 3.0*mt*t2*p2 + t3*p3;
}
```

### 4.2 Vertex Index Interpretation

The vertex shader receives `gl_VertexIndex` and must determine:
1. Which blade this vertex belongs to
2. Where along the blade (t value, 0 at base, 1 at tip)
3. Left side or right side of the blade

For high LOD with 15 vertices per blade:
- Vertices 0,2,4,6,8,10,12,14 are left side (even indices)
- Vertices 1,3,5,7,9,11,13 are right side (odd indices)
- The t value increases as we go up

```glsl
uint vertsPerBlade = 15;
uint bladeIndex = gl_VertexIndex / vertsPerBlade;
uint vertIndex = gl_VertexIndex % vertsPerBlade;

uint segmentIndex = vertIndex / 2;
uint numSegments = (vertsPerBlade - 1) / 2; // 7 segments for 15 verts

float t = float(segmentIndex) / float(numSegments);
bool isRightSide = (vertIndex % 2) == 1;
```

### 4.3 Non-Uniform Vertex Distribution

Grass blades curve most near the tip. If we distribute vertices uniformly along t, we waste vertices on straight sections and lack resolution in curved sections.

Ghost of Tsushima used an artist-configurable parameter to redistribute vertices. A common approach is to use smoothstep:

```glsl
// Original uniform t
float t = float(segmentIndex) / float(numSegments);

// Remap: more vertices near middle/tip where curvature is higher
t = t * t * (3.0 - 2.0 * t); // Smoothstep remapping
```

This bunches vertices toward the curved portion for smoother bending.

### 4.4 Setting Up Control Points

The compute shader gives us base position, facing, height, tilt, and bend. We construct control points from these:

**P0 (Base):** Simply the blade's world position.

**P3 (Tip):** Offset from base by height (upward) and tilt (forward in facing direction).

```glsl
vec3 facingDir = vec3(facing.x, 0.0, facing.y);
vec3 tipOffset = vec3(0.0, height, 0.0) + facingDir * tilt * height;
vec3 p3 = basePos + tipOffset;
```

**P1 and P2 (Control Points):** These create the curve. The "bend" parameter pushes the curve upward and outward:

```glsl
vec3 midPoint = (p0 + p3) * 0.5;
vec3 bendOffset = vec3(0.0, height * bend, 0.0);

p1 = mix(p0, midPoint, 0.33) + bendOffset * 0.5;
p2 = mix(p0, midPoint, 0.66) + bendOffset;
```

High bend values create more droopy, curved blades. Low bend values create stiffer, straighter blades.

### 4.5 Wind Animation

Wind animation is a simple sine wave applied to the tip position:

```glsl
// Phase offset from per-blade hash (so blades don't move in sync)
float windPhase = bladeHash * PI * 2.0 + time * 3.0;

// Wave that increases along blade (base doesn't move, tip moves most)
float windWave = sin(windPhase + t * PI * 0.5) * 0.5 + 0.5;

// Apply wind in wind direction, scaled by wind strength and height along blade
vec3 windOffset = vec3(windDirection.x, 0.0, windDirection.y) *
                  windStrength * windWave * t * t;

tipOffset += windOffset;
```

The `t * t` factor means the tip moves much more than the middle, which moves more than the base. This matches how real grass bends.

**Important Caveat:** The arc length of a bezier curve changes as you move control points. During strong wind animation, blades might stretch slightly. Ghost of Tsushima found this acceptable if animation stays relatively constrained.

### 4.6 Bobbing Animation

Beyond wind push, blades bob up and down slightly:

```glsl
float bobPhase = bladeHash * PI * 2.0 + time * 2.0 + t * PI;
float bob = sin(bobPhase) * 0.02 * height * windStrength;

tipOffset.y += bob;
```

The phase varies with position along blade (`t * PI`) giving a swaying look.

### 4.7 Generating the Vertex Position

With control points set, evaluate the bezier curve at our t value:

```glsl
vec3 curvePos = evaluateBezier(p0, p1, p2, p3, t);
```

Now we need to offset left or right to create width. Calculate the normal:

```glsl
vec3 tangent = normalize(evaluateBezierDerivative(p0, p1, p2, p3, t));
vec3 bladeNormal = normalize(cross(tangent, facingDir));
```

Apply width offset (tapering toward tip):

```glsl
float widthAtT = width * (1.0 - t * 0.9); // 90% taper
float offset = widthAtT * (isRightSide ? 0.5 : -0.5);

vec3 worldPos = curvePos + bladeNormal * offset;
```

### 4.8 View-Space Thickening

When a blade is viewed edge-on, it becomes a single-pixel line that aliases badly and contributes little to field fullness.

Ghost of Tsushima solved this by slightly thickening blades that are viewed edge-on:

```glsl
vec3 viewDir = normalize(cameraPosition - curvePos);
float edgeFactor = 1.0 - abs(dot(bladeNormal, viewDir));

// Thicken by up to 30% when viewed edge-on
widthOffset *= (1.0 + edgeFactor * 0.3);
```

This makes fields look fuller without adding more blades.

### 4.9 Normal Tilting

For a more rounded, natural appearance, normals are tilted outward slightly:

```glsl
vec3 normal = normalize(bladeNormal + tangent * 0.2);
```

This catches light at more angles, making blades look less flat.

### 4.10 Distance-Based Normal Blending

At distance, individual blade normals create specular noise ("glittering"). As blades animate, their normals vary dramatically, creating distracting sparkle.

Solution: Blend normals toward a common direction (straight up, or a per-clump average normal) at distance:

```glsl
float normalBlend = smoothstep(20.0, 50.0, distToCamera);
vec3 clumpNormal = vec3(0.0, 1.0, 0.0);
normal = normalize(mix(normal, clumpNormal, normalBlend));
```

Combined with reducing glossiness at distance, this creates smooth, coherent distant fields.

### 4.11 Blade Folding for Short Grass

When grass is very short, each blade covers few pixels. Rendering thousands of tiny blades is wasteful.

The solution: for short grass, fold the vertices to create two blades instead of one. The same vertex budget produces twice the coverage.

```glsl
bool isSplitBlade = height < 0.3;

if (isSplitBlade) {
    // First half of vertices = blade 0
    // Second half of vertices = blade 1
    uint whichBlade = vertIndex >= (vertsPerBlade / 2) ? 1 : 0;

    if (whichBlade == 1) {
        vertIndex -= vertsPerBlade / 2;
    }

    // Offset each blade sideways
    vec3 splitOffset = bendDir * width * (whichBlade == 0 ? -1.0 : 1.0) * 0.5;
    p0 += splitOffset;
    p1 += splitOffset;
    // ... etc.

    height *= 0.7; // Each split blade is shorter
}
```

This technique came from Altera's blog post and significantly improves short grass density.

### 4.12 LOD Transitions

High LOD uses 15 vertices per blade; low LOD uses 7. The transition must be seamless.

**Challenge 1: Vertex Count Change**
Going from 15 to 7 vertices causes geometry popping. Solution: In the transition zone, high LOD blades morph their geometry toward the low LOD shape before switching.

**Challenge 2: Blade Density Change**
Low LOD tiles are twice the size but have the same blade count, meaning half the density. Solution: During transition, high LOD drops 3 out of every 4 blades so density matches low LOD at the boundary.

```glsl
// In compute shader
float lodTransition = smoothstep(highLODMax - transitionZone, highLODMax, dist);

// Probabilistically drop blades as we approach transition
uint bladeId = uint(hash(worldPos2D) * 4.0);
if (bladeId < 3u && lodTransition > hash(worldPos2D + vec2(1000.0))) {
    return; // Skip this blade
}
```

---

## Phase 5: Wind System

Wind is crucial for bringing grass fields to life. Ghost of Tsushima used wind not just aesthetically but as a gameplay mechanic - the wind guides the player toward objectives.

### 5.1 Design Goals

**Unified System:** The same wind system must be sampleable on both GPU (for grass animation) and CPU (for gameplay). You can't have grass blowing east while the wind indicator shows north.

**Low Overhead:** Wind sampling happens for every grass blade every frame. It must be fast.

**Natural Variation:** Real wind has gusts and lulls, varying spatially. Uniform wind looks artificial.

### 5.2 The Wind Model

Ghost of Tsushima used 2D Perlin noise scrolled in the wind direction:

- At any world position, sample Perlin noise
- The noise position scrolls over time in the wind direction
- Noise value (0-1) represents local wind strength
- Combined with global wind direction vector for final force

**Why This Works:**

Perlin noise has spatial coherence - nearby points have similar values. This means adjacent grass blades feel similar wind, creating visible waves across the field.

The scrolling creates the appearance of wind moving through the field. You see waves propagate in the wind direction.

**Multi-Octave for Detail:**

Single-octave Perlin noise is too smooth. Adding a second octave at higher frequency and lower amplitude creates more natural variation:

```glsl
float sampleWind(vec2 worldPos, vec2 windDir, float windSpeed, float time) {
    vec2 scrolledPos = worldPos - windDir * time * windSpeed;

    float noise = 0.0;
    float amplitude = 1.0;
    float frequency = 0.1;

    for (int i = 0; i < 2; i++) {
        noise += perlinNoise(scrolledPos * frequency) * amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return noise * 0.5 + 0.5; // Remap to [0, 1]
}
```

### 5.3 CPU Wind Sampling

For gameplay (stealth detection, guiding wind indicator), the CPU needs to sample wind:

**C++ Wind System:**

```cpp
struct WindSystem {
    glm::vec2 direction = glm::vec2(1.0f, 0.0f);
    float speed = 5.0f;
    float strength = 1.0f;
    float gustFrequency = 0.5f;

    // Pre-computed gradient table for Perlin noise
    std::vector<glm::vec2> gradients;

    void initialize() {
        // Generate gradient table (same as GPU hash function would produce)
        gradients.resize(256);
        for (int i = 0; i < 256; i++) {
            float angle = hash(i) * 2.0f * PI;
            gradients[i] = glm::vec2(cos(angle), sin(angle));
        }
    }

    float sample(glm::vec2 worldPos, float time) const {
        // Same algorithm as GPU shader
        glm::vec2 scrolled = worldPos - direction * time * speed;
        // ... Perlin noise evaluation using gradients table
    }
};
```

**Important:** The CPU and GPU must produce identical results for the same inputs. This requires careful implementation - floating point differences can cause visual/gameplay mismatch.

### 5.4 GPU Wind Uniforms

The compute and vertex shaders receive wind parameters:

```glsl
layout(set = X, binding = Y) uniform WindData {
    vec2 direction;
    float strength;
    float speed;
    float time;
    // ... gust parameters
};
```

The compute shader can pre-sample wind at each blade position and store it in instance data, or the vertex shader can sample on the fly. Pre-sampling in compute is often faster since it's done once per blade rather than once per vertex.

---

## Phase 6: Grass Type System

A single grass type produces uniform, boring fields. The type system enables dramatic variety from the same rendering code.

### 6.1 What Defines a Grass Type?

**Geometry Parameters:**

- `baseHeight`: Average height in world units
- `heightVariance`: How much height can vary (0-1 multiplier)
- `baseWidth`: Average blade width
- `widthVariance`: Width variation
- `baseTilt`: How much blades lean forward
- `tiltVariance`: Tilt variation
- `baseBend`: Curvature amount
- `bendVariance`: Bend variation

**Clumping Parameters:**

- `clumpHeightInfluence`: How much clump ID affects height
- `clumpFacingInfluence`: How much blades align toward/away from clump center
- `clumpPullInfluence`: How much blades cluster toward clump center
- `clumpColorInfluence`: How much clump ID affects color

**Animation Parameters:**

- `windInfluence`: How strongly wind affects this grass
- `windWaveFrequency`: Speed of wind wave motion
- `bobFrequency`: Speed of bobbing motion
- `bobAmplitude`: Height of bobbing

**Material Parameters:**

- `baseColor`: Color at blade base
- `tipColor`: Color at blade tip
- `glossiness`: Specular intensity
- `translucency`: Light transmission through blade

**Special Flags:**

- `isStealthGrass`: Does hiding in this grass work for gameplay?
- `hasPampasFlowers`: Trigger procedural asset placement?

### 6.2 Example Grass Type Configurations

**Short Lawn Grass:**
- Height: 0.15m, low variance
- Width: 0.01m (thin)
- Tilt: 0.1 (nearly upright)
- Bend: 0.1 (stiff)
- Wind influence: 0.5 (doesn't move much)
- Color: Dark green

**Tall Wild Grass:**
- Height: 0.8m, high variance
- Width: 0.02m
- Tilt: 0.3 (leaning)
- Bend: 0.5 (curved)
- High clump height influence (clustered height variation)
- Wind influence: 1.2 (very responsive)
- Stealth grass: true

**Pampas Grass:**
- Height: 1.5m
- Bend: 0.7 (very curved, drooping)
- High clump facing influence (blades radiate from clump center)
- Wind influence: 1.5 (dramatic motion)
- Triggers pampas flower asset placement

**Burnt Grass:**
- Height: 0.3m
- Width: 0.008m (very thin)
- Tilt: 0.5 (bent over)
- Wind influence: 0.3 (stiff, dead)
- Color: Brown/black

**Spider Lilies (Flowers):**
- Height: 0.4m
- Width: 0.025m (wider for flower appearance)
- Low bend (upright stalks)
- Tip color: Bright red
- High clump color influence (uniform color within clumps)

### 6.3 Storage and Access

Grass type parameters are stored in a uniform buffer accessible by both compute and fragment shaders:

```cpp
struct GrassTypeParams {
    // ... all parameters ...
};

struct GrassTypeLibrary {
    static constexpr uint32_t MAX_GRASS_TYPES = 256;
    std::array<GrassTypeParams, MAX_GRASS_TYPES> types;

    VkBuffer uniformBuffer;
    VmaAllocation allocation;
};
```

The compute shader reads parameters by type index:

```glsl
uint typeOffset = grassType * 4; // 4 vec4s per type
vec4 params0 = grassTypes.typeData[typeOffset + 0];
vec4 params1 = grassTypes.typeData[typeOffset + 1];
// ... unpack parameters
```

---

## Phase 7: Fragment Shader and Materials

The fragment shader handles the visual appearance of grass blades - color, lighting, translucency.

### 7.1 Texture Mapping

Three textures control grass appearance:

**Vein Texture (64x1, grayscale):**
A horizontal stripe pattern representing the central vein of a grass blade. The U coordinate maps across blade width (0 = left edge, 1 = right edge). The center is slightly brighter.

The V coordinate repeats along the blade length, creating a striped pattern if visible.

**Color Palette Texture (256x64, RGB):**
This is the key to the "painted" aesthetic. The U axis is controlled by clump ID (0-1 from the Voronoi hash). The V axis is controlled by position along blade (0 = base, 1 = tip).

Artists paint this palette texture. Each horizontal stripe is a different clump color. Each vertical gradient shows how color changes from base to tip.

By controlling this texture, artists create the large uniform swaths that give Ghost of Tsushima its distinctive look. The actual color differences are very subtle - just enough variation to avoid monotony without breaking the painted feel.

**Gloss Texture (64x1, grayscale):**
Specular intensity across blade width. The edges might be slightly less glossy than the center.

### 7.2 Translucency

Real grass blades are semi-translucent. Light hitting the back of a blade partly transmits through, creating a warm glow when backlit by the sun.

Simple translucency approximation:

```glsl
float NdotL = max(dot(N, L), 0.0);
float backLight = max(dot(-N, L), 0.0) * translucency;
vec3 diffuse = albedo * (NdotL + backLight) * lightColor;
```

This adds light from the back side, scaled by the translucency parameter.

### 7.3 Ambient Occlusion

Pre-baked SSAO doesn't work well for grass (too much motion for temporal stability). Instead, we output a constant AO value based on position along the blade:

```glsl
float ao = 1.0 - (1.0 - t) * 0.6; // t=0 at base (dark), t=1 at tip (light)
```

The base of blades is dark (occluded by other blades). Tips are lighter (exposed to sky).

### 7.4 Distance-Based Specular Reduction

At distance, blade normals vary dramatically in screen space. The grass is glossy, so specular highlights create noise as blades animate - the field glitters unpleasantly.

Solution: Fade out specular at distance.

```glsl
float specularFade = 1.0 - smoothstep(20.0, 50.0, distToCamera);
float finalGloss = glossiness * glossVariation * specularFade;
```

Combined with normal blending from the vertex shader, this creates smooth, coherent distant fields.

### 7.5 Why Not Store Velocity?

Ghost of Tsushima's grass doesn't write to the velocity buffer (used for motion blur and temporal effects).

**Why It's Difficult:**

To calculate velocity, you need the previous frame's position. For grass, that requires:
- Previous frame's wind data (direction and speed can change)
- Previous frame's displacement data (player might have walked through)
- Previous frame's per-blade data (which is regenerated each frame)

Caching all this data is expensive in memory and compute.

**Why It's Problematic Even If You Do:**

Grass blades constantly wave over each other. Temporal accumulation (used by SSAO, motion blur) would see blades occluding and disoccluding frame-to-frame, creating a splotchy, unstable result.

Ghost of Tsushima accepted the lack of motion blur on grass and used per-blade AO instead of temporal SSAO.

---

## Phase 8: Shadow Handling

Shadows are expensive for grass. Running the full compute + vertex pipeline for each shadow-casting light is impractical.

### 8.1 The Shadow Problem

Proper grass shadows would require:
1. Run grass compute shader from light's perspective
2. Run grass vertex shader to generate shadow map geometry
3. Render to shadow map
4. Repeat for each shadow cascade, for each light

This multiplies the already-expensive grass pipeline by the number of shadow-casting lights.

### 8.2 The Imposter Solution

Instead of rendering actual grass geometry to shadow maps, Ghost of Tsushima used an imposter approach:

**Concept:** Render the terrain mesh, but raise each vertex by the grass height at that position. Then use dithered depth output to approximate the varying density of grass shadows.

**Vertex Shader:**

```glsl
void main() {
    // Sample grass height at this terrain vertex
    float grassHeight = texture(grassHeightMap, inTexCoord).r;

    // Raise the terrain vertex
    vec3 raisedPos = inPosition + vec3(0.0, grassHeight, 0.0);

    gl_Position = lightSpaceMatrix * vec4(raisedPos, 1.0);
    fragGrassHeight = grassHeight;
}
```

**Fragment Shader with Dithering:**

```glsl
void main() {
    // 4x4 Bayer dither matrix
    const float bayerMatrix[16] = float[]( /* ... */ );

    ivec2 pixel = ivec2(gl_FragCoord.xy) % 4;
    float threshold = bayerMatrix[pixel.y * 4 + pixel.x];

    // Discard some fragments based on grass height
    // Denser grass = more shadow = less discard
    if (grassHeight < threshold * 0.5) {
        discard;
    }
}
```

When combined with shadow filtering (PCF), the dithered pattern creates soft shadows that approximate grass density.

**Limitations:**
- Hard edges where the terrain mesh has low resolution
- Doesn't capture individual blade shadows
- Can't shadow one blade on another

### 8.3 Screen-Space Shadows for Detail

For fine shadow detail (blade self-shadowing, close-range shadows), Ghost of Tsushima relied on screen-space shadows:

**Why Screen-Space Works for Grass:**
- Grass is very thin (no thickness problems)
- Only short-range shadows needed (grass is small)
- Handles self-shadowing well
- No pre-rendering required

Screen-space shadows ray-march through the depth buffer to detect occlusion. For grass, this catches nearby blade shadows that the imposter misses.

### 8.4 Combined Shadow Result

The final shadow for a grass pixel combines:
- Shadow map result (using imposter geometry)
- Screen-space shadow result

```glsl
float finalShadow = min(shadowMapShadow, screenSpaceShadow);
```

This isn't physically perfect, but it's visually convincing and performant.

---

## Phase 9: Far LOD and Terrain Blending

At vista distances, rendering individual grass blades is impossible. You need a fallback.

### 9.1 The Vista Problem

Ghost of Tsushima has viewpoints where players can see most of the island. Grass must be represented somehow, but you can't render millions of blades.

Early experiments tried using the clump normal information to shade the terrain as if it were grass. This was expensive and didn't account for things like red spider lily fields that would incorrectly show as green.

### 9.2 The Artist Texture Solution

The pragmatic solution: for far distances, render an artist-authored texture on the terrain instead of grass.

**Per Grass Type:** Each grass type has an associated far-LOD texture that represents how that grass looks from distance.

**Blending:** In the terrain fragment shader, blend between normal terrain and far-LOD grass texture based on distance:

```glsl
float farLODBlend = smoothstep(transitionStart, transitionEnd, distToCamera);

if (farLODBlend > 0.01 && grassType > 0) {
    vec3 farGrassColor = texture(farLODTextures[grassType], terrainUV).rgb;
    albedo = mix(albedo, farGrassColor, farLODBlend);
}
```

**Advantages:**
- Very cheap (just a texture sample)
- Artists can paint exactly the look they want
- Works for any grass type, including colorful flowers

**Disadvantages:**
- No movement at distance (grass is static)
- Requires artist work per grass type
- Transition can be noticeable if not tuned

---

## Phase 10: Player Interaction

Grass that ignores the player feels dead. Reacting to movement brings the world alive.

### 10.1 The Displacement Buffer

A 2D texture stores displacement vectors across a world region around the camera:

- Format: RG16F (two 16-bit floats for X and Z displacement)
- Resolution: 512x512
- Coverage: 50m x 50m centered on camera
- Texel size: ~10cm

**Displacement Sources:**

The CPU tracks entities that push grass:
- Player character
- NPCs
- Horses
- Projectiles
- Environmental effects

Each source has position, radius, and strength. The displacement update shader applies these to the texture.

### 10.2 Displacement Update Compute Shader

Each frame, update the displacement texture:

```glsl
void main() {
    ivec2 texCoord = ivec2(gl_GlobalInvocationID.xy);
    vec2 worldPos = texelToWorld(texCoord);

    // Decay existing displacement (grass springs back)
    vec2 currentDisp = imageLoad(displacementMap, texCoord).rg * decay;

    // Apply each displacement source
    for (uint i = 0; i < numSources; i++) {
        vec3 sourcePos = sources[i].xyz;
        float radius = sources[i].w;
        float strength = strengths[i].x;

        vec2 toSource = worldPos - sourcePos.xz;
        float dist = length(toSource);

        if (dist < radius) {
            float falloff = 1.0 - (dist / radius);
            falloff *= falloff; // Quadratic

            vec2 pushDir = normalize(toSource);
            currentDisp += pushDir * strength * falloff;
        }
    }

    // Clamp magnitude
    float mag = length(currentDisp);
    if (mag > maxDisplacement) {
        currentDisp = currentDisp / mag * maxDisplacement;
    }

    imageStore(displacementMap, texCoord, vec4(currentDisp, 0, 0));
}
```

### 10.3 Applying Displacement to Grass

In the grass compute shader, sample the displacement texture and modify blade parameters:

```glsl
vec2 dispUV = worldPosToDispUV(worldPos2D);
vec2 displacement = texture(displacementMap, dispUV).rg;

// Rotate facing toward displacement direction
facing = normalize(facing + displacement * 0.5);

// Increase tilt (blade bends over)
tilt += length(displacement) * 0.3;
```

This causes blades to lean away from the player as they walk through.

### 10.4 Decay and Spring-Back

The decay multiplier (e.g., 0.95) makes grass spring back after being pushed. High decay = grass stays pushed longer. Low decay = quick spring-back.

For tall grass, slower spring-back looks natural. For short lawn grass, quick recovery is appropriate.

### 10.5 Stealth Grass Detection (CPU)

Gameplay needs to know if the player is hidden. This requires CPU-readable grass height data.

**Approach:** When tiles load, run a compute shader that copies grass height information to a CPU-readable buffer. This is done once per tile load, not per frame.

The CPU can then query:

```cpp
bool isPositionHidden(glm::vec3 position, float crouchHeight) {
    float grassHeight = sampleGrassHeightCPU(position);
    return grassHeight > crouchHeight;
}
```

**Consistency Note:** Ghost of Tsushima found that returning the actual grass height was inconsistent - players couldn't predict if they were hidden. Instead, each grass type is flagged as "stealth grass" or "decorative grass," returning a constant height. All stealth grass includes pampas grass flowers for visual consistency.

---

## Phase 11: Procedural Asset Placement

Grass blades alone don't complete a field. Procedurally-placed assets (pampas plumes, flowers, small rocks) add the finishing touch.

### 11.1 The Growth System Concept

Ghost of Tsushima used a "growth system" that procedurally places artist-authored mesh assets based on where grass types are painted.

**How It Works:**

- When a tile loads, run a compute shader
- For each position in a grid (lower density than grass), check the grass type
- If it matches an asset's required type, place an instance
- Output transforms to an instance buffer
- Render all instances with GPU instancing

### 11.2 Asset Definition

Each growth asset specifies:

- **Mesh:** The actual geometry to render
- **Required Grass Type:** Only place where this type exists
- **Density:** Instances per square meter
- **Scale Range:** Random scale variation
- **Rotation Range:** Random Y-axis rotation

For example, pampas grass plumes require grass type 3 (pampas grass), have density 0.1/m², scale 0.8-1.2, full rotation.

### 11.3 Tile Caching

Growth instances are expensive to compute. Rather than regenerating every frame:

- Keep a 3x3 grid of tiles around the camera
- When the camera moves to a new area, compute new tiles, discard old ones
- Store computed instances in buffers

### 11.4 Random Placement Algorithm

Eric Woley mentioned starting with simple random jitter for placement:

```glsl
void main() {
    uint id = gl_GlobalInvocationID.x;

    // Grid position with jitter
    vec2 basePos = gridPosition(id);
    vec2 jitter = hash2(basePos) * cellSize;
    vec2 worldPos = basePos + jitter;

    // Check grass type
    uint grassType = sampleGrassType(worldPos);
    if (grassType != requiredGrassType) return;

    // Random transform
    float scale = mix(minScale, maxScale, hash(worldPos));
    float rotation = hash(worldPos + vec2(100)) * TWO_PI;

    // Atomic allocate and write
    uint idx = atomicAdd(counter, 1);
    writeTransform(idx, worldPos, rotation, scale);
}
```

Interestingly, artists used this for rice paddies and it worked so well that more complex placement algorithms were never needed.

---

## Phase 12: Performance Optimizations

The grass system is performance-critical. Several optimizations keep it fast.

### 12.1 Double-Buffer Tile Processing

Processing all tiles simultaneously would require massive buffers. Instead, use a double-buffer strategy:

- Instance buffer holds 8 tiles of data (two batches of 4)
- Frame N: Compute batch A while rendering batch B
- Frame N+1: Compute batch B while rendering batch A

This keeps the GPU busy and limits memory usage.

**Implementation:**

```cpp
void processGrass() {
    // Batch 0 tiles are in slots 0-3
    // Batch 1 tiles are in slots 4-7

    if (currentBatch == 0) {
        // Dispatch compute for tiles using slots 0-3
        // Render tiles from previous frame in slots 4-7
    } else {
        // Dispatch compute for tiles using slots 4-7
        // Render tiles from previous frame in slots 0-3
    }

    currentBatch = 1 - currentBatch;
}
```

### 12.2 Occlusion Culling

Blades behind solid geometry are wasted. The compute shader can sample a hierarchical Z-buffer to skip occluded positions:

```glsl
vec4 clipPos = viewProj * vec4(worldPos, 1.0);
vec2 screenUV = (clipPos.xy / clipPos.w) * 0.5 + 0.5;

float occluderDepth = textureLod(hiZBuffer, screenUV, mipLevel).r;
float bladeDepth = clipPos.z / clipPos.w;

if (bladeDepth > occluderDepth + epsilon) {
    return; // Occluded
}
```

Ghost of Tsushima found this was a "small performance win" in most scenes - many grass positions aren't significantly occluded. The benefit depends on your scene composition.

### 12.3 Subgroup Operations

Modern GPUs support subgroup (wave/wavefront) operations that reduce atomic contention:

Instead of each thread doing an atomic add:

```glsl
// Every thread does an atomic - 64 atomics per workgroup
uint idx = atomicAdd(counter, 1);
```

Use ballot and broadcast:

```glsl
// Count active threads in subgroup
bool shouldEmit = /* ... */;
uvec4 ballot = subgroupBallot(shouldEmit);
uint activeCount = bitCount(ballot.x); // Assumes 32-wide subgroup

// Single atomic per subgroup
uint baseIdx;
if (gl_SubgroupInvocationID == 0) {
    baseIdx = atomicAdd(counter, activeCount);
}
baseIdx = subgroupBroadcast(baseIdx, 0);

// Each thread calculates its own index
uint myIdx = baseIdx + bitCount(ballot.x & ((1u << gl_SubgroupInvocationID) - 1u));
```

This reduces atomics by ~32x, reducing contention.

### 12.4 Shared Memory for Type Parameters

Grass type parameters are frequently accessed. Loading them into shared memory once per workgroup is faster than each thread sampling the buffer:

```glsl
shared vec4 sharedTypeParams[16]; // Assume max 4 types active in tile

void main() {
    // First 4 threads load parameters
    if (gl_LocalInvocationIndex < 4) {
        uint typeIdx = /* common types in this tile */;
        sharedTypeParams[gl_LocalInvocationIndex * 4 + 0] = typeData[typeIdx * 4 + 0];
        // ... etc.
    }

    barrier();

    // All threads read from shared memory
    vec4 myParams = sharedTypeParams[localTypeIndex * 4 + 0];
}
```

---

## Implementation Order

Structured as vertical slices - each milestone produces something visible and testable. You should have working grass on screen by the end of Milestone 2.

---

### Milestone 1: Compute-to-Draw Pipeline (No Grass Yet)

**Goal:** Verify the compute → indirect draw pipeline works before adding grass complexity.

**What you'll see:** Coloured dots/quads scattered on screen.

1. Create a compute shader that outputs hardcoded positions to a storage buffer
2. Create the atomic counter buffer and indirect args buffer
3. Write a trivial "indirect args setup" compute shader
4. Create graphics pipeline that reads positions from buffer, draws point sprites or quads
5. Wire up `vkCmdDrawIndirect` to read instance count from the buffer
6. **Verify:** You see dots on screen. Changing the compute shader changes the dots.

```glsl
// Minimal compute shader - just emit a grid of positions
void main() {
    uvec2 id = gl_GlobalInvocationID.xy;
    vec2 pos = vec2(id) * 0.5 - vec2(16.0); // 32x32 grid

    uint idx = atomicAdd(counter.count, 1u);
    positions[idx] = vec4(pos.x, 0.0, pos.y, 1.0);
}
```

**Why this first:** If this doesn't work, nothing else will. Isolate the infrastructure before adding grass logic.

---

### Milestone 2: Grass Blades on a Flat Plane

**Goal:** See actual grass blades waving in wind. No terrain yet - just a flat ground plane.

**What you'll see:** A patch of animated grass on Y=0.

1. Expand compute shader to output full instance data (position, facing, height, hash)
2. Replace point sprites with the bezier curve vertex shader
3. Add simple wind animation (sine wave based on hash and time)
4. Add basic fragment shader (solid green, maybe height-based gradient)
5. **Verify:** Grass blades sway in wind. Camera can move around them.

```glsl
// Compute: scatter grass on a flat area
vec2 worldPos2D = tileMin + normalizedPos * tileSize + jitter;
vec3 worldPos = vec3(worldPos2D.x, 0.0, worldPos2D.y); // Flat Y=0

float hash = computeHash(worldPos2D);
vec2 facing = normalize(hash2(worldPos2D) - 0.5);

// Write instance: position, facing, height=0.5, width=0.02, tilt=0.2, bend=0.3
```

**Checkpoint:** At this point you have working procedural grass. Everything after this is refinement.

---

### Milestone 3: Distance Culling and Basic LOD

**Goal:** Performance scales with view distance. Grass fades out properly.

**What you'll see:** Grass only renders within a radius. Distant grass has fewer blades.

1. Add frustum planes to compute shader uniforms
2. Implement distance culling (skip blades beyond max distance)
3. Implement frustum culling (skip blades outside view)
4. Add LOD: reduce blade count at distance (drop 3/4 blades in transition zone)
5. **Verify:** GPU time drops when looking at sky. Blade count varies with camera position.

**Why now:** You need culling working before adding terrain, otherwise you'll be debugging performance and correctness simultaneously.

---

### Milestone 4: Terrain Height Sampling

**Goal:** Grass follows terrain elevation.

**What you'll see:** Grass on hills instead of floating/buried.

1. Create a test heightmap texture (can be procedural noise, or a simple ramp)
2. Add heightmap sampler to compute shader
3. Sample height and set blade Y position accordingly
4. **Verify:** Grass follows the heightmap. Create a visible hill to confirm.

```glsl
float terrainHeight = texture(heightMap, uv).r * maxHeight;
vec3 worldPos = vec3(worldPos2D.x, terrainHeight, worldPos2D.y);
```

**Keep it simple:** Don't add grass types yet. Just prove height sampling works.

---

### Milestone 5: Grass Types and Height Multiplier

**Goal:** Different grass in different areas.

**What you'll see:** Tall grass in some areas, short in others (visually distinct).

1. Create grass type texture (R8_UINT, paint regions with type indices 1, 2, 3)
2. Create grass height multiplier texture (R8_UNORM)
3. Implement `textureGather` weighted random selection for type
4. Add 2-3 grass type parameter sets (short/medium/tall)
5. Read type parameters in compute shader, vary height/width/tilt
6. **Verify:** Painted regions show different grass characteristics.

**Artistic milestone:** This is where the "painted field" look starts to emerge.

---

### Milestone 6: Clumping (Voronoi)

**Goal:** Natural variation within grass type regions.

**What you'll see:** Grass has organic clusters instead of uniform randomness.

1. Implement the Voronoi clump calculation
2. Pass clumpId and clumpDistance to instance data
3. Modulate height by clumpId
4. Modulate facing direction (toward or away from clump center)
5. **Verify:** Visible clusters of taller grass. Blades in a cluster lean similarly.

**Before/after comparison:** Toggle clumping on/off to see the difference - uniform randomness vs. natural clustering.

---

### Milestone 7: Material Polish

**Goal:** Grass looks good, not just correct.

**What you'll see:** Proper lighting, colour variation, translucency.

1. Add colour gradient (base to tip) in fragment shader
2. Add clump-based colour variation (subtle!)
3. Implement translucency (backlight when sun is behind grass)
4. Add AO darkening at blade base
5. Implement view-space thickening (fatten edge-on blades)
6. Add distance-based normal blending and specular fade
7. **Verify:** Grass looks cohesive at all distances. No glittering at distance.

---

### Milestone 8: Shadow Integration

**Goal:** Grass casts and receives shadows.

**What you'll see:** Grass shadows on terrain, grass shadowed by other objects.

1. Implement shadow imposter (raised terrain mesh with dithered depth)
2. Render imposter to existing shadow map pass
3. Sample shadow map in grass fragment shader
4. Optionally: add screen-space shadow sampling for fine detail
5. **Verify:** Grass casts soft shadows. Shadows from trees fall on grass.

**Depends on:** Your existing shadow system from the lighting plan.

---

### Milestone 9: Player Interaction

**Goal:** Grass reacts to movement.

**What you'll see:** Grass bends away as you walk through it.

1. Create displacement texture (RG16F, 512x512)
2. Implement displacement update compute shader
3. Add player position as a displacement source
4. Sample displacement in grass compute shader
5. Modify facing and tilt based on displacement
6. Add decay so grass springs back
7. **Verify:** Walk through grass field, see it part around you.

**Gameplay milestone:** If you have stealth mechanics, this is where they become possible.

---

### Milestone 10: Far LOD (Terrain Texture Fallback)

**Goal:** Grass visible at vista distances.

**What you'll see:** Distant fields are grass-coloured terrain, not bare.

1. Create far LOD texture per grass type
2. Blend terrain shader toward grass texture at distance
3. Tune transition to be seamless with nearest grass tiles
4. **Verify:** Vista views show grass-covered hills, not sudden cutoff.

---

### Milestone 11: Procedural Asset Placement

**Goal:** Flowers, pampas plumes, etc. scattered in fields.

**What you'll see:** Authored mesh assets appearing in grass regions.

1. Create growth asset compute shader (similar to grass but lower density)
2. Output transform data (position, rotation, scale)
3. Render with GPU instancing
4. Cache 3x3 tiles around camera
5. **Verify:** Pampas grass type regions have pampas flower meshes.

---

### Milestone 12: Performance Optimization

**Goal:** Hit target frame budget.

**What you'll see:** Consistent performance, profile data to prove it.

1. Profile and identify bottlenecks
2. Implement double-buffer tile processing if not already
3. Add occlusion culling if profiling shows benefit
4. Query subgroup support, enable optimizations if available
5. Tune blade counts, draw distances, LOD thresholds
6. **Verify:** Stable frame time with target blade count on screen.

**Do this last:** Premature optimization wastes effort. Get it working and looking right first.

---

## Summary: What You Can See After Each Milestone

| Milestone | What's Visible |
|-----------|----------------|
| 1 | Dots on screen (pipeline works) |
| 2 | **Grass blades waving in wind** |
| 3 | Grass culled by distance/frustum |
| 4 | Grass follows terrain height |
| 5 | Different grass types in painted regions |
| 6 | Natural clumping variation |
| 7 | Polished materials and lighting |
| 8 | Shadows cast and received |
| 9 | Grass reacts to player |
| 10 | Vista distances have grass |
| 11 | Flowers and assets in fields |
| 12 | Stable performance |

**Key insight:** By the end of Milestone 2, you have working grass. Everything else is iterative improvement on a functional system.

---

## References

- **Altera Blog Post:** "Procedural Grass Rendering" (original inspiration)
- **GDC 2021 Talk:** Eric Woley, "Procedural Grass in Ghost of Tsushima"
- **Related Talk:** Bill Rockenbeck, "Blowing from the West" (Wind System)
- **Related Talk:** Matt Pullman, "Samurai Landscapes: Building and Rendering Tsushima Island on PS4"

---

## Appendix A: Vulkan Resource Checklist

### Buffers

- [ ] Grass instance data buffer (8MB, storage + vertex usage)
- [ ] Index buffer for grass blade triangle strips (1MB)
- [ ] Indirect draw arguments buffer (64 tiles x 20 bytes)
- [ ] Atomic counter buffer (64 tiles x 4 bytes)
- [ ] Grass type parameters uniform buffer
- [ ] Wind parameters uniform buffer
- [ ] Displacement texture (512x512, RG16F)

### Pipelines

- [ ] Grass generation compute pipeline
- [ ] Indirect args setup compute pipeline
- [ ] Displacement update compute pipeline
- [ ] Grass graphics pipeline (vertex + fragment)
- [ ] Shadow imposter graphics pipeline

### Descriptor Sets

- [ ] Compute: terrain textures, instance buffer, counters, params
- [ ] Graphics: instance buffer, uniforms, material textures
- [ ] Shadow: light space matrix, grass height texture

### Synchronization

- [ ] Compute-to-graphics memory barrier
- [ ] Double-buffer semaphores (if using async compute)
- [ ] Fence for CPU readback (stealth detection)

---

## Appendix B: Texture Formats and Sizes

| Texture | Format | Size | Purpose |
|---------|--------|------|---------|
| Terrain Height | R16_UNORM | 512x512 | Terrain elevation |
| Grass Type | R8_UINT | 512x512 | Grass type index (0-255) |
| Grass Height | R8_UNORM | 512x512 | Height multiplier |
| Displacement | RG16F | 512x512 | Player interaction |
| Vein | R8_UNORM | 64x1 | Blade detail |
| Color Palette | RGB8_UNORM | 256x64 | Clump/height color |
| Gloss | R8_UNORM | 64x1 | Specular variation |
| Far LOD (per type) | RGB8_UNORM | 256x256 | Distance fallback |

---

## Appendix C: Performance Targets

Based on Ghost of Tsushima's PS4 performance:

| Metric | Target |
|--------|--------|
| Blades considered | ~1,000,000 |
| Blades rendered | ~100,000 |
| End-to-end time | ~2.5ms |
| Instance buffer size | 8MB |
| Verts per blade (high LOD) | 15 |
| Verts per blade (low LOD) | 7 |
| Tiles in buffer | 8 |
| Max blades per tile | 16,384 |

Actual numbers will vary based on target hardware and scene complexity. Profile early and often.

---

## Appendix D: MoltenVK / macOS Compatibility

This section outlines platform-specific considerations when running on macOS via MoltenVK (Vulkan over Metal).

### Fully Supported Features

The core grass system uses features that are fully supported:

| Feature | Status | Notes |
|---------|--------|-------|
| Compute shaders | Supported | Full Metal compute support |
| Storage buffers | Supported | Instance data buffer works |
| Scalar atomics | Supported | `atomicAdd` on `uint` in SSBO |
| Indirect draw | Supported | `vkCmdDrawIndexedIndirect` works |
| 2D texture sampling | Supported | All terrain textures |
| `textureGather` | Supported | Used for grass type sampling |
| `imageLoad`/`imageStore` | Supported | Displacement buffer updates |
| Push constants | Supported | Tile parameters |

### Constraint 1: Subgroup Operations

**Issue:** Phase 12.3 suggests using `subgroupBallot` and `subgroupBroadcast` to reduce atomic contention. Support varies by hardware.

**Status:** MoltenVK 1.4+ supports basic subgroup operations on Apple Silicon and recent iOS devices.

**Recommendation:** Query support at runtime and provide a fallback:

```cpp
VkPhysicalDeviceSubgroupProperties subgroupProps = {};
subgroupProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;

VkPhysicalDeviceProperties2 props2 = {};
props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
props2.pNext = &subgroupProps;

vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

bool hasSubgroupArithmetic = (subgroupProps.supportedOperations &
                              VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0;
bool hasSubgroupBallot = (subgroupProps.supportedOperations &
                          VK_SUBGROUP_FEATURE_BALLOT_BIT) != 0;

// Use subgroup optimizations only if supported, otherwise fall back to per-thread atomics
```

**Fallback Shader (without subgroups):**

```glsl
// Standard per-thread atomic - works everywhere
uint instanceIndex = atomicAdd(counter.bladeCount, 1u);
```

### Constraint 2: Async Compute

**Issue:** The double-buffer tile processing strategy benefits from async compute (compute and graphics running in parallel). Behaviour varies by hardware.

**Hardware Behaviour:**

| Hardware | Async Compute |
|----------|---------------|
| Apple Silicon (M1+) | Good parallel execution |
| Intel Macs | May serialize compute and graphics |
| iOS (A11+) | Good parallel execution |

**Recommendation:** The double-buffer strategy is still valuable even without true async - it keeps the GPU fed with work. However, don't assume parallel execution. Always use proper synchronization:

```cpp
// Compute submission
VkSubmitInfo computeSubmit = {};
computeSubmit.signalSemaphoreCount = 1;
computeSubmit.pSignalSemaphores = &computeFinishedSemaphore;
vkQueueSubmit(computeQueue, 1, &computeSubmit, VK_NULL_HANDLE);

// Graphics submission waits on compute
VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
VkSubmitInfo graphicsSubmit = {};
graphicsSubmit.waitSemaphoreCount = 1;
graphicsSubmit.pWaitSemaphores = &computeFinishedSemaphore;
graphicsSubmit.pWaitDstStageMask = &waitStage;
vkQueueSubmit(graphicsQueue, 1, &graphicsSubmit, VK_NULL_HANDLE);
```

On Intel Macs, expect compute and graphics to run sequentially. Profile to verify actual behaviour.

### Constraint 3: Workgroup Size

**Recommendation:** Apple GPUs have a SIMD width of 32. Use workgroup sizes that are multiples of 32 for best occupancy.

The grass compute shader uses `local_size_x = 8, local_size_y = 8` (64 threads) which is good.

```glsl
// Good - 64 threads, multiple of 32
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Also good alternatives:
// layout(local_size_x = 16, local_size_y = 4, local_size_z = 1) in;  // 64
// layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;  // 32
```

### Constraint 4: Geometry Shaders (Not Used)

The grass system does **not** use geometry shaders, which is good because MoltenVK emulates them with significant overhead. All blade geometry is generated in the vertex shader via vertex pulling.

### Constraint 5: Floating-Point Image Atomics (Not Used)

The displacement buffer uses `imageStore` but not atomic operations on floats. This is fine - `imageStore` is fully supported.

If you wanted to accumulate displacement from multiple sources atomically, you'd need to use the workarounds from the MoltenVK constraints doc (fixed-point atomics or shared memory accumulation). The current design avoids this by updating displacement in a compute shader that processes sources sequentially.

### Constraint 6: Memory Considerations

**Apple Silicon Unified Memory:** M1/M2/M3 chips have unified memory shared between CPU and GPU. This means:

- Large buffers (8MB instance buffer) share system RAM
- CPU readback for stealth detection is relatively cheap
- Memory bandwidth is good, but still profile texture-heavy operations

**Recommendation:** Monitor total memory usage. The grass system's ~10MB footprint is reasonable, but combined with shadow maps, probe data, and scene geometry, you may need to budget carefully on devices with limited RAM (e.g., older iOS devices).

### MoltenVK Grass Compatibility Checklist

Before implementation, verify these features are available:

- [x] Compute shaders - Always supported
- [x] Storage buffers with atomic uint - Always supported
- [x] Indirect draw commands - Always supported
- [x] 2D texture sampling with `textureGather` - Always supported
- [x] Storage images (`imageLoad`/`imageStore`) - Always supported
- [ ] Subgroup operations - Query at runtime, provide fallback
- [ ] Async compute benefit - Profile on target hardware

### Shader Precision

Metal defaults to 32-bit floats. The `mediump` and `lowp` GLSL qualifiers are ignored. This means:

- Grass parameter calculations are always high precision (good for quality)
- No performance benefit from reduced precision qualifiers
- Hash functions and noise will produce consistent results

### Testing Recommendations

1. **Test on both Apple Silicon and Intel Macs** if supporting older hardware
2. **Profile compute shader dispatch times** - Metal's shader compilation is fast but first-run may have a hitch
3. **Verify subgroup support** before enabling those optimizations
4. **Check memory usage** on lower-end iOS devices if targeting mobile
