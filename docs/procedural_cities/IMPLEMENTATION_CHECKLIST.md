# Procedural Cities Implementation Checklist

[← Back to Index](README.md)

A bullet-point checklist for implementing procedural city generation, organized by phase and system layer.

---

## Phase 0: 2D Preview & Browser Tooling (TypeScript)

Parallel track for rapid visual iteration. See [IMPL_TOOLING.md](IMPL_TOOLING.md)

### Spatial Types & Polygon Operations
- [ ] `Vec2` type with basic operations (add, subtract, scale, normalize, dot, cross)
- [ ] `Polygon` class with CCW winding convention
  - [ ] `area()` computation
  - [ ] `centroid()` calculation
  - [ ] `contains(point)` point-in-polygon test
  - [ ] `bounds()` AABB computation
- [ ] `Polyline` class
  - [ ] `length()` total length
  - [ ] `pointAt(t)` position sampling
  - [ ] `tangentAt(t)` direction at position
  - [ ] `normalAt(t)` perpendicular (left side)
- [ ] `AABB2D` bounding box type

*Reference: [IMPL_FOUNDATION.md](IMPL_FOUNDATION.md) §1.1*

### Subdivision Algorithms
- [ ] BSP subdivision (`bspSubdivide`)
  - [ ] Split polygon recursively by random axis-aligned lines
  - [ ] Respect minCellArea/minCellWidth constraints
- [ ] Grid subdivision (`gridSubdivide`)
  - [ ] Regular grid respecting polygon boundary
- [ ] **Frontage subdivision** (`frontageSubdivide`) - CRITICAL
  - [ ] Subdivide blocks along street frontage edge
  - [ ] Create burgage-style lots (5-10m wide × 30-60m deep)
  - [ ] Track which original edge each cell touches

*Reference: [IMPL_FOUNDATION.md](IMPL_FOUNDATION.md) §1.1*

### Path Network
- [ ] `PathNode` data structure (id, position, connections, type)
- [ ] `PathSegment` data structure (id, start/end nodes, type, control points)
- [ ] `PathNetwork` class
  - [ ] `addNode()` / `addSegment()`
  - [ ] `finalize()` - compute derived geometry
  - [ ] `getSegmentsOfType(type)` query
  - [ ] `findNearestNode(position)` spatial query

*Reference: [IMPL_GEOMETRY.md](IMPL_GEOMETRY.md) §2.1*

### Space Colonization Algorithm
- [ ] `Attractor` type (position, weight, type)
- [ ] Space colonization configuration (kill distance, influence radius, segment length)
- [ ] Core algorithm implementation
  - [ ] Seed from entry points
  - [ ] Grow toward attractors
  - [ ] Branch at junctions
  - [ ] Remove satisfied attractors

*Reference: [IMPL_GEOMETRY.md](IMPL_GEOMETRY.md) §2.1*

### Layout Generator
- [ ] `Lot` data structure (boundary, frontage, zone, building type)
- [ ] `Block` data structure (boundary, lot IDs, bounding segments)
- [ ] `SettlementLayout` output structure
- [ ] Layout generation pipeline:
  - [ ] Place key buildings as attractors
  - [ ] Generate street network via space colonization
  - [ ] Identify blocks (enclosed areas between streets)
  - [ ] Subdivide blocks into lots along frontage
  - [ ] Assign zones and building types

*Reference: [IMPL_GEOMETRY.md](IMPL_GEOMETRY.md) §2.2*

### SVG Renderer
- [ ] Basic SVG rendering with pan/zoom
- [ ] Render path network (roads, streets, lanes)
- [ ] Render lots with zone colors
- [ ] Render building footprints
- [ ] Click-to-inspect functionality

### Browser UI
- [ ] Parameter panel (seed, settlement type, density sliders)
- [ ] Layer visibility toggles
- [ ] Live regeneration on parameter change
- [ ] JSON export for C++ import
- [ ] SVG export for documentation

*Reference: [IMPL_TOOLING.md](IMPL_TOOLING.md) §9.2-9.5*

---

## Phase 1: C++ Foundation Layer

Core systems with no dependencies. See [IMPL_FOUNDATION.md](IMPL_FOUNDATION.md)

### Spatial Utilities (Port from TypeScript)
- [ ] `Polygon` struct matching TypeScript version
- [ ] `Polyline` struct matching TypeScript version
- [ ] Polygon boolean operations (using Clipper2 library)
  - [ ] `polygonUnion()`
  - [ ] `polygonIntersection()`
  - [ ] `polygonDifference()`
  - [ ] `polygonOffset()` (inset/outset)
