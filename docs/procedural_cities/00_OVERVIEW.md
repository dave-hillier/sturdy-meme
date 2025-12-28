# Procedural Cities - Overview & Milestones

[← Back to Index](README.md)

---

## Executive Summary

This document outlines a comprehensive plan for implementing procedural city and settlement generation in the game engine. The system will generate contextually appropriate settlements ranging from small hamlets to larger towns, integrated with the existing terrain, biome, and road systems.

**Target Quality**: AAA-standard procedural generation with artist-controllable parameters
**Scale**: Settlements from 5-building hamlets to 200+ building towns
**Setting**: High Medieval (c. 1100-1300 AD) South Coast of England
**Region**: Inspired by Sussex, Hampshire, and Dorset coastal landscapes

---

## Design Decisions

The following key decisions have been made:

| Decision | Choice | Implication |
|----------|--------|-------------|
| **Scalability** | Algorithm must scale from hamlet to town | No "minimum viable" - single system handles all sizes |
| **Generation Pipeline** | Fully procedural first, then artist modifiers | Build-time generation with post-generation tweaking |
| **Building Interiors** | Required | Full interior spaces, not solid volumes |
| **Performance Budget** | Not constrained initially | Optimize after functionality complete |
| **Dynamic Settlements** | No | Settlements are static once generated |
| **Terrain Integration** | Buildings adapt to terrain | Cellars, tunnels, wells cut into terrain; existing terrain cut system (no physics yet) |
| **Street Networks** | Hybrid: Space colonization + A* terrain-following | L-systems alone insufficient; driven by POIs and terrain constraints |
| **Building Styles** | Multiple period styles per building type | Norman, Early English Gothic, regional material variations |
| **Asset Pipeline** | Procedural blockout → hand-crafted replacement | Start procedural, swap in artist assets as available |
| **Defensive Structures** | Required for towns, castles for key settlements | South coast = contested frontier; French raids, piracy |

### South Coast Defensive Context

The south coast of England was a **contested frontier** throughout the medieval period:

- **Norman Conquest Legacy**: Castles at strategic points (Arundel, Lewes, Pevensey, Hastings)
- **French Raids**: Ongoing threat requiring town defenses (Southampton burned 1338)
- **Cinque Ports**: Defensive confederation (Hastings, Romney, Hythe, Dover, Sandwich + Rye, Winchelsea)
- **Piracy**: Coastal settlements needed protection
- **Trade Routes**: Valuable ports required walls and harbor defenses

**Defensive elements by settlement type:**

| Settlement Type | Defenses |
|----------------|----------|
| Hamlet | None (flee to nearest fortification) |
| Village | Church as refuge (thick walls, tower) |
| Town | Town walls, gates, towers |
| Fishing Village | Possible watchtower, beacon |
| Port Town | Full walls + harbor chain/boom |
| Castle Settlement | Castle + dependent town |

### Street Network Algorithm Rationale

Pure L-systems are insufficient because streets must be:
- **Driven by Points of Interest**: Connections between settlements, key buildings
- **Terrain-Aware**: Follow contours, avoid steep slopes, cross water at fords
- **Hierarchical**: Main roads connect towns, lanes branch to serve lots

Selected approach combines:
1. **Space Colonization**: Organic growth from seed points toward attractors
2. **A* Pathfinding**: Terrain cost evaluation (existing `RoadPathfinder`)
3. **Constraint Satisfaction**: Ensure connectivity, avoid obstacles

### Building Style Variations

Each building type has period/style variants:

| Style Period | Date Range | Characteristics |
|-------------|-----------|-----------------|
| **Saxon Survival** | Pre-1100 | Simple timber, small windows, steep roofs |
| **Norman** | 1066-1200 | Round arches, thick walls, herringbone masonry |
| **Transitional** | 1150-1200 | Mix of round and pointed arches |
| **Early English** | 1180-1275 | Pointed arches, lancet windows, buttresses |

Buildings select style based on:
- Settlement wealth parameter
- Building age (when was it "built")
- Regional material availability

