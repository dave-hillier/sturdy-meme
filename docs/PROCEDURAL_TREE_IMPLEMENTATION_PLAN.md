# Procedural Tree Rendering Implementation Plan

A compute-shader-driven approach to tree rendering that extrapolates from the Ghost of Tsushima grass techniques. Trees are generated procedurally on the GPU, enabling dynamic forests that integrate seamlessly with the procedural grass system.

## Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [Phase 1: Tree Structure Representation](#phase-1-tree-structure-representation)
3. [Phase 2: Branch Generation with Bezier Curves](#phase-2-branch-generation-with-bezier-curves)
4. [Phase 3: Leaf Cluster Compute Shader](#phase-3-leaf-cluster-compute-shader)
5. [Phase 4: Voronoi-Based Canopy Distribution](#phase-4-voronoi-based-canopy-distribution)
6. [Phase 5: Unified Wind System](#phase-5-unified-wind-system)
7. [Phase 6: Color Palette and Material System](#phase-6-color-palette-and-material-system)
8. [Phase 7: Shadow Strategy](#phase-7-shadow-strategy)
9. [Phase 8: LOD Through Density](#phase-8-lod-through-density)
10. [Phase 9: Seasonal and Dynamic Effects](#phase-9-seasonal-and-dynamic-effects)
11. [Implementation Order](#implementation-order)
12. [Appendix A: Integration with Grass System](#appendix-a-integration-with-grass-system)
13. [Appendix B: Vulkan Resource Checklist](#appendix-b-vulkan-resource-checklist)
14. [Appendix C: MoltenVK Compatibility](#appendix-c-moltenvk-compatibility)

---

## Design Philosophy

### Why Procedural Trees?

The grass implementation plan describes a powerful pattern: compute shaders generate instance data, the GPU culls and outputs to a buffer, and indirect draw calls render the survivors. This same pattern can revolutionize tree rendering.

**Traditional Tree Rendering:**
- Pre-modeled trees with fixed geometry
- CPU determines which trees to render
- Each tree type needs hand-authored LOD models
- Forests feel repetitive despite randomization

**Procedural Tree Rendering:**
- Tree structure defined by parameters, not vertices
- Compute shader generates branches and leaves each frame
- Natural variation emerges from procedural rules
- Seamless integration with procedural grass aesthetic

### The Unified Field

Ghost of Tsushima's fields feel cohesive because everything follows the same wind, the same color philosophy, the same level of stylization. By generating trees with the same compute-driven approach as grass, we achieve:

- **Consistent wind response** - Trees and grass sample the same wind texture
- **Unified color palette** - Same palette mapping approach for painted aesthetic
- **Matching LOD behavior** - Density reduction rather than model swapping
- **Seamless transitions** - Grass and low tree leaves blend naturally

### Performance Model

Like grass, we consider many potential positions and cull most:

| Metric | Target |
|--------|--------|
| Leaf positions considered | ~500,000 per frame |
| Leaves rendered | ~50,000-100,000 |
| Branches rendered | ~5,000-10,000 |
| Target frame time | ~1.5ms for tree system |

---

## Phase 1: Tree Structure Representation

### 1.1 Parametric Tree Definition

Instead of storing vertices, we store parameters that describe a tree's structure:

```cpp
struct TreeDefinition {
    // Trunk parameters
    float trunkHeight;
    float trunkRadius;
    float trunkTaper;           // How much trunk narrows toward top
    float trunkBend;            // Natural lean/curve

    // Branching parameters
    uint32_t branchLevels;      // 0 = trunk only, 1 = primary branches, etc.
    float branchAngle;          // Angle from parent
    float branchSpread;         // Angular spread around parent
    float branchLengthRatio;    // Length relative to parent
    float branchRadiusRatio;    // Radius relative to parent
    uint32_t branchesPerLevel;  // How many branches at each level

    // Canopy parameters
    glm::vec3 canopyCenter;     // Offset from trunk top
    glm::vec3 canopyExtent;     // Ellipsoid radii
    float leafDensity;          // Leaves per cubic meter
    float leafSize;             // Base leaf size
    float leafSizeVariance;

    // Animation parameters
    float windInfluence;
    float branchStiffness;      // Resistance to wind (varies by thickness)

    // Visual parameters
    uint32_t leafPaletteIndex;  // Index into color palette
    uint32_t barkTextureIndex;
};
```

### 1.2 Tree Instance Data

Each placed tree has:

```cpp
struct TreeInstance {
    glm::vec3 position;         // World position of trunk base
    float rotation;             // Y-axis rotation
    float scale;                // Uniform scale
    float age;                  // 0-1, affects size and fullness
    uint32_t definitionIndex;   // Which TreeDefinition to use
    float hash;                 // Unique random seed for variation
};
```

### 1.3 Tree Placement Map

Like grass type maps, a texture defines where trees grow:

- **Tree Density Map (R8_UNORM):** 0 = no trees, 1 = maximum density
- **Tree Type Map (R8_UINT):** Index into tree definition array
- **Resolution:** Lower than grass (128x128 per terrain tile is sufficient)

The compute shader samples these to determine tree positions, similar to grass placement.

---

## Phase 2: Branch Generation with Bezier Curves

### 2.1 Branches as Bezier Curves

The grass system renders blades as bezier curves. Branches are the same concept at larger scale:

```glsl
struct BranchInstance {
    vec3 basePosition;      // Where branch connects to parent
    vec3 tipPosition;       // End of branch
    vec3 controlPoint1;     // Bezier control (curvature)
    vec3 controlPoint2;
    float baseRadius;       // Thickness at base
    float tipRadius;        // Thickness at tip (tapers)
    float hash;             // Per-branch random value
    uint parentIndex;       // Which branch this connects to (for animation)
    uint depth;             // 0 = trunk, 1 = primary branch, etc.
};
```

### 2.2 Recursive Branch Generation

A compute shader generates the branch hierarchy:

```glsl
// Phase 1: Generate trunk
void generateTrunk(uint treeIndex) {
    TreeInstance tree = trees[treeIndex];
    TreeDefinition def = definitions[tree.definitionIndex];

    vec3 basePos = tree.position;
    vec3 tipPos = basePos + vec3(0, def.trunkHeight * tree.scale, 0);

    // Add natural bend
    vec3 bendDir = hash3(tree.hash) * 2.0 - 1.0;
    bendDir.y = 0;
    bendDir = normalize(bendDir);

    vec3 ctrl1 = mix(basePos, tipPos, 0.33) + bendDir * def.trunkBend;
    vec3 ctrl2 = mix(basePos, tipPos, 0.66) + bendDir * def.trunkBend * 0.5;

    uint idx = atomicAdd(branchCount, 1);
    branches[idx] = BranchInstance(
        basePos, tipPos, ctrl1, ctrl2,
        def.trunkRadius * tree.scale,
        def.trunkRadius * tree.scale * def.trunkTaper,
        tree.hash, 0xFFFFFFFF, 0
    );
}

// Phase 2: Generate child branches from existing branches
void generateChildBranches(uint parentIdx) {
    BranchInstance parent = branches[parentIdx];
    if (parent.depth >= maxBranchDepth) return;

    TreeDefinition def = /* lookup from parent */;
    uint numChildren = def.branchesPerLevel;

    for (uint i = 0; i < numChildren; i++) {
        float t = 0.3 + hash(parent.hash + i) * 0.5; // Position along parent
        vec3 attachPoint = evaluateBezier(parent, t);

        // Direction: outward and upward from parent tangent
        vec3 parentTangent = evaluateBezierDerivative(parent, t);
        vec3 outward = generateBranchDirection(parent, i, def);

        float length = parent.length * def.branchLengthRatio;
        vec3 tipPos = attachPoint + outward * length;

        // ... calculate control points with natural curve ...

        uint idx = atomicAdd(branchCount, 1);
        branches[idx] = BranchInstance(/* ... */);
    }
}
```

### 2.3 Branch Vertex Shader

Like grass blades, branches are rendered as triangle strips along bezier curves:

```glsl
void main() {
    uint branchIndex = gl_VertexIndex / vertsPerBranch;
    uint vertIndex = gl_VertexIndex % vertsPerBranch;

    BranchInstance branch = branches[branchIndex];

    // Position along branch
    float t = float(vertIndex / 2) / float(numSegments);
    vec3 curvePos = evaluateBezier(branch, t);
    vec3 tangent = normalize(evaluateBezierDerivative(branch, t));

    // Radius interpolation with taper
    float radius = mix(branch.baseRadius, branch.tipRadius, t);

    // Radial position (circular cross-section)
    float angle = float(vertIndex % vertsPerRing) / float(vertsPerRing) * 2.0 * PI;
    vec3 radialDir = calculateRadialDirection(tangent, angle);

    vec3 worldPos = curvePos + radialDir * radius;

    // Apply wind animation (more movement at tips and thin branches)
    worldPos += calculateBranchWind(branch, t, time);

    gl_Position = viewProj * vec4(worldPos, 1.0);
}
```

### 2.4 Why Generate Branches Every Frame?

Unlike pre-modeled trees, procedural branches can:

- Respond to wind with accurate hierarchical motion
- Grow over time (age parameter)
- Be damaged or cut dynamically
- Adapt LOD by generating fewer branches at distance

The cost is offset by GPU parallelism and the ability to cull aggressively.

---

## Phase 3: Leaf Cluster Compute Shader

### 3.1 From Grass Blades to Leaves

The grass compute shader generates blade instances from a grid of positions. The leaf compute shader does the same within a tree's canopy volume:

```glsl
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

void main() {
    // 3D grid within canopy bounds
    uvec3 gridPos = gl_GlobalInvocationID;

    // Convert to position within canopy ellipsoid
    vec3 normalizedPos = vec3(gridPos) / vec3(gridSize) * 2.0 - 1.0;
    vec3 localPos = normalizedPos * canopyExtent;

    // Check if inside canopy ellipsoid
    float ellipsoidDist = length(localPos / canopyExtent);
    if (ellipsoidDist > 1.0) return;

    // Add jitter to break grid
    vec3 jitter = (hash3(localPos) - 0.5) * cellSize;
    localPos += jitter;

    vec3 worldPos = canopyCenter + localPos;

    // Distance culling
    float distToCamera = length(worldPos - cameraPosition);
    if (distToCamera > maxLeafDistance) return;

    // Frustum culling
    if (!isInFrustum(worldPos)) return;

    // Density falloff toward canopy edge (fuller in middle)
    float density = 1.0 - smoothstep(0.6, 1.0, ellipsoidDist);
    if (hash(worldPos) > density * leafDensity) return;

    // Generate leaf instance
    uint idx = atomicAdd(leafCount, 1);
    writeLeafInstance(idx, worldPos, /* ... */);
}
```

### 3.2 Leaf Instance Data

Similar to grass blade instances:

```glsl
struct LeafInstance {
    vec3 position;          // World position
    vec2 facing;            // 2D direction leaf faces
    float size;             // Leaf scale
    float hash;             // Per-leaf random
    uint treeIndex;         // Which tree this belongs to

    // Canopy-relative data
    float depthInCanopy;    // 0 = surface, 1 = deep inside
    vec3 clumpCenter;       // Voronoi clump center
    float clumpId;          // For color variation

    // Animation
    float windPhase;        // Offset for wind animation
    float flutter;          // High-frequency flutter amount
};
```

### 3.3 Leaf Geometry Options

**Option A: Grass-Style Bezier Cards**

Render leaves as small bezier curve strips, exactly like grass blades but shorter and wider:

```glsl
// Leaf vertex shader - reuse grass bezier code
vec3 p0 = leafBase;
vec3 p3 = leafBase + leafDir * leafLength;
vec3 p1 = mix(p0, p3, 0.33) + leafNormal * curl * 0.3;
vec3 p2 = mix(p0, p3, 0.66) + leafNormal * curl * 0.5;

vec3 curvePos = evaluateBezier(p0, p1, p2, p3, t);
```

**Option B: Billboard Quads**

Simple camera-facing quads with leaf texture:

```glsl
// Simpler, potentially faster for dense canopies
vec3 toCamera = normalize(cameraPosition - leafPosition);
vec3 right = normalize(cross(vec3(0,1,0), toCamera));
vec3 up = cross(toCamera, right);

vec3 offset = right * (vertexUV.x - 0.5) + up * (vertexUV.y - 0.5);
vec3 worldPos = leafPosition + offset * leafSize;
```

**Option C: Hybrid**

Near leaves use bezier geometry for quality; far leaves use billboards for performance.

### 3.4 Integration with Branch Tips

Leaves should cluster near branch tips. The compute shader can reference generated branches:

```glsl
// For each potential leaf position, find nearest branch tip
float minDistToBranch = 1000.0;
vec3 nearestTip;

for (uint i = 0; i < branchCount; i++) {
    BranchInstance branch = branches[i];
    if (branch.depth < 2) continue; // Only leaf-bearing branches

    float dist = length(worldPos - branch.tipPosition);
    if (dist < minDistToBranch) {
        minDistToBranch = dist;
        nearestTip = branch.tipPosition;
    }
}

// Leaves cluster near branch tips
float branchProximity = 1.0 - smoothstep(0.0, maxLeafDist, minDistToBranch);
if (hash(worldPos + vec3(100)) > branchProximity) return;
```

This creates natural leaf distribution following the branch structure.

---

## Phase 4: Voronoi-Based Canopy Distribution

### 4.1 Reusing the Grass Clumping System

The grass system uses Voronoi cells to create natural clumping. Apply the same to tree canopies:

```glsl
void calculateLeafClump(vec3 worldPos, float clumpScale,
                        out float clumpId, out float clumpDist, out vec3 clumpCenter) {
    // Same algorithm as grass, but in 3D
    vec3 scaledPos = worldPos / clumpScale;
    vec3 cellId = floor(scaledPos);

    float minDist = 10.0;
    vec3 nearestPoint;
    float nearestId;

    // Check 3x3x3 neighborhood
    for (int z = -1; z <= 1; z++) {
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                vec3 neighborCell = cellId + vec3(x, y, z);
                vec3 jitter = hash3(neighborCell) * 0.8 + 0.1;
                vec3 clumpPoint = (neighborCell + jitter) * clumpScale;

                float dist = length(worldPos - clumpPoint);
                if (dist < minDist) {
                    minDist = dist;
                    nearestPoint = clumpPoint;
                    nearestId = hash(neighborCell);
                }
            }
        }
    }

    clumpId = nearestId;
    clumpDist = minDist;
    clumpCenter = nearestPoint;
}
```

### 4.2 Clump Influence on Leaves

Like grass, clumps affect leaf properties:

- **Size:** Leaves in the same clump have similar sizes
- **Color:** Clump ID maps to color palette for subtle variation
- **Orientation:** Leaves can face toward/away from clump center
- **Density:** Some clumps are denser than others

```glsl
// In leaf compute shader
float clumpSizeMod = mix(1.0, 0.7 + clumpId * 0.6, clumpSizeInfluence);
float finalSize = baseLeafSize * clumpSizeMod * (1.0 + (leafHash - 0.5) * sizeVariance);

// Facing influenced by clump
vec3 toClumpCenter = normalize(clumpCenter - worldPos);
vec3 randomFacing = normalize(hash3(worldPos) - 0.5);
vec3 facing = normalize(mix(randomFacing, toClumpCenter, clumpFacingInfluence));
```

### 4.3 Multi-Scale Clumping

Trees benefit from multiple clump scales:

- **Large clumps (1-2m):** Major light/dark regions in canopy
- **Medium clumps (0.3-0.5m):** Visible clusters of leaves
- **Small clumps (0.1m):** Fine variation

```glsl
float largeClumpId, medClumpId, smallClumpId;
// ... calculate each scale ...

// Combine for final color variation
float combinedClumpId = largeClumpId * 0.5 + medClumpId * 0.3 + smallClumpId * 0.2;
```

---

## Phase 5: Unified Wind System

### 5.1 Sharing Wind with Grass

The grass system samples a scrolling Perlin noise texture for wind. Trees use the same:

```glsl
// Same wind sampling as grass
float sampleWind(vec2 worldPosXZ, float time) {
    vec2 scrolledPos = worldPosXZ - windDirection * time * windSpeed;

    float noise = 0.0;
    float amplitude = 1.0;
    float frequency = 0.1;

    for (int i = 0; i < 2; i++) {
        noise += perlinNoise(scrolledPos * frequency) * amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return noise * 0.5 + 0.5;
}
```

### 5.2 Hierarchical Wind Response

Trees have a hierarchy of motion that grass doesn't:

```
Trunk sways slowly
  └─ Primary branches sway on top of trunk
       └─ Secondary branches sway on top of primary
            └─ Leaves flutter on top of secondary
```

Each level adds motion to its children:

```glsl
// Branch wind calculation
vec3 calculateBranchWind(BranchInstance branch, float t, float time) {
    // Get wind at this position
    float windStrength = sampleWind(branch.basePosition.xz, time);

    // Stiffness based on branch thickness
    float stiffness = branch.baseRadius / maxBranchRadius;
    float sway = windStrength * (1.0 - stiffness) * maxBranchSway;

    // Movement increases along branch (tip moves more)
    sway *= t * t;

    // Primary sway direction
    vec3 swayOffset = vec3(windDirection.x, 0, windDirection.y) * sway;

    // Add secondary oscillation perpendicular to wind
    float perpPhase = time * 2.0 + branch.hash * 6.28;
    vec3 perpDir = vec3(-windDirection.y, 0, windDirection.x);
    swayOffset += perpDir * sin(perpPhase) * sway * 0.3;

    return swayOffset;
}

// Leaf wind includes branch motion plus flutter
vec3 calculateLeafWind(LeafInstance leaf, float time) {
    // Inherit motion from nearest branch
    vec3 branchMotion = getBranchMotionAtPoint(leaf.position, time);

    // Add high-frequency flutter
    float flutterPhase = time * 8.0 + leaf.windPhase;
    vec3 flutter = vec3(
        sin(flutterPhase),
        sin(flutterPhase * 1.3) * 0.5,
        cos(flutterPhase * 0.7)
    ) * leaf.flutter * windStrength;

    return branchMotion + flutter;
}
```

### 5.3 Wind Gusts Propagating Through Forest

Because trees and grass sample the same wind texture, gusts visibly travel across the landscape:

1. Wind gust appears in grass
2. Same gust reaches nearby tree (grass and tree move together)
3. Gust continues through forest, tree by tree
4. Creates cohesive, natural wind behavior

This is the key benefit of the unified system.

---

## Phase 6: Color Palette and Material System

### 6.1 The Painted Look

Ghost of Tsushima's grass uses a color palette texture where:

- U axis = clump ID (which color variant)
- V axis = position along blade (base to tip gradient)

Trees use the same approach:

```glsl
// Leaf fragment shader
vec2 paletteUV;
paletteUV.x = clumpId; // Horizontal = color variant
paletteUV.y = depthInCanopy; // Vertical = surface to interior gradient

vec3 leafColor = texture(leafPalette, paletteUV).rgb;
```

### 6.2 Palette Design for Trees

The tree color palette encodes:

**Horizontal Variation (clump-based):**
- Subtle hue shifts (yellow-green to blue-green)
- Saturation variation
- Value (brightness) variation

**Vertical Gradient (depth-based):**
- Top (V=0): Brighter, sun-lit leaves
- Bottom (V=1): Darker, shadowed interior leaves

```
Palette Texture (256x64):
+--------------------------------------------------+
| Light yellow-green | Light green | Light blue-green | (surface)
| ...                | ...         | ...              |
| Dark yellow-green  | Dark green  | Dark blue-green  | (interior)
+--------------------------------------------------+
     Clump 0            Clump 0.5       Clump 1.0
```

### 6.3 Seasonal Color Variation

The palette approach makes seasons trivial:

```cpp
// Just swap the palette texture
if (season == AUTUMN) {
    bindTexture(autumnLeafPalette);
} else if (season == SPRING) {
    bindTexture(springLeafPalette);
}
```

Autumn palette: oranges, reds, yellows
Spring palette: light greens, hints of pink (blossoms)
Winter: no leaves (or sparse brown)

### 6.4 Translucency

Like grass, leaves are translucent:

```glsl
// Leaf fragment shader
float NdotL = dot(normal, lightDir);
float frontLight = max(0.0, NdotL);

// Light from behind transmits through leaf
float backLight = max(0.0, -NdotL) * translucency;

// Warm the transmitted light (chlorophyll filtering)
vec3 translucentColor = leafColor * vec3(1.2, 1.1, 0.5);

vec3 finalColor = leafColor * frontLight * lightColor
                + translucentColor * backLight * lightColor;
```

### 6.5 Bark Material

Branches use a simpler material:

```glsl
// Branch fragment shader
vec2 barkUV;
barkUV.x = radialAngle / (2.0 * PI); // Around the branch
barkUV.y = distanceAlongBranch;       // Along the branch

vec3 barkColor = texture(barkTexture, barkUV * barkTiling).rgb;

// Darken in crevices (using normal variation)
float ao = 1.0 - abs(dot(normal, branchTangent)) * 0.3;
barkColor *= ao;
```

---

## Phase 7: Shadow Strategy

### 7.1 Reusing the Grass Shadow Approach

The grass system uses shadow imposters - simplified geometry for shadow maps. Trees can do the same:

**Main Pass:**
- Render full branch and leaf geometry
- Receive shadows from shadow map

**Shadow Pass:**
- Render trunk and thick branches only
- Render canopy as a simple ellipsoid mesh
- Skip individual leaves entirely

### 7.2 Canopy Shadow Proxy

```glsl
// Shadow pass vertex shader for canopy proxy
void main() {
    // Simple ellipsoid geometry at canopy center
    vec3 localPos = sphereVertex * canopyExtent;
    vec3 worldPos = canopyCenter + localPos;

    gl_Position = lightSpaceMatrix * vec4(worldPos, 1.0);
}

// Shadow pass fragment shader - dithered for soft edges
void main() {
    // Dither based on distance from ellipsoid surface
    float distFromSurface = /* calculate */;
    float dither = bayerDither(gl_FragCoord.xy);

    // Soft edge on shadow
    if (distFromSurface > dither * shadowSoftness) {
        discard;
    }
}
```

### 7.3 Self-Shadowing Approximation

Instead of actual leaf self-shadowing, use depth-in-canopy:

```glsl
// Leaf fragment shader
float selfShadow = mix(1.0, 0.4, depthInCanopy);
finalColor *= selfShadow;
```

Leaves deep in the canopy are darker, approximating occlusion from outer leaves.

### 7.4 Branch Shadows

Thick branches (trunk, primary branches) cast shadows normally. Thin branches and leaves do not cast individual shadows.

```cpp
void renderShadowPass() {
    // Render trunk and primary branches
    for (auto& branch : branches) {
        if (branch.depth <= 1 && branch.baseRadius > minShadowRadius) {
            drawBranch(branch);
        }
    }

    // Render canopy shadow proxies
    for (auto& tree : trees) {
        drawCanopyShadowProxy(tree);
    }
}
```

---

## Phase 8: LOD Through Density

### 8.1 The Grass LOD Approach Applied to Trees

Grass doesn't swap models at distance; it reduces blade density. Trees do the same:

**Near (0-50m):**
- Full branch depth (all levels generated)
- Full leaf density
- Bezier leaf geometry

**Medium (50-150m):**
- Reduced branch depth (skip tertiary branches)
- 50% leaf density
- Billboard leaves

**Far (150-300m):**
- Trunk and primary branches only
- 25% leaf density
- Larger billboard leaves

**Very Far (300m+):**
- Billboard impostor or no geometry
- Represented in terrain texture

### 8.2 Density Reduction in Compute

```glsl
// In leaf compute shader
float distToCamera = length(worldPos - cameraPosition);

// Density multiplier based on distance
float densityMult = 1.0;
if (distToCamera > 50.0) densityMult = 0.5;
if (distToCamera > 150.0) densityMult = 0.25;

// Probabilistic culling
if (hash(worldPos) > baseDensity * densityMult) return;

// Also reduce at distance based on screen-space size
float screenSize = leafSize / distToCamera;
if (screenSize < minScreenSize) return;
```

### 8.3 Branch LOD

Branch generation respects distance:

```glsl
// In branch generation compute shader
float distToCamera = length(branchBase - cameraPosition);

// Maximum branch depth decreases with distance
uint maxDepthForDistance = 4;
if (distToCamera > 50.0) maxDepthForDistance = 3;
if (distToCamera > 100.0) maxDepthForDistance = 2;
if (distToCamera > 200.0) maxDepthForDistance = 1;

if (currentDepth >= maxDepthForDistance) return;
```

### 8.4 Smooth Transitions

Like grass, use probabilistic fade to avoid popping:

```glsl
float lodTransition = smoothstep(lodStart, lodEnd, distToCamera);

// Some leaves disappear earlier based on hash
if (leafHash < lodTransition) return;
```

---

## Phase 9: Seasonal and Dynamic Effects

### 9.1 Leaf Fall Animation

Because leaves are generated each frame, we can animate them falling:

```glsl
// Autumn leaf behavior
if (season == AUTUMN) {
    // Some leaves are "falling" based on hash and time
    float fallProbability = (time - autumnStart) / autumnDuration;

    if (leafHash < fallProbability) {
        // This leaf is falling
        float fallTime = (time - autumnStart) - leafHash * autumnDuration;

        // Spiral down
        vec3 fallOffset = vec3(
            sin(fallTime * 2.0) * 0.5,
            -fallTime * fallSpeed,
            cos(fallTime * 2.0) * 0.5
        );

        worldPos += fallOffset;

        // Hit ground = despawn
        if (worldPos.y < groundHeight) return;
    }
}
```

### 9.2 Growth Over Time

The `age` parameter in TreeInstance enables growth:

```glsl
// Scale everything by age
float ageFactor = tree.age; // 0 = sapling, 1 = mature

float effectiveHeight = def.trunkHeight * ageFactor;
float effectiveCanopySize = def.canopyExtent * pow(ageFactor, 1.5);
float effectiveLeafDensity = def.leafDensity * ageFactor;
```

A newly planted tree starts small and grows over game time.

### 9.3 Damage and Destruction

Procedural trees can respond to damage:

```cpp
struct TreeDamage {
    float healthPercent;      // 1.0 = healthy, 0.0 = dead
    glm::vec3 damageCenter;   // Where damage is concentrated
    float damageRadius;
};
```

```glsl
// In leaf compute shader
float distToDamage = length(worldPos - tree.damageCenter);
float damageInfluence = 1.0 - smoothstep(0.0, tree.damageRadius, distToDamage);

// Damaged areas have fewer leaves
if (hash(worldPos) > leafDensity * (1.0 - damageInfluence * 0.8)) return;

// Damaged leaves are discolored
leafColor = mix(leafColor, vec3(0.3, 0.2, 0.1), damageInfluence);
```

### 9.4 Fire Effects

Burning trees:

```glsl
if (tree.onFire) {
    // Leaves near fire are consumed
    float burnProgress = tree.burnTime;
    float burnHeight = tree.position.y + burnProgress * tree.height;

    if (worldPos.y < burnHeight) {
        // Below burn line = consumed
        return;
    }

    // Just above burn line = charred
    float charDist = worldPos.y - burnHeight;
    if (charDist < charZone) {
        leafColor = mix(vec3(0.1, 0.05, 0.0), leafColor, charDist / charZone);
    }
}
```

---

## Implementation Order

### Milestone 1: Branch Bezier Pipeline

**Goal:** Render a single procedural tree trunk.

1. Create TreeDefinition and TreeInstance structures
2. Compute shader generates trunk as a BranchInstance
3. Vertex shader renders branch as bezier tube
4. Basic bark texturing
5. **Verify:** Single trunk on screen, can modify parameters

### Milestone 2: Branch Hierarchy

**Goal:** Full branching structure.

1. Multi-pass compute: trunk → primary → secondary branches
2. Proper attachment points and orientations
3. Radius tapering through hierarchy
4. **Verify:** Tree skeleton visible, branches connect properly

### Milestone 3: Leaf Generation

**Goal:** Leaves fill the canopy.

1. Leaf compute shader generates positions in canopy volume
2. Basic leaf rendering (billboards or simple cards)
3. Culling: frustum, distance, density
4. **Verify:** Tree has foliage, performance is reasonable

### Milestone 4: Wind System Integration

**Goal:** Tree responds to same wind as grass.

1. Sample shared wind texture in branch shader
2. Hierarchical wind response (trunk → branch → leaf)
3. Leaf flutter animation
4. **Verify:** Tree and grass move together in wind

### Milestone 5: Voronoi Clumping

**Goal:** Natural canopy variation.

1. Implement 3D Voronoi in leaf compute shader
2. Clump influences size, orientation
3. **Verify:** Visible clustering in canopy, not uniform

### Milestone 6: Color Palette System

**Goal:** Painted aesthetic matching grass.

1. Create leaf color palette texture
2. Map clumpId and depth to palette UV
3. Implement translucency
4. **Verify:** Cohesive color with grass, nice backlighting

### Milestone 7: Shadow System

**Goal:** Trees cast and receive shadows.

1. Canopy shadow proxy geometry
2. Branch shadow pass (thick branches only)
3. Self-shadowing approximation via depth
4. **Verify:** Soft shadows on ground, canopy has depth

### Milestone 8: LOD System

**Goal:** Performance at distance.

1. Distance-based density reduction
2. Branch depth limits by distance
3. Smooth probability-based transitions
4. **Verify:** Forest renders efficiently, no popping

### Milestone 9: Forest Rendering

**Goal:** Many trees efficiently.

1. Tree placement from density/type maps
2. Batch all tree compute and rendering
3. Spatial culling before compute dispatch
4. **Verify:** 100+ trees at reasonable performance

---

## Summary: Milestone Visibility

| Milestone | What's Visible |
|-----------|----------------|
| 1 | Single procedural trunk |
| 2 | Full branching tree skeleton |
| 3 | **Tree with foliage** |
| 4 | **Wind animation matching grass** |
| 5 | Natural canopy clustering |
| 6 | **Painted color aesthetic** |
| 7 | Proper shadows |
| 8 | Performance at distance |
| 9 | Full forest |

**Key insight:** Milestones 3, 4, and 6 create the visual target. You have a cohesive tree-grass system by Milestone 6.

---

## Appendix A: Integration with Grass System

### Shared Resources

| Resource | Grass | Trees | Sharing Strategy |
|----------|-------|-------|------------------|
| Wind texture | Sample | Sample | Single texture, both sample |
| Wind uniforms | Read | Read | Single uniform buffer |
| Instance buffer | Write | Write | Separate regions or separate buffers |
| Indirect args | Write | Write | Separate draw calls |
| Color palettes | Sample | Sample | Same texture array |
| Shadow maps | Receive | Cast + Receive | Standard shadow system |

### Unified Compute Pass

Option: Run grass and tree compute in the same command buffer for efficiency:

```cpp
void recordComputePass(VkCommandBuffer cmd) {
    // Wind texture is already populated

    // Grass generation
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, grassPipeline);
    for (auto& tile : grassTiles) {
        dispatchGrassCompute(cmd, tile);
    }

    // Tree generation (can overlap with grass on async compute)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, treeBranchPipeline);
    for (auto& tree : visibleTrees) {
        dispatchBranchCompute(cmd, tree);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, treeLeafPipeline);
    for (auto& tree : visibleTrees) {
        dispatchLeafCompute(cmd, tree);
    }

    // Memory barrier before rendering
    vkCmdPipelineBarrier(/* ... */);
}
```

### Shared Culling

Both systems can use the same frustum planes and hierarchical Z-buffer for occlusion culling.

---

## Appendix B: Vulkan Resource Checklist

### Buffers

- [ ] Tree instance buffer (transforms, parameters)
- [ ] Tree definition buffer (parametric tree types)
- [ ] Branch instance buffer (generated branches)
- [ ] Leaf instance buffer (generated leaves)
- [ ] Branch indirect draw args
- [ ] Leaf indirect draw args
- [ ] Atomic counters (branches, leaves)

### Textures

- [ ] Tree placement density map
- [ ] Tree type map
- [ ] Leaf color palette (shared with grass approach)
- [ ] Bark texture
- [ ] Leaf alpha texture

### Pipelines

- [ ] Branch generation compute
- [ ] Leaf generation compute
- [ ] Branch rendering graphics
- [ ] Leaf rendering graphics
- [ ] Shadow proxy rendering

### Descriptor Sets

- [ ] Compute: tree data, instance buffers, wind, counters
- [ ] Graphics: instance buffers, textures, lighting

---

## Appendix C: MoltenVK Compatibility

### Fully Supported Features

| Feature | Status | Notes |
|---------|--------|-------|
| Compute shaders | Supported | Multi-pass generation works |
| Storage buffers | Supported | Branch and leaf instance buffers |
| Scalar atomics | Supported | Counter incrementing |
| Indirect draw | Supported | Variable instance counts |
| 3D dispatch | Supported | Leaf volume generation |
| Texture sampling | Supported | Wind, palettes, bark |

### Considerations

**Workgroup Size:** Use multiples of 32 for Apple GPU efficiency.

```glsl
// 3D dispatch for leaf generation
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in; // 64 threads
```

**Subgroup Operations:** Optional optimization, same as grass - query support at runtime.

**Memory:** Procedural trees regenerate each frame, so instance buffers don't accumulate. Budget similar to grass (~8MB for instance data).

### No Problematic Features

- No geometry shaders (all geometry from vertex pulling)
- No tessellation (bezier evaluation in vertex shader)
- No float atomics (only uint counters)

---

## References

- **Ghost of Tsushima GDC Talk:** Procedural grass techniques that inspire this approach
- **L-Systems:** Formal grammar for procedural tree generation (simplified here)
- **The Witness:** Visual target for painterly trees
- **SpeedTree:** Industry standard for comparison (different approach)