- [ ] Port subdivision algorithms from TypeScript
- [ ] `SpatialIndex2D` class (R-tree or grid acceleration)
  - [ ] `insert()` / `remove()`
  - [ ] `queryBox()` / `queryRadius()` / `queryNearest()`
- [ ] Straight skeleton (simplified version initially)

*Location: `tools/common/SpatialUtils.h/.cpp`*
*Reference: [IMPL_FOUNDATION.md](IMPL_FOUNDATION.md) §1.1*

### Config System
- [ ] `ConfigRegistry` class
  - [ ] `loadFromDirectory(path)`
  - [ ] Template `get<T>(id)` type-safe access
  - [ ] `findByTag()` / `findByType()` queries
- [ ] JSON schema definitions:
  - [ ] `BuildingTypeConfig` (footprint, floors, roof, materials, props)
  - [ ] `SettlementTemplateConfig` (layout pattern, street widths, zones)
  - [ ] `MaterialConfig` (textures, UV scale, variants)
  - [ ] `PropConfig` (placement rules)
- [ ] Schema validation on load

*Location: `tools/common/ConfigSystem.h/.cpp`*
*Reference: [IMPL_FOUNDATION.md](IMPL_FOUNDATION.md) §1.2*

### Terrain Integration
- [ ] `TerrainInterface` class
  - [ ] Initialize from heightmap + biomemap paths
  - [ ] `getHeight(worldPos)` / `getHeights(batch)` queries
  - [ ] `getNormal()` / `getSlope()` queries
  - [ ] `getBiome()` / `isWater()` / `isCoastline()` queries
- [ ] `AreaAnalysis` struct and `analyzeArea()` method
- [ ] `findBuildableRegions()` for complex terrain
- [ ] Terrain modification accumulation
  - [ ] `addModification()` (flatten, cut, fill)
  - [ ] `exportModificationLayer()`
  - [ ] `exportMaterialMask()`

*Location: `tools/common/TerrainInterface.h/.cpp`*
*Reference: [IMPL_FOUNDATION.md](IMPL_FOUNDATION.md) §1.3*

### SVG Exporter (C++ debugging)
- [ ] `SVGExporter` class
  - [ ] `begin()` / `save()`
  - [ ] `drawPolygon()` / `drawPolyline()` / `drawCircle()` / `drawText()`
  - [ ] `drawPathNetwork()` / `drawLots()` / `drawSettlement()` high-level methods
- [ ] Parity verification against TypeScript output

*Location: `tools/common/SVGExporter.h/.cpp`*
*Reference: [IMPL_TOOLING.md](IMPL_TOOLING.md) §9.6*

---

## Phase 2: Geometry Generation Layer

Systems that produce geometry. See [IMPL_GEOMETRY.md](IMPL_GEOMETRY.md)

### Path Network System (C++)
- [ ] Port `PathNetwork` class from TypeScript
- [ ] `PathType` enum (MainRoad, Street, Lane, Alley, WallPath, QuayEdge, etc.)
- [ ] `PathProperties` defaults per type (width, height, material)
- [ ] Port `SpaceColonization` algorithm
- [ ] `TerrainAwarePaths` class
  - [ ] Refine paths for terrain constraints
  - [ ] Integrate with existing `RoadPathfinder` for A* routing
- [ ] `WallPathGenerator` for defensive wall alignment
- [ ] `QuayPathGenerator` for waterfront edges
- [ ] Field boundary generation

*Reference: [IMPL_GEOMETRY.md](IMPL_GEOMETRY.md) §2.1, [IMPL_RECONCILIATION.md](IMPL_RECONCILIATION.md) §10.3*

### Water Crossing Detection
- [ ] `WaterCrossing` struct (type, span, depth, position)
- [ ] `WaterCrossingDetector` class
  - [ ] `detectCrossings()` - find where roads cross water
  - [ ] `classifyCrossing()` - ford vs timber vs stone bridge
  - [ ] `isFordViable()` - check depth/width/slope thresholds
- [ ] `BridgeSettlementBooster` class
  - [ ] `applyBridgeBonus()` - boost settlement scores near bridges
  - [ ] `identifyBridgeTowns()` - settlements that grew around crossings

*Reference: [IMPL_GEOMETRY.md](IMPL_GEOMETRY.md) §2.1 (Water Crossing Detection), [00_OVERVIEW.md](00_OVERVIEW.md) §M2.5*

