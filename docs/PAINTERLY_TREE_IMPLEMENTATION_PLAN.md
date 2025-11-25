# Painterly Tree Rendering Implementation Plan

A step-by-step guide to implementing stylized, hand-painted looking trees inspired by The Witness and the Starboard devlog. Adapted for this Vulkan game engine.

## Table of Contents

1. [Design Goals](#design-goals)
2. [Phase 1: Leaf Clump Geometry](#phase-1-leaf-clump-geometry)
3. [Phase 2: Normal Editing for Soft Lighting](#phase-2-normal-editing-for-soft-lighting)
4. [Phase 3: Back-Face Rendering](#phase-3-back-face-rendering)
5. [Phase 4: Edge Fade (Perpendicular Polygon Dissolve)](#phase-4-edge-fade-perpendicular-polygon-dissolve)
6. [Phase 5: Shadow System](#phase-5-shadow-system)
7. [Phase 6: Interior Clump Shading](#phase-6-interior-clump-shading)
8. [Phase 7: Wind Animation](#phase-7-wind-animation)
9. [Phase 8: LOD System](#phase-8-lod-system)
10. [Phase 9: Instancing and Batching](#phase-9-instancing-and-batching)
11. [Implementation Order](#implementation-order)
12. [Appendix A: Vulkan Resource Checklist](#appendix-a-vulkan-resource-checklist)
13. [Appendix B: MoltenVK Compatibility](#appendix-b-moltenvk-compatibility)

---

## Design Goals

### The Painterly Aesthetic

The goal is trees that look hand-painted rather than photorealistic. Key characteristics:

- **Soft, fluffy silhouettes** - No harsh polygon edges visible
- **Coherent lighting** - Light appears to wrap around leaf clumps as volumes, not individual polygons
- **Stylized shadows** - Smooth shadows that suggest form without revealing geometry
- **Natural movement** - Gentle swaying that feels organic

### Why Not Standard Tree Rendering?

Traditional game trees use alpha-tested leaf cards with per-polygon normals. This creates problems:

**Harsh Edges:** When leaf polygons are nearly perpendicular to the camera, they appear as thin lines. The alpha cutoff creates jagged silhouettes.

**Noisy Lighting:** Each polygon has its own normal, so adjacent leaves catch light differently. The tree surface "sparkles" as the camera moves.

**Shadow Artifacts:** Self-shadowing between individual leaf polygons creates a chaotic, noisy shadow pattern inside the canopy.

### The Solution: Volumetric Leaf Clumps

Instead of treating each leaf polygon independently, we treat groups of leaves as volumetric clumps:

- Normals are edited to face outward from clump centers (spherical distribution)
- Shadows are cast by invisible proxy geometry, not the leaves themselves
- Edges are faded based on view angle to maintain soft silhouettes
- Interior shading darkens the clump center for depth

This approach comes from The Witness and has been refined by indie developers like the Starboard creator.

---

## Phase 1: Leaf Clump Geometry

### 1.1 Tree Structure

A tree consists of:

- **Trunk:** Standard mesh, rendered normally
- **Branches:** Standard mesh, can share material with trunk
- **Leaf Clumps:** Groups of leaf card polygons, each clump treated as a unit

Each leaf clump is a collection of flat quads (leaf cards) arranged to fill a roughly spherical volume.

### 1.2 Leaf Card Texture

The leaf texture is not a single leaf but a scattered collection of individual leaves with alpha transparency:

```
+---------------------------+
|   🍃      🍃    🍃       |
|      🍃       🍃    🍃   |
|   🍃    🍃         🍃    |
|      🍃    🍃   🍃       |
+---------------------------+
```

**Texture Requirements:**

- RGB: Leaf color (can be grayscale for palette mapping)
- Alpha: Leaf shapes with soft edges
- Resolution: 256x256 or 512x512 is sufficient
- Format: RGBA8_UNORM

**Why Scattered Leaves:**

A single large leaf shape creates obvious repetition. Scattered small leaves break up the pattern and create a more organic feel when cards overlap.

### 1.3 Leaf Card Arrangement

Within each clump, leaf cards are arranged:

- **Random positions** within the clump volume
- **Random rotations** around Y axis
- **Slight random tilts** (but not too extreme)
- **Facing roughly outward** from clump center

The goal is to fill the volume without obvious patterns while ensuring good coverage from all viewing angles.

### 1.4 Clump Metadata

Each clump needs metadata for shading:

```cpp
struct LeafClump {
    glm::vec3 center;        // World-space center of clump
    float radius;            // Approximate clump radius
    uint32_t vertexOffset;   // Start of this clump's vertices
    uint32_t vertexCount;    // Number of vertices in clump
};
```

This metadata is stored per-tree and used by the vertex shader for normal calculation and the fragment shader for interior shading.

---

## Phase 2: Normal Editing for Soft Lighting

This is the most important technique for achieving the painterly look.

### 2.1 The Problem with Per-Polygon Normals

Standard leaf cards have normals perpendicular to the card surface. When lit:

- Cards facing the sun are bright
- Cards facing away are dark
- Adjacent cards at different angles have dramatically different brightness
- The result is a noisy, sparkling appearance

### 2.2 The Solution: Spherical Normals

Instead of using the polygon's actual normal, we calculate normals as if the clump were a sphere:

```glsl
// In vertex shader
vec3 clumpCenter = /* from clump metadata */;
vec3 toVertex = vertexPosition - clumpCenter;
vec3 sphericalNormal = normalize(toVertex);

// Use this for lighting instead of the polygon normal
outNormal = sphericalNormal;
```

Now all vertices on the outer surface of a clump have normals pointing outward, as if the clump were a solid sphere. Light wraps around smoothly.

### 2.3 Hybrid Normal Approach

Pure spherical normals can look too smooth, losing the leafy character. A hybrid approach blends:

```glsl
vec3 polygonNormal = /* actual surface normal */;
vec3 sphericalNormal = normalize(vertexPosition - clumpCenter);

// Blend factor: 0.0 = pure polygon, 1.0 = pure spherical
float normalBlend = 0.7; // Tunable per tree type
vec3 finalNormal = normalize(mix(polygonNormal, sphericalNormal, normalBlend));
```

Higher blend values create softer, more volumetric lighting. Lower values retain more surface detail.

### 2.4 Storing Clump Center

The vertex shader needs to know which clump each vertex belongs to. Options:

**Option A: Per-Vertex Attribute**

Store clump center directly in vertex data:

```cpp
struct LeafVertex {
    glm::vec3 position;
    glm::vec2 texCoord;
    glm::vec3 clumpCenter;  // Redundant but simple
    float clumpRadius;
};
```

Pros: Simple, no indirection
Cons: Redundant data (all vertices in a clump store the same center)

**Option B: Clump Index + Buffer Lookup**

Store a clump index per vertex, look up center in a buffer:

```cpp
struct LeafVertex {
    glm::vec3 position;
    glm::vec2 texCoord;
    uint32_t clumpIndex;
};

// Separate buffer
struct ClumpData {
    glm::vec4 centerAndRadius; // xyz = center, w = radius
};
```

Pros: Less vertex data
Cons: Requires buffer lookup in shader

For simplicity, Option A is recommended unless memory is tight.

---

## Phase 3: Back-Face Rendering

### 3.1 The Problem

Leaf cards are flat polygons. By default, the back face is culled (not rendered). This means:

- Looking at the tree from one side, you see leaves
- Looking from the other side, those same leaves are invisible
- The tree appears sparse from certain angles

### 3.2 The Solution

Disable back-face culling for leaf geometry:

```cpp
VkPipelineRasterizationStateCreateInfo rasterizer = {};
rasterizer.cullMode = VK_CULL_MODE_NONE; // Render both sides
```

### 3.3 Normal Handling for Back Faces

When rendering the back face, the normal should be flipped:

```glsl
// In fragment shader
vec3 normal = gl_FrontFacing ? inNormal : -inNormal;
```

With spherical normals, this is less critical (the normal already points outward from clump center), but it's still good practice for correct lighting.

### 3.4 Two-Sided Lighting Considerations

Back faces receive light from the opposite direction. For leaves, this creates a natural translucency effect - light hitting the back of a leaf partially illuminates the front.

This can be enhanced with subsurface scattering approximation (see Phase 6).

---

## Phase 4: Edge Fade (Perpendicular Polygon Dissolve)

### 4.1 The Problem

When a leaf card is nearly perpendicular to the camera (edge-on), it appears as a thin line. This creates:

- Harsh, unnatural edges in the tree silhouette
- Flickering as cards rotate in/out of edge-on view
- Obvious polygon structure

### 4.2 The Solution: View-Angle Alpha Fade

Calculate the angle between the polygon normal and the view direction. Fade out polygons as they approach perpendicular:

```glsl
// In fragment shader
vec3 viewDir = normalize(cameraPosition - worldPosition);
vec3 faceNormal = /* polygon's actual normal, not spherical normal */;

// Absolute value because we render both sides
float viewDot = abs(dot(faceNormal, viewDir));

// viewDot = 1.0 when facing camera, 0.0 when edge-on
// Fade out as we approach edge-on
float edgeFade = smoothstep(0.0, 0.3, viewDot);

// Apply to alpha
float finalAlpha = textureAlpha * edgeFade;

if (finalAlpha < 0.1) discard;
```

### 4.3 Tuning the Fade

The `smoothstep(0.0, 0.3, viewDot)` parameters control the fade:

- First parameter (0.0): Angle at which polygon is fully transparent
- Second parameter (0.3): Angle at which polygon is fully opaque

Smaller range = more aggressive fade, softer silhouettes
Larger range = less fade, more visible individual cards

The Starboard devlog suggests starting with a small fade angle and increasing until the tree doesn't disappear entirely.

### 4.4 Important: Use Polygon Normal, Not Spherical Normal

The edge fade must use the actual polygon surface normal, not the spherical lighting normal. We want to fade based on whether we're seeing the card edge-on, regardless of how we're lighting it.

```glsl
// Lighting uses spherical normal
vec3 lightingNormal = sphericalNormal;

// Edge fade uses actual polygon normal
vec3 fadeNormal = polygonNormal;
```

This requires passing both normals from vertex to fragment shader (or reconstructing polygon normal from derivatives).

---

## Phase 5: Shadow System

### 5.1 The Problem with Leaf Shadows

If leaf cards cast shadows onto each other:

- Interior of tree becomes chaotically dark
- Shadow pattern is noisy and distracting
- Shadows flicker as leaves animate
- Reveals polygon structure

### 5.2 The Shadow Proxy Solution

Instead of leaf cards casting shadows, use invisible proxy geometry:

1. **Leaf clumps do NOT cast shadows** (disable shadow casting)
2. **Shadow proxy spheres DO cast shadows** (invisible in main pass)
3. **Proxy spheres rotate with the sun** to always cast clean shadows

The shadow proxy is a simple sphere (or ellipsoid) positioned at each clump center, sized to match the clump.

### 5.3 Implementation

**Main Render Pass:**
- Render leaves with shadows OFF (they receive shadows but don't cast)
- Trunk and branches cast shadows normally

**Shadow Map Pass:**
- Render trunk and branches
- Render shadow proxy spheres (one per leaf clump)
- Do NOT render actual leaf geometry

```cpp
// When recording shadow pass
for (auto& tree : trees) {
    // Render trunk/branches
    drawTrunkGeometry(tree);

    // Render shadow proxies (spheres at clump centers)
    for (auto& clump : tree.leafClumps) {
        drawShadowProxy(clump.center, clump.radius);
    }

    // Do NOT draw leaf cards
}
```

### 5.4 Dynamic Shadow Proxy Rotation

For more accurate shadows, proxy geometry can be oriented based on sun direction:

```glsl
// Shadow proxy vertex shader
vec3 sunDir = normalize(lightPosition - clumpCenter);

// Flatten the sphere slightly in the sun direction
// This prevents overly round shadows
vec3 offset = vertexPosition - clumpCenter;
float sunAlignment = dot(normalize(offset), sunDir);
offset *= mix(1.0, 0.7, abs(sunAlignment)); // Flatten along sun axis

gl_Position = lightSpaceMatrix * vec4(clumpCenter + offset, 1.0);
```

This creates shadows that match the clump shape from the sun's perspective.

### 5.5 Receiving Shadows

Leaves should still receive shadows from:

- Other trees
- The trunk and branches of their own tree
- Buildings, terrain, etc.

In the fragment shader, sample the shadow map normally.

---

## Phase 6: Interior Clump Shading

### 6.1 The Goal

Real tree canopies are darker inside. Light penetrates the outer leaves but the interior is shadowed. We approximate this with a depth-based darkening.

### 6.2 Distance from Clump Center

Use the distance from the vertex to the clump center to darken interior regions:

```glsl
float distToCenter = length(worldPosition - clumpCenter);
float normalizedDist = distToCenter / clumpRadius;

// Interior darkening: closer to center = darker
float interiorShadow = smoothstep(0.0, 0.8, normalizedDist);

// Apply to lighting
vec3 shadedColor = baseColor * mix(interiorDarkness, 1.0, interiorShadow);
```

Where `interiorDarkness` is a tunable value (e.g., 0.3 for significant darkening).

### 6.3 Color Shift for Shadows

The Starboard devlog mentions a "bluish color for parts in shadow." This mimics ambient sky light:

```glsl
vec3 shadowColor = vec3(0.4, 0.5, 0.7); // Bluish
vec3 litColor = leafColor;

// Blend based on interior shadow and direct shadow
float totalShadow = min(interiorShadow, directShadow);
vec3 finalColor = mix(shadowColor * leafColor, litColor, totalShadow);
```

### 6.4 Subsurface Scattering Approximation

Leaves are translucent. Light hitting the back of a leaf partially transmits through. A simple approximation:

```glsl
// Wrap lighting: allows some light from behind
float NdotL = dot(normal, lightDir);
float wrap = 0.5;
float wrappedDiffuse = max(0.0, (NdotL + wrap) / (1.0 + wrap));

// Subsurface term: light from behind the leaf
float subsurface = max(0.0, dot(-viewDir, lightDir)) * 0.3;

// Warm the subsurface light (light filtering through leaf)
vec3 subsurfaceColor = lightColor * vec3(1.0, 0.9, 0.5) * subsurface;

vec3 finalLighting = wrappedDiffuse * lightColor + subsurfaceColor;
```

---

## Phase 7: Wind Animation

### 7.1 Design Principles

Tree wind animation should be:

- **Layered:** Whole tree sways, branches sway on top of that, leaves rustle on top of that
- **Asynchronous:** Different trees and different branches have different phases
- **Subtle:** Gentle movement, not a hurricane

### 7.2 Trunk/Branch Sway

The trunk and large branches sway slowly:

```glsl
// Vertex shader for trunk/branches
float swayAmount = sin(time * 0.5 + treeHash * 6.28) * 0.02;
float heightFactor = (position.y - treeBase.y) / treeHeight;

vec3 swayOffset = windDirection * swayAmount * heightFactor * heightFactor;
position += swayOffset;
```

The `heightFactor * heightFactor` makes higher parts sway more.

### 7.3 Leaf Rustle

Leaves have faster, smaller movement on top of the branch sway:

```glsl
// Vertex shader for leaves
// First apply branch sway (same as above but for clump center)
vec3 swayedClumpCenter = clumpCenter + branchSwayOffset;

// Then add leaf rustle
float rustlePhase = time * 3.0 + vertexHash * 6.28;
vec3 rustleOffset = vec3(
    sin(rustlePhase) * 0.01,
    sin(rustlePhase * 1.3) * 0.005,
    cos(rustlePhase * 0.7) * 0.01
);

position += swayedClumpCenter - clumpCenter + rustleOffset;
```

### 7.4 Wind Texture (Optional)

For spatial wind variation (gusts moving across the landscape), sample a scrolling noise texture:

```glsl
vec2 windUV = worldPosition.xz * 0.1 + windDirection * time * windSpeed;
float windStrength = texture(windNoiseTexture, windUV).r;

swayAmount *= windStrength;
```

This creates visible waves of wind passing through trees, matching the grass system if implemented.

---

## Phase 8: LOD System

### 8.1 LOD Levels

Trees need multiple detail levels:

**LOD 0 (Near):** Full detail
- All leaf cards rendered
- Edge fade active
- Full lighting model

**LOD 1 (Medium):** Reduced detail
- Fewer leaf cards (every other card)
- Simplified lighting
- Larger edge fade threshold

**LOD 2 (Far):** Billboard or impostor
- Single textured quad or cross
- Pre-rendered from multiple angles
- No individual leaf cards

**LOD 3 (Very Far):** Terrain texture
- Tree represented only in terrain color
- No geometry

### 8.2 LOD Transition

Smooth transitions prevent popping:

**Alpha-Based Transition:**

```glsl
// In fragment shader
float lodFade = smoothstep(lodDistances[currentLOD], lodDistances[currentLOD + 1], distToCamera);

// Fade out current LOD as we approach next LOD distance
finalAlpha *= 1.0 - lodFade;
```

Render both LOD levels during transition, with alpha fading.

**Dithered Transition:**

```glsl
// Screen-space dither pattern
float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);

if (lodFade > dither) discard;
```

This avoids the cost of rendering two LODs simultaneously.

### 8.3 Impostor Generation

For LOD 2 billboards, pre-render the tree from multiple angles:

- 8-16 angles around the tree
- Store in a texture atlas
- At runtime, select the two nearest angles and blend

```glsl
float viewAngle = atan(viewDir.z, viewDir.x);
int impostorIndex = int(viewAngle / (2.0 * PI) * numImpostors);
vec2 impostorUV = calculateImpostorUV(texCoord, impostorIndex);
```

---

## Phase 9: Instancing and Batching

### 9.1 Tree Instancing

Trees of the same type can be rendered with GPU instancing:

```cpp
struct TreeInstance {
    glm::mat4 transform;      // Position, rotation, scale
    float treeHash;           // For wind phase variation
    uint32_t treeTypeIndex;   // Index into tree type parameters
};
```

Use `vkCmdDrawIndexed` with instance count, or `vkCmdDrawIndexedIndirect` for GPU-driven culling.

### 9.2 Frustum Culling

CPU-side frustum culling before rendering:

```cpp
std::vector<TreeInstance> visibleTrees;
for (auto& tree : allTrees) {
    if (frustum.intersects(tree.boundingSphere)) {
        visibleTrees.push_back(tree.instanceData);
    }
}

// Upload visibleTrees to instance buffer
```

For large forests, use spatial data structures (quadtree, octree) for efficient culling.

### 9.3 GPU Culling (Advanced)

Similar to grass, a compute shader can cull trees:

```glsl
void main() {
    uint treeIndex = gl_GlobalInvocationID.x;
    TreeData tree = trees[treeIndex];

    // Frustum test
    if (!isInFrustum(tree.boundingSphere)) return;

    // Distance test for LOD
    float dist = length(tree.position - cameraPosition);
    if (dist > maxTreeDistance) return;

    // Emit to visible buffer
    uint idx = atomicAdd(visibleCount, 1);
    visibleTrees[idx] = tree;
}
```

---

## Implementation Order

### Milestone 1: Basic Tree Rendering

**Goal:** Get a tree on screen with standard rendering.

1. Import or create a tree model with trunk, branches, and leaf cards
2. Render with basic texturing and lighting
3. **Verify:** Tree is visible, textured, lit by sun

### Milestone 2: Spherical Normals

**Goal:** Soft, volumetric lighting on leaf clumps.

1. Add clump center data to leaf vertices
2. Calculate spherical normals in vertex shader
3. Implement normal blending parameter
4. **Verify:** Leaf lighting is smooth, no sparkle. Adjust blend factor.

### Milestone 3: Back-Face Rendering

**Goal:** Tree looks full from all angles.

1. Disable back-face culling for leaf geometry
2. Handle normal flipping in fragment shader
3. **Verify:** Rotate around tree, density is consistent from all angles

### Milestone 4: Edge Fade

**Goal:** Soft silhouettes, no harsh edges.

1. Calculate view angle in fragment shader
2. Implement alpha fade based on angle
3. Tune fade parameters
4. **Verify:** No thin polygon lines visible at edges. Tree doesn't disappear.

### Milestone 5: Shadow Proxy System

**Goal:** Clean shadows without interior noise.

1. Create shadow proxy geometry (spheres at clump centers)
2. Render proxies in shadow pass, skip leaves
3. Leaves receive shadows but don't cast
4. **Verify:** Tree casts smooth shadow on ground. Interior isn't chaotically dark.

### Milestone 6: Interior Shading

**Goal:** Depth and volume in leaf clumps.

1. Implement distance-from-center darkening
2. Add blue color shift for shadowed areas
3. Add subsurface scattering approximation
4. **Verify:** Clumps have visible depth. Backlighting glows warmly.

### Milestone 7: Wind Animation

**Goal:** Gentle, natural movement.

1. Implement trunk/branch sway
2. Add leaf rustle on top
3. Hash-based phase variation per tree
4. Optional: wind texture for spatial variation
5. **Verify:** Trees sway gently. Different trees move differently.

### Milestone 8: LOD System

**Goal:** Performance at distance.

1. Create reduced-detail leaf geometry for LOD 1
2. Generate impostor textures for LOD 2
3. Implement LOD transitions
4. **Verify:** Distant trees are low-poly. Transitions aren't jarring.

### Milestone 9: Instancing

**Goal:** Many trees efficiently.

1. Set up instance buffer and instanced draw calls
2. Implement CPU frustum culling
3. Optional: GPU culling compute shader
4. **Verify:** Forest of 1000 trees renders efficiently

---

## Summary: What You Can See After Each Milestone

| Milestone | What's Visible |
|-----------|----------------|
| 1 | Basic tree on screen |
| 2 | **Soft, volumetric leaf lighting** |
| 3 | Full tree from all angles |
| 4 | **Soft silhouettes, no hard edges** |
| 5 | Clean ground shadows |
| 6 | Depth in leaf clumps, nice backlighting |
| 7 | Natural wind animation |
| 8 | Performance at distance |
| 9 | Many trees efficiently |

**Key insight:** Milestones 2 and 4 create the painterly look. Everything else is polish and performance.

---

## Appendix A: Vulkan Resource Checklist

### Buffers

- [ ] Tree instance buffer (per-tree transforms and parameters)
- [ ] Leaf clump data buffer (centers and radii for all clumps)
- [ ] Shadow proxy vertex/index buffers

### Textures

- [ ] Leaf texture atlas (RGBA, scattered leaves)
- [ ] Optional: Color palette texture for tinting
- [ ] Optional: Wind noise texture
- [ ] Impostor texture atlas (for LOD 2)

### Pipelines

- [ ] Tree trunk/branch pipeline (standard, with shadows)
- [ ] Leaf pipeline (no culling, edge fade, spherical normals)
- [ ] Shadow proxy pipeline (shadow pass only)
- [ ] Impostor pipeline (LOD 2 billboards)

### Descriptor Sets

- [ ] Per-frame: camera, lighting, shadow maps
- [ ] Per-material: leaf texture, parameters
- [ ] Per-tree-type: clump data buffer

---

## Appendix B: MoltenVK Compatibility

The tree system uses standard features that are fully supported on MoltenVK:

| Feature | Status | Notes |
|---------|--------|-------|
| Instanced rendering | Supported | Standard `vkCmdDrawIndexed` |
| Double-sided rendering | Supported | `cullMode = NONE` |
| Alpha testing | Supported | `discard` in fragment shader |
| Shadow mapping | Supported | Standard depth comparison |
| Texture sampling | Supported | All leaf and wind textures |

### No Problematic Features Used

- No geometry shaders (all geometry is pre-built or instanced)
- No tessellation (leaf cards are static meshes)
- No compute shaders required (optional for GPU culling)
- No atomic operations required

### Performance Notes

- Apple Silicon handles instanced draws efficiently
- Alpha testing (`discard`) can reduce early-Z benefits - sort trees front-to-back if possible
- Shadow proxy spheres are simple geometry, minimal impact

---

## References

- **The Witness** - Industry-leading stylized tree rendering
- **Starboard Devlog** - Indie implementation with clear explanations
- **GPU Gems 3, Chapter 16** - "Vegetation Procedural Animation and Shading"