### Blockout-to-Handcrafted Pipeline

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   PROCEDURAL    │────▶│    BLOCKOUT     │────▶│   HAND-CRAFTED  │
│   GENERATION    │     │     ASSET       │     │   REPLACEMENT   │
└─────────────────┘     └─────────────────┘     └─────────────────┘
        │                       │                       │
        ▼                       ▼                       ▼
  - Dimensions            - Simplified mesh      - Artist-modeled
  - Footprint             - Material slots       - Full detail
  - Room layout           - LOD-ready            - Unique variants
  - Style params          - Collision proxy      - Baked lighting
```

Each generated building outputs:
1. **Blockout mesh**: Low-poly placeholder with correct dimensions
2. **Metadata JSON**: All generation parameters for artist reference
3. **Asset ID**: Unique identifier for hand-crafted replacement

Artists can then:
- Model detailed replacements referencing blockout dimensions
- Register replacement in asset manifest
- System swaps in hand-crafted version at runtime

### Implications of Interior Requirement

Requiring interiors significantly expands scope:

- **Room Layout Generation**: Need floor plan algorithms
- **Furniture Placement**: Interior prop system required
- **Lighting**: Interior light sources (hearth, candles, windows)
- **Occlusion**: Cannot use simple box collision
- **LOD Complexity**: Interior/exterior LOD transitions
- **Navigation**: AI pathfinding inside buildings

### Terrain Integration Details

The existing terrain cut system will be extended for:

- **Cellars**: Below-ground rooms in stone houses, churches
- **Tunnels**: Access passages, crypts under churches
- **Wells**: Vertical shafts with water table integration
- **Foundations**: Leveled areas for building footprints
- **Terracing**: Stepped foundations on slopes

*Note: Physics integration for terrain cuts is pending*

---

## Visual Milestones - Breadth-First Development

Development follows a **breadth-first** approach: fill the entire world with low-fidelity content first, then progressively refine. This enables parallel development of gameplay, AI, and other systems against real (if rough) environments.

### Milestone Overview

```
M1        M2          M2.5       M3         M4          M5          M6         M7         M8
Markers → Footprints → Roamable → Blockout → Silhouette → Structure → Material → Facade → Props
  ▼          ▼           ▼          ▼          ▼           ▼           ▼          ▼         ▼
 ●●●       ▭▭▭         ═══        ▤▤▤        ⌂⌂⌂         🏠🏠🏠       ░░░       ▦▦▦       ⚐⚐⚐
Dots      Flat       Roads+     Boxes      Roofs       Types       Colors    Windows   Details
on map    shapes     Plots      extruded   added       visible     applied   doors     placed
                   ═══════════
                   KEY POINT:
                   Characters
                   can roam!