### Layout System (C++)
- [ ] Port `LayoutGenerator` from TypeScript
- [ ] Add terrain awareness (slope limits, water avoidance)
- [ ] `LayoutImporter` for JSON from browser tool
- [ ] `WaterfrontLayout` for coastal lots with quay access
- [ ] `AgriculturalLayout` for medieval strip fields and pastures
- [ ] `DefensiveLayout` for walls, gates, towers

*Reference: [IMPL_GEOMETRY.md](IMPL_GEOMETRY.md) §2.2*

### Mesh Generation System
- [ ] Extend `Mesh` class with new methods:
  - [ ] `createExtrudedPolygon(footprint, height, capTop, capBottom)`
  - [ ] `createExtrudedPath(path, profile)` for walls/quays
  - [ ] `createRoof(footprint, pitch, type)` - gable/hipped/etc.
- [ ] `ExtrusionGenerator` class
  - [ ] Polygon extrusion
  - [ ] Path extrusion (for walls, quays)
  - [ ] Taper extrusion (for towers)
  - [ ] Batter extrusion (for castle walls)
- [ ] `RoofGenerator` class
  - [ ] Gable roof using straight skeleton
  - [ ] Hipped roof
  - [ ] Half-hipped, gambrel, mansard variants
- [ ] `CSGOperations` class
  - [ ] `cutRectangularHole()` for windows/doors (simplified)
  - [ ] Full `subtract()` / `unionMeshes()` (optional, complex)
- [ ] `PathMeshGenerator` class
  - [ ] `generatePathSurface()` for roads/streets
  - [ ] `generateWallMesh()` with battlements
  - [ ] `generateQuayMesh()` with steps

*Location: `tools/common/MeshGeneration.h/.cpp`*
*Reference: [IMPL_GEOMETRY.md](IMPL_GEOMETRY.md) §2.3*

### Shape Grammar System
- [ ] `Shape` struct (id, type, transform, size, symbol, children)
- [ ] `ShapeTree` class for hierarchical shape management
- [ ] Grammar rules:
  - [ ] `SplitRule` - divide along X/Y/Z axis
  - [ ] `ComponentSplitRule` - extract faces (front, back, sides, top)
  - [ ] `ConditionalRule` - branching based on context
  - [ ] `RepeatRule` - repeat pattern along dimension
- [ ] `ShapeGrammarEngine` class
  - [ ] `addRule()` / `execute()`
  - [ ] `generateMesh()` from shape tree
- [ ] `BuildingGrammarFactory`
  - [ ] Config-to-rules conversion
  - [ ] Pre-built grammars: cottage, longhouse, church, tower

*Location: `tools/building_generator/ShapeGrammar.h/.cpp`*
*Reference: [IMPL_GEOMETRY.md](IMPL_GEOMETRY.md) §2.4*

### Placement System
- [ ] `PlacementInstance` struct (object ID, transform, zone, category)
- [ ] `PlacementRule` struct (placement type, spacing, quantity, orientation)
- [ ] `PlacementEngine` class
  - [ ] Poisson disk sampling for random placement
  - [ ] `placeInArea()` / `placeInRoom()` / `placeAlongPath()`
  - [ ] Exclusion zone handling
- [ ] Specialized placers:
  - [ ] `ExteriorPropPlacer` (yards, streets)
  - [ ] `InteriorPropPlacer` (furniture)
  - [ ] `MaritimePropPlacer` (quays, beaches)

*Location: `tools/common/PlacementSystem.h/.cpp`*
*Reference: [IMPL_GEOMETRY.md](IMPL_GEOMETRY.md) §2.5*

---

## Phase 3: Assembly Layer

Systems that combine lower-level components. See [IMPL_ASSEMBLY.md](IMPL_ASSEMBLY.md)

### Building Assembler
- [ ] `BuildingOutput` struct (meshes, LODs, rooms, props, metadata)
- [ ] `BuildingAssembler` class
  - [ ] Generate footprint from lot constraints
  - [ ] Generate exterior using shape grammar
  - [ ] Generate interior (floor plan, rooms)
  - [ ] Generate LOD meshes
  - [ ] Place exterior and interior props
- [ ] Integration tests: full building export to GLB

*Location: `tools/building_generator/BuildingAssembler.h/.cpp`*
*Reference: [IMPL_ASSEMBLY.md](IMPL_ASSEMBLY.md) §3.1*

### Settlement Assembler
- [ ] `SettlementOutput` struct (ground, roads, walls, buildings, props, navmesh)
- [ ] `SettlementAssembler` class
  - [ ] Generate layout (or import from browser tool)
  - [ ] Generate all buildings via BuildingAssembler
  - [ ] Generate road meshes
  - [ ] Generate defensive structures (walls, gates, towers)
  - [ ] Populate props (streets, vegetation)
  - [ ] Generate navigation mesh
