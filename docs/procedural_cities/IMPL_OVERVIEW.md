# Implementation - Overview & Order

[← Back to Index](README.md)

---

This document reorganizes the procedural cities plan from a **systems perspective**. Rather than slicing by feature (buildings, streets, walls, ports), we identify the core technical systems that underpin multiple features. This prevents duplicate implementations and ensures consistent behavior across the codebase.

## System Dependency Graph

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              RUNTIME LAYER                                       │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                 │
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                    │
│    │   Streaming  │───▶│     LOD      │───▶│   Renderer   │                    │
│    │    System    │    │    System    │    │  Integration │                    │
│    └──────────────┘    └──────────────┘    └──────────────┘                    │
│            ▲                  ▲                                                 │
├────────────┼──────────────────┼─────────────────────────────────────────────────┤
│            │    BUILD-TIME GENERATION LAYER                                      │
├────────────┼──────────────────┼─────────────────────────────────────────────────┤
│            │                  │                                                 │
│    ┌───────┴──────┐    ┌──────┴───────┐    ┌──────────────┐                    │
│    │   Settlement │    │   Building   │    │    Prop      │                    │
│    │   Assembler  │    │   Assembler  │    │   Assembler  │                    │
│    └──────┬───────┘    └──────┬───────┘    └──────┬───────┘                    │
│           │                   │                   │                             │
├───────────┼───────────────────┼───────────────────┼─────────────────────────────┤
│           │         GEOMETRY GENERATION LAYER     │                             │
├───────────┼───────────────────────────────────────┼─────────────────────────────┤
│           │                                       │                             │
│    ┌──────┴───────┐    ┌──────────────┐    ┌─────┴────────┐                    │
│    │    Layout    │    │    Shape     │    │  Placement   │                    │
│    │    System    │    │   Grammar    │    │    System    │                    │
│    └──────┬───────┘    └──────┬───────┘    └──────────────┘                    │
│           │                   │                                                 │
│    ┌──────┴───────┐    ┌──────┴───────┐                                        │
│    │    Path      │    │    Mesh      │                                        │
│    │   Network    │    │  Generation  │                                        │
│    └──────┬───────┘    └──────┬───────┘                                        │
│           │                   │                                                 │
├───────────┼───────────────────┼─────────────────────────────────────────────────┤
│           │         FOUNDATION LAYER              │                             │
├───────────┼───────────────────────────────────────┼─────────────────────────────┤
│           │                                       │                             │
│    ┌──────┴───────┐    ┌──────────────┐    ┌─────┴────────┐                    │
│    │   Terrain    │    │    Config    │    │   Spatial    │                    │
│    │ Integration  │    │    System    │    │   Utilities  │                    │
│    └──────────────┘    └──────────────┘    └──────────────┘                    │
│                                                                                 │
└─────────────────────────────────────────────────────────────────────────────────┘
```

---


---

## 5. Implementation Order

Based on system dependencies, the recommended implementation order. **Key insight**: The 2D layout work can be prototyped in TypeScript first for rapid iteration, then ported to C++.

### Phase 0: 2D Preview Foundation (TypeScript - Parallel Track)
Start browser tooling early for fast visual iteration:

1. **TypeScript Spatial Types** - Vec2, Polygon, Polyline, AABB
2. **Polygon Operations** - area, centroid, contains, bounds
3. **SVG Renderer** - Basic rendering with pan/zoom
4. **Space Colonization** - Street network algorithm
5. **Frontage Subdivision** - Lot subdivision algorithm
6. **Browser UI** - Parameter controls, layer toggles

This can run in parallel with C++ foundation work. The TypeScript version becomes the executable specification.

### Phase 1: Foundation (C++)
1. **Spatial Utilities** - Port from TypeScript, add straight skeleton
2. **Config System** - JSON schemas and registry
3. **Terrain Integration** - Height queries from existing terrain
4. **SVG Exporter** - Debug output, parity verification

### Phase 2: Core Generation (C++)
5. **Path Network** - Port from TypeScript
6. **Layout System** - Port from TypeScript, add terrain awareness
7. **Layout Importer** - Import JSON from browser tool
8. **Mesh Generation** - Extrusion, roofs (C++ only)

### Phase 3: Buildings (C++)
9. **Shape Grammar** - Building generation engine
10. **Placement System** - Props and furniture
11. **Building Assembler** - Combines grammar + placement

### Phase 4: World Assembly (C++)
12. **Settlement Assembler** - Complete settlements
13. **Agricultural/Maritime/Defensive layouts** - Extensions

### Phase 5: Runtime (C++)
14. **Streaming System** - Load/unload settlements
15. **LOD System** - Performance optimization

### Development Flow

```
Week 1-2: TypeScript 2D Preview
├── Spatial types + polygon ops
├── Space colonization algorithm
├── SVG renderer
└── Basic browser UI
     │
     │  Can now visualize and iterate on layouts!
     ▼