```

### Milestone 1: World Markers
**Goal**: See settlement distribution across entire terrain

| Element | Representation |
|---------|---------------|
| Settlement centers | Colored sphere/icon by type |
| Settlement radius | Circle decal on terrain |
| External roads | Line decals connecting settlements |
| Castle locations | Distinct marker |

**Deliverables**:
- Settlement position data from BiomeGenerator visualized
- Road network from RoadPathfinder rendered as lines
- Debug visualization mode in engine

**Enables**: Understanding of world layout, travel distances, strategic positions

---

### Milestone 2: Footprints
**Goal**: See settlement extents and internal structure as 2D shapes

| Element | Representation |
|---------|---------------|
| Building lots | Flat colored quads on terrain |
| Street network | Flat road surface geometry |
| Wall perimeter | Line showing wall path |
| Key buildings | Different colors (church=gold, inn=brown, etc.) |

**Deliverables**:
- Settlement layout generator outputting lot positions
- Street network as flat mesh
- Terrain decals or flat geometry for all elements

**Enables**: Pathfinding development, NPC placement testing, gameplay area sizing

---

### Milestone 2.5: Roamable World (Key Integration Point)
**Goal**: Complete world skeleton that characters can navigate - road network, agricultural fields, and properly subdivided settlement plots aligned to streets

This is the **critical integration milestone** where all layout systems connect: inter-settlement roads, intra-settlement streets, and lot subdivision create a coherent navigable world.

#### Road Network (Inter-Settlement)

| Element | Representation |
|---------|---------------|
| Main roads | Flat geometry connecting towns, following terrain |
| Lanes | Narrower paths to villages and hamlets |
| Footpaths | Minimal paths to isolated locations |
| Bridges | Simple flat spans over rivers |
| Fords | Road dips into shallow water |

**Technical Requirements**:
- Road splines from existing `RoadPathfinder` (A* terrain-aware)
- Road surface geometry with proper banking on slopes
- Road-terrain blending (flatten terrain under roads)
- Clear route from any settlement to any other

#### Bridge & Ford Network (Water Crossings)

Bridges are **expensive in pathfinding** - the high water penalty (1000.0) in `RoadPathfinder` represents construction cost. Once the network is generated, bridges become critical infrastructure that influences settlement patterns.

| Crossing Type | Pathfinder Cost | Construction | Typical Use |
|---------------|-----------------|--------------|-------------|
| Ford | Moderate (100-300) | Minimal | Low traffic, shallow water |
| Timber Bridge | High (500-800) | Significant | Village connections |
| Stone Bridge | Very High (800-1200) | Major | Town connections, main roads |
| Clapper Bridge | Moderate (200-400) | Modest | Footpaths, moorland streams |

**Bridge Placement Logic**:

```
1. Water Crossing Detection → Identify where road path crosses water bodies
2. Crossing Type Selection:
   - Main roads (Town↔Town) → Prefer stone bridge
   - Secondary roads (Town↔Village) → Timber bridge or ford
   - Lanes (Village↔Hamlet) → Ford or timber
   - Footpaths → Ford, stepping stones, or clapper
3. Ford Viability Check:
   - Water depth < 0.5m at crossing point
   - River width < 20m
   - Gentle bank slopes (< 15°)
   - If viable AND low-traffic route → Use ford instead of bridge
4. Bridge Site Optimization:
   - Narrow river width preferred
   - Stable banks (not marshland)
   - Avoid flood plains
```

**Network Effects** (post-generation):

| Effect | Description |
|--------|-------------|
| Bridge towns | Settlements near bridges gain importance (control point) |
| Toll points | Bridges on major routes may have toll infrastructure |
| Defense focus | Bridges are natural defensive positions |
| Alternative fords | Shallow crossings may exist near bridges (local knowledge) |

**Data Structure**:

```cpp
struct WaterCrossing {
    uint32_t id;
    uint32_t roadSegmentId;          // Which road segment this belongs to
    glm::vec2 position;

    enum class Type : uint8_t {
        Ford,
        SteppingStones,
        ClapperBridge,      // Simple stone slabs
        TimberBridge,
        StoneBridge
    } type;

    float span;                       // Distance across water
    float waterDepth;                 // At crossing point
    bool hasTollPoint = false;
    uint32_t nearestSettlementId;     // For bridge-town association
};

struct WaterCrossingNetwork {
    std::vector<WaterCrossing> crossings;