- [ ] Settlement export pipeline (meshes + metadata)

*Location: `tools/settlement_generator/SettlementAssembler.h/.cpp`*
*Reference: [IMPL_ASSEMBLY.md](IMPL_ASSEMBLY.md) §3.2*

---

## Phase 4: Runtime Layer

In-engine systems. See [IMPL_RUNTIME.md](IMPL_RUNTIME.md)

### Streaming System
- [ ] `SettlementStreamingSystem` class
  - [ ] Settlement slot management (unloaded/loading/loaded/unloading)
  - [ ] Priority queue for load requests based on distance
  - [ ] `update()` called each frame with camera position
  - [ ] `getVisibleSettlements()` for rendering
- [ ] Background loading with async I/O
- [ ] Unload distant settlements to free memory

*Location: `src/settlements/SettlementStreaming.h/.cpp`*
*Reference: [IMPL_RUNTIME.md](IMPL_RUNTIME.md) §4.1*

### LOD System
- [ ] `SettlementLODSystem` class
  - [ ] LOD level selection based on distance and screen size
  - [ ] Per-building LOD switching
  - [ ] Impostor atlas for distant buildings (follow `TreeImpostorAtlas` pattern)
- [ ] LOD mesh generation during build
- [ ] Smooth LOD transitions

*Location: `src/settlements/SettlementLOD.h/.cpp`*
*Reference: [IMPL_RUNTIME.md](IMPL_RUNTIME.md) §4.2*

### Renderer Integration
- [x] `SettlementSystem` class (following `TreeSystem` / `RockSystem` pattern)
  - [x] `create()` factory with InitInfo
  - [x] `getSceneObjects()` for building renderables (walls/roads TBD)
  - [x] Integration with existing render pipeline
- [ ] Material assignment from ConfigSystem
- [ ] Lighting integration (interior light sources)

*Location: `src/settlements/SettlementSystem.h/.cpp`*
*Reference: [IMPL_RECONCILIATION.md](IMPL_RECONCILIATION.md) §10.5*

---

## Visual Milestones

Progress checkpoints from [00_OVERVIEW.md](00_OVERVIEW.md)

### M1: World Markers
- [ ] Visualize settlement positions from BiomeGenerator
- [ ] Colored spheres/icons by settlement type
- [ ] Circle decals for settlement radius
- [ ] Line decals for external road network
- [ ] Debug visualization mode toggle

### M2: Footprints
- [ ] 2D layout generation working
- [ ] Flat colored quads for building lots
- [ ] Flat road surface geometry
- [ ] Wall perimeter lines
- [ ] Color coding by building type

### M2.5: Roamable World (KEY MILESTONE)
- [ ] Complete road network (all settlements connected)
- [x] Street networks within settlements (simplified: main street + cross streets for villages/towns)
- [x] Plot subdivision with street alignment (burgage-style lots: 5-10m × 30-60m perpendicular to frontage)
- [ ] Agricultural field layouts
- [x] Simple building blockout boxes (placed on lots with frontage alignment)
- [ ] NavMesh for pathfinding
- [ ] **Character can walk from any settlement to any other**

### M3: Blockout Volumes
- [x] Extruded boxes for buildings (footprint × height)
- [x] Correct proportions and scale
- [ ] Collision geometry for gameplay
- [ ] Wall extrusion

### M4: Silhouettes
- [ ] Pitched roofs (gable/hipped)
- [ ] Church towers with spires
- [ ] Wall crenellations
- [ ] Tower caps
- [ ] Recognizable from distance

### M5: Structural Articulation
- [ ] Visible timber frames
- [ ] Church buttresses
- [ ] Window/door openings (holes)
- [ ] Mill wheels/sails
- [ ] Building types distinguishable

### M6: Material Assignment
- [ ] Base colors by material type
- [ ] Regional material palette
- [ ] Procedural color variation (weathering)
- [ ] Material ID per surface

### M7: Facade Detail
- [ ] Window geometry with frames
- [ ] Door geometry
- [ ] 3D timber beam geometry
- [ ] Chimneys
- [ ] Church lancet windows
- [ ] Signs (inns, shops)

### M8: Props & Ground Detail
- [ ] Yard props (carts, barrels, tools)
- [ ] Fences and property boundaries
- [ ] Street furniture (wells, market stalls)
- [ ] Ground material variation
- [ ] Garden plots and vegetation

### M9: Interiors
- [ ] Floor plan generation
- [ ] Interior walls
- [ ] Basic furniture blockout
- [ ] Hearth with light source
- [ ] Doorway portals