Week 2-4: C++ Foundation + 2D Port
├── Port spatial utilities
├── Port path network
├── Port layout system
├── Add terrain awareness
└── Verify parity with TypeScript
     │
     │  2D layout locked in, matches browser
     ▼
Week 4-6: C++ 3D Generation
├── Mesh generation (extrusion, roofs)
├── Shape grammar engine
├── Building assembler
└── First 3D buildings in engine
     │
     ▼
Week 6+: Refinement
├── More building types
├── Props and detail
├── LOD and streaming
└── Polish
```

```
Phase 1          Phase 2              Phase 3           Phase 4          Phase 5
════════         ════════             ════════          ════════         ════════

Spatial   ──────▶ Path     ──────────▶ Shape    ───────▶ Settlement ────▶ Streaming
Utilities        Network              Grammar          Assembler         System
    │               │                    │                 │
    │               │                    │                 │
Config    ──────▶ Layout   ──────────▶ Building ─────────┘
System           System               Assembler
    │               │                    │
    │               │                    │
Terrain   ──────▶ Mesh     ──────────▶ Placement
Integration      Generation           System
                                         │
                                         │
                            ┌────────────┴────────────┐
                            │                         │
                     Interior Props          Exterior Props
                     Maritime Props
```

---

## 6. Testing Strategy

Each system should be testable independently:

| System | Test Method |
|--------|-------------|
| Spatial Utilities | Unit tests with known polygons |
| Config System | Schema validation tests |
| Terrain Integration | Test with actual heightmap data |
| Path Network | Visual output of network graphs |
| Layout System | Visual output of lot layouts |
| Mesh Generation | Export to OBJ/GLB, view in Blender |
| Shape Grammar | Visual tests of building variations |
| Placement System | Visual scatter plots |
| Building Assembler | Full building export and render |
| Settlement Assembler | Full settlement render test |

**Visual testing approach**:
1. Generate test output to `generated/test/`
2. Export meshes as GLB
3. View in external tool or simple debug renderer
4. Compare against reference images

---

## 7. File Structure

```
tools/
├── common/
│   ├── SpatialUtils.h/.cpp
│   ├── ConfigSystem.h/.cpp
│   ├── TerrainInterface.h/.cpp
│   ├── MeshGeneration.h/.cpp
│   └── PlacementSystem.h/.cpp
│
├── path_generator/
│   ├── PathNetwork.h/.cpp
│   ├── SpaceColonization.h/.cpp
│   └── TerrainAwarePaths.h/.cpp
│
├── layout_generator/
│   ├── LayoutSystem.h/.cpp
│   ├── WaterfrontLayout.h/.cpp
│   ├── AgriculturalLayout.h/.cpp
│   └── DefensiveLayout.h/.cpp
│
├── building_generator/
│   ├── ShapeGrammar.h/.cpp
│   ├── ShapeRules.h/.cpp
│   ├── BuildingGrammars.h/.cpp
│   └── BuildingAssembler.h/.cpp
│
└── settlement_generator/
    ├── SettlementAssembler.h/.cpp
    └── SettlementExporter.h/.cpp

src/
└── settlements/
    ├── SettlementStreaming.h/.cpp
    ├── SettlementLOD.h/.cpp
    └── SettlementRenderer.h/.cpp

assets/
├── settlements/templates/
├── buildings/types/
├── materials/
└── props/
```

---

## 8. Shared Code Opportunities

The systems approach reveals several opportunities for code reuse:

| Functionality | Used By |
|--------------|---------|
| Polygon subdivision | Lots, rooms, fields, harbor areas |
| Path following extrusion | Streets, walls, quays, hedgerows |
| Straight skeleton | Roofs, wall tops, floor plan analysis |
| Poisson disk sampling | Props, trees, rocks, furniture |
| Shape grammar rules | All building types, towers, gates |
| Height/terrain queries | Everything with ground contact |
| LOD mesh simplification | Buildings, walls, props |
| Impostor generation | Distant buildings, trees |

By implementing these as shared systems, we avoid duplicating logic across different feature areas.

---