    // Query helpers
    std::vector<uint32_t> getCrossingsNear(glm::vec2 pos, float radius) const;
    std::vector<uint32_t> getBridgesOnRoute(uint32_t startSettlement, uint32_t endSettlement) const;
};
```

**Historical Context** (1100-1300 AD South Coast):
- Stone bridges rare and valuable (often have chapels)
- Timber bridges common but require maintenance
- Fords preferred where possible (no construction cost)
- Bridge rights often controlled by monasteries or lords
- Bridge settlements develop markets (captive traffic)

**Integration with Settlement Scoring**:
```cpp
// Boost settlement score for bridge proximity
if (nearBridge && bridgeOnMajorRoute) {
    settlement.score += 0.15f;  // Bridge town bonus
    settlement.type = promoteSettlementType(settlement.type);  // Hamlet→Village, etc.
}
```

#### Farm Fields (Agricultural Zones)

| Element | Representation |
|---------|---------------|
| Strip fields | Long narrow plots (~200m × 20-30m) |
| Common fields | Large enclosed areas |
| Pasture | Fenced grazing areas |
| Orchards | Grid-planted tree areas |
| Field boundaries | Hedgerows, ditches, or low walls |

**Pattern**: Medieval open field system with ridge-and-furrow visible

```
Village
   │
   └──── Common Road ────────────────┐
              │                      │
    ┌─────────┼─────────┐    ┌──────┴──────┐
    │ Strip │ Strip │ Strip │    │  Pasture    │
    │ Field │ Field │ Field │    │  (fenced)   │
    │  ▓▓▓  │  ▒▒▒  │  ░░░  │    │             │
    │  ▓▓▓  │  ▒▒▒  │  ░░░  │    └─────────────┘
    │  ▓▓▓  │  ▒▒▒  │  ░░░  │
    └───────┴───────┴───────┘
```

#### Street Network (Intra-Settlement)

| Element | Representation |
|---------|---------------|
| Main street | Primary through-road (continuation of external road) |
| Market street | Wider section, often with widening for stalls |
| Back lanes | Narrow access to rear of properties |
| Alleys | Minimal passages between buildings |
| Crossroads | Junction points with decision nodes |

**Generation Approach**: Space colonization from settlement entry points (road connections) toward key attractors (church, market, well), then A* refinement for terrain

#### Plot Subdivision (Critical: Road-Aligned Lots)

| Element | Representation |
|---------|---------------|
| Street frontage | Plot edge aligned parallel to street |
| Burgage plots | Deep narrow lots (typical medieval: 5-10m wide × 30-60m deep) |
| Rear access | Back lane or shared access |
| Corner plots | Larger, more valuable |
| Irregular fills | Odd-shaped plots where streets meet |

**Subdivision Algorithm**:

```
1. Street Network → Generate primary and secondary streets
2. Block Identification → Find enclosed areas between streets
3. Frontage Extraction → Identify edges adjacent to streets
4. Perpendicular Division → Subdivide blocks perpendicular to frontage
5. Depth Calculation → Extend lots to back lane or block center
6. Width Assignment → Based on building type requirements
7. Adjustment → Handle corners, irregular shapes, terrain
```

**Visual**: Properly aligned lots along street

```
        Street (Main Road Continuation)
    ═══════════════════════════════════════════
         │         │         │         │
         │  Lot 1  │  Lot 2  │  Lot 3  │  Lot 4
         │ 8m×40m  │ 6m×40m  │10m×40m  │ 7m×40m
         │         │         │         │
         │    ▢    │    ▢    │    ▢    │    ▢     ← Building footprints
         │         │         │         │              on lots
         │         │         │         │
    ─────┴─────────┴─────────┴─────────┴─────────
                   Back Lane