### M10: Polish
- [ ] Full PBR textures
- [ ] Weathering effects
- [ ] Hand-crafted hero buildings
- [ ] Complete LOD system
- [ ] Baked ambient occlusion
- [ ] Interior lighting

---

## Testing Strategy

From [IMPL_OVERVIEW.md](IMPL_OVERVIEW.md) §6

- [ ] **Spatial Utilities**: Unit tests with known polygons
- [ ] **Config System**: Schema validation tests
- [ ] **Terrain Integration**: Test with actual heightmap data
- [ ] **Path Network**: Visual SVG output verification
- [ ] **Layout System**: Visual output, parity with TypeScript
- [ ] **Mesh Generation**: Export to GLB, view in Blender
- [ ] **Shape Grammar**: Visual tests of building variations
- [ ] **Placement System**: Visual scatter plots
- [ ] **Building Assembler**: Full building export and render test
- [ ] **Settlement Assembler**: Full settlement render test

---

## Integration with Existing Code

Key reuse points from [IMPL_RECONCILIATION.md](IMPL_RECONCILIATION.md)

### Direct Reuse
- [x] Use `BiomeGenerator` settlement positions directly
- [ ] Use `RoadPathfinder` for inter-settlement roads
- [ ] Use `RoadSpline` / `RoadNetwork` for path geometry
- [x] Use `TerrainHeight` utilities for height conversion
- [x] Use existing `Mesh` primitives (cube, cylinder, etc.)
- [x] Follow `TreeSystem` / `RockSystem` patterns for runtime

### Extensions Required
- [x] Extend `RoadType` enum with street types (Street, BackLane, Alley)
- [ ] Extend `Mesh` with polygon extrusion methods
- [ ] Add `StreetPathfinder` extending `RoadPathfinder`

### New Systems Required
- [ ] `Polygon2D` for 2D geometry operations
- [x] `FrontageSubdivision` for lot generation (simplified, built into SettlementSystem)
- [x] `SpaceColonization` for organic street networks (simplified, built into SettlementSystem)
- [ ] `ShapeGrammar` for building geometry
- [ ] `RoofGenerator` for roof meshes
- [x] `SettlementSystem` for runtime rendering

---

## File Structure

```
tools/
├── common/
│   ├── SpatialUtils.h/.cpp      # Polygon, Polyline, subdivision
│   ├── ConfigSystem.h/.cpp       # JSON config registry
│   ├── TerrainInterface.h/.cpp   # Terrain queries
│   ├── MeshGeneration.h/.cpp     # Extrusion, roofs
│   ├── PlacementSystem.h/.cpp    # Prop placement
│   └── SVGExporter.h/.cpp        # Debug visualization
│
├── road_generator/
│   ├── StreetGenerator.h/.cpp    # Space colonization
│   └── WaterCrossing.h/.cpp      # Bridge/ford detection
│
├── layout_generator/
│   ├── LayoutSystem.h/.cpp       # Settlement layout
│   ├── WaterfrontLayout.h/.cpp   # Coastal layouts
│   ├── AgriculturalLayout.h/.cpp # Farm fields
│   └── DefensiveLayout.h/.cpp    # Walls/gates
│
├── building_generator/
│   ├── ShapeGrammar.h/.cpp       # CGA-style rules
│   ├── RoofGenerator.h/.cpp      # Straight skeleton roofs
│   └── BuildingAssembler.h/.cpp  # Complete buildings
│
└── settlement_generator/
    ├── SettlementAssembler.h/.cpp
    └── SettlementExporter.h/.cpp

src/settlements/
├── SettlementSystem.h/.cpp       # Runtime rendering
├── SettlementLOD.h/.cpp          # LOD management
└── SettlementStreaming.h/.cpp    # Load/unload

web-tools/
├── src/spatial/                  # TypeScript geometry
├── src/paths/                    # Path network
├── src/layout/                   # Layout generation
├── src/renderer/                 # SVG rendering
└── src/ui/                       # Browser controls

assets/
├── settlements/templates/        # Settlement configs
├── buildings/types/              # Building configs
├── materials/                    # Material definitions
└── props/                        # Prop definitions
```

---

## Quick Wins

Immediate progress with minimal new code:

1. [ ] **SVG Export from BiomeGenerator** - export settlements as circles, roads as lines
2. [x] **Blockout cubes** - place `Mesh::createCube()` at settlement positions
3. [x] **Extend RoadType** - add Street/Lane/Alley to existing enum
4. [ ] **Config files** - start JSON templates with nlohmann::json

---

*Last updated: See git history for this file*