```

**Key Properties**:
- Frontage Width: Street-facing edge determines buildable width
- Plot Depth: Distance to back boundary (rear lane, neighbor, or block center)
- Street Alignment: Building façade line parallel to street centerline
- Setback: Small front yard or building at street edge (period-appropriate)

#### Building Placement on Lots

| Placement Rule | Description |
|----------------|-------------|
| Street-facing | Main building front on street frontage |
| Gable-end or parallel | Based on lot width vs building length |
| Outbuildings rear | Secondary structures toward back of lot |
| Yard space | Working area between buildings |
| Access point | Door faces street or yard |

**Blockout Representation**:
- Simple extruded box per lot (lot width × depth × estimated height)
- Color-coded by intended building type
- Collision mesh for character navigation

#### Navigation & Collision

| Element | Implementation |
|---------|---------------|
| Road walkable | Flat, clear paths |
| Field traversable | Slower movement through crops |
| Lot boundaries | Implicit (no collision) or low fence |
| Building collision | Blockout boxes block movement |
| Water obstacles | Rivers require bridges/fords |

**Deliverables**:
- Complete road network mesh (all settlements connected)
- Agricultural field layout with boundaries
- Street network within each settlement
- Lot subdivision with proper street alignment
- Building blockout boxes on each lot
- NavMesh or equivalent for pathfinding
- Character can walk from any settlement to any other

**Enables**: Full world traversal, distance testing, NPC routing, gameplay prototyping

**Visual Result**: Looking down at a settlement, you see:
- Roads entering from outside → becoming streets inside
- Streets dividing settlement into blocks
- Blocks subdivided into regular plots aligned to streets
- Simple box buildings on each plot
- Fields radiating outward in organized patterns
- Characters can walk everywhere on roads/streets

---

### Milestone 3: Blockout Volumes
**Goal**: 3D presence - correct mass and scale, no detail

| Element | Representation |
|---------|---------------|
| Buildings | Extruded boxes (footprint × height) |
| Walls | Extruded wall path (simple box section) |
| Towers | Cylinders or box primitives |
| Gates | Box with tunnel cutout |
| Castle keep | Large box |

**Visual quality**: Minecraft-like, but correct proportions

**Deliverables**:
- Procedural box mesh generation for each building
- Simple wall extrusion
- Collision geometry (usable for gameplay)

**Enables**: First-person navigation, combat testing, siege mechanics prototyping

---

### Milestone 4: Silhouettes
**Goal**: Recognizable medieval settlement from distance

| Element | Representation |
|---------|---------------|
| Buildings | Box + pitched roof (gable/hipped) |
| Church | Box + tower with simple spire |
| Walls | Box section with flat-top crenellation |
| Towers | Cylinder + crenellated top |
| Gates | Arch shape in wall |
| Castle keep | Stepped profile, corner turrets as boxes |

**Visual quality**: Identifiable building types, good for impostor generation

**Deliverables**:
- Roof generation (straight skeleton or simplified)
- Tower cap geometry
- Basic crenellation pattern

**Enables**: Long-distance visibility, impostor atlas generation, skyline composition

---

### Milestone 5: Structural Articulation
**Goal**: Building types clearly distinguishable

| Element | Representation |
|---------|---------------|
| Timber buildings | Visible frame lines on walls |
| Stone buildings | Different wall profile |
| Church | Buttresses, window openings (holes) |
| Longhouse | Distinct proportions |
| Castle | Forebuilding, spiral stair turret shapes |
| Mill | Wheel or sail shapes attached |

**Visual quality**: Can identify building function from medium distance

**Deliverables**:
- Building type-specific geometry rules
- Structural element generation (beams, buttresses)
- Opening placement (doors, windows as holes)

**Enables**: Player orientation, building-specific gameplay, quest targeting

---

### Milestone 6: Material Assignment
**Goal**: Color and material variation, sense of place

| Element | Representation |
|---------|---------------|
| Walls | Base color by material (flint=grey, timber=brown, daub=cream) |
| Roofs | Thatch=golden, tile=terracotta, slate=grey |
| Timber | Dark brown/black beams |
| Stone details | Lighter color for quoins, dressings |

**Visual quality**: Distinct regional character, no texture detail yet

**Deliverables**:
- Material ID assignment per surface
- Basic color palette per material
- Procedural color variation (age, weathering tint)

**Enables**: Atmosphere, regional identity, time-of-day lighting tests

---

### Milestone 7: Facade Detail
**Goal**: Close-up visual interest

| Element | Representation |
|---------|---------------|
| Windows | Geometry with frame, possibly shutters |
| Doors | Planked door geometry |
| Timber frame | 3D beam geometry (not just lines) |
| Chimneys | Basic chimney stacks |
| Church windows | Lancet/round arch shapes |
| Signs | Inn signs, shop signs |

**Visual quality**: Acceptable for gameplay camera distances

**Deliverables**:
- Window/door insertion system
- Timber frame geometry generation
- Chimney placement

**Enables**: NPC interaction points, entry/exit visualization

---

### Milestone 8: Props and Ground Detail
**Goal**: Lived-in feeling, environmental storytelling

| Element | Representation |
|---------|---------------|
| Yard props | Carts, barrels, haystacks, tools |
| Fences | Property boundaries |
| Street furniture | Wells, market stalls, posts |
| Ground variation | Mud, cobbles, grass patches |
| Vegetation | Garden plots, trees |

**Visual quality**: Rich environment for exploration

**Deliverables**:
- Prop placement system
- Ground material variation
- Vegetation integration with TreeSystem

**Enables**: Looting, hiding spots, environmental puzzles

---

### Milestone 9: Interiors (Gameplay-Critical Only)
**Goal**: Enterable buildings for gameplay

| Element | Representation |
|---------|---------------|
| Room volumes | Correct floor plan |
| Basic furniture | Table, bed, chest (blockout) |
| Hearth | Fire location with light source |
| Doorways | Portals between rooms |

**Visual quality**: Functional for gameplay, not showcase

**Deliverables**:
- Floor plan generation
- Interior wall placement
- Basic furniture blockout

**Enables**: Indoor combat, stealth, NPC schedules, looting

---

### Milestone 10: Polish
**Goal**: Release quality for key areas

| Element | Representation |
|---------|---------------|
| Full texture detail | PBR materials with normal maps |
| Weathering | Moss, staining, wear |
| Unique buildings | Hand-crafted hero assets |
| Interior detail | Full furniture, props |
| LOD system | Smooth transitions |
| Lighting | Baked AO, interior lights |

**Visual quality**: AAA for hero locations, good for everywhere

**Deliverables**:
- Texture generation/assignment
- LOD mesh generation
- Hand-crafted asset integration
- Lighting bake

---

### Milestone Dependencies

```
                    ┌─────────────────────────────────────────────────┐
                    │              PARALLEL WORKSTREAMS               │
                    └─────────────────────────────────────────────────┘

 SETTLEMENTS        M1 ──▶ M2 ──▶ M2.5 ──▶ M3 ──▶ M4 ──▶ M5 ──▶ M6 ──▶ M7 ──▶ M8 ──▶ M9 ──▶ M10
                    │      │       │       │      │      │
                    ▼      ▼       ▼       ▼      ▼      ▼
 GAMEPLAY           ·······●───────●───────●──────●──────●─────────────────────────────────▶
                           │       │       │      │
                           ▼       ▼       ▼      │
 AI/NPCs            ·······●───────●───────●──────●──────────────────────────────────────▶
                           │       │       │
                           ▼       ▼       ▼
 EXPLORATION        ·······│───────●───────●──────●──────────────────────────────────────▶
                                   │       │
                                   ▼       ▼
 COMBAT             ···············●───────●─────────────────────────────────────────────▶
                                           │
                                           ▼
 QUESTS             ·······················●─────────────────────────────────────────────▶

 Legend: ● = Can begin development  ─▶ = Continues with refinement
 M2.5 = Roamable World (roads, fields, subdivided plots) - KEY INTEGRATION POINT
```

### Recommended First Pass

For fastest time-to-playable:

1. **M1**: Generate all settlement markers (use existing BiomeGenerator output)
2. **M2**: Generate footprints for 3-5 test settlements
3. **M2.5**: Roads + fields + subdivided plots for those settlements (ROAMABLE!)
4. **M2.5-ext**: Extend M2.5 to ALL settlements (world becomes fully navigable)
5. **M3**: Blockout buildings on each lot
6. **M4**: Add silhouettes to all (roofs, crenellations)
7. **Continue breadth-first**: Each milestone covers entire world before next

### Quality Tiers

Not all areas need equal polish. Define tiers:

| Tier | Areas | Target Milestone |
|------|-------|-----------------|
| **Hero** | Starting town, key story locations | M10 (full polish) |
| **Primary** | Major towns, quest hubs | M8-M9 |
| **Secondary** | Villages on main routes | M6-M7 |
| **Background** | Distant hamlets, rarely visited | M4-M5 |

This allows focused effort while maintaining world coverage.

---

