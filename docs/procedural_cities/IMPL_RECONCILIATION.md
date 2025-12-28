# Implementation - Codebase Reconciliation

[← Back to Index](README.md)

---

## 10. Reconciliation with Existing Codebase

This section maps the planned systems against existing functionality to identify what can be reused, extended, or must be built from scratch.

### 10.1 Existing Systems Summary

| System | Location | Status | Reuse Potential |
|--------|----------|--------|-----------------|
| **BiomeGenerator** | `tools/biome_preprocess/BiomeGenerator.h` | ✅ Complete | High - settlement detection already works |
| **Settlement Types** | `BiomeGenerator.h:56-61` | ✅ Complete | Direct reuse - Hamlet, Village, Town, FishingVillage |
| **BiomeZone** | `BiomeGenerator.h:11-23` | ✅ Complete | Direct reuse for material selection |
| **RoadPathfinder** | `tools/road_generator/RoadPathfinder.h` | ✅ Complete | Extend for intra-settlement streets |
| **RoadSpline** | `tools/road_generator/RoadSpline.h` | ✅ Complete | Direct reuse for all path types |
| **RoadNetwork** | `RoadSpline.h:129-150` | ✅ Complete | Extend for street networks |
| **SplineRasterizer** | `tools/tile_generator/SplineRasterizer.h` | ✅ Complete | Pattern for settlement rasterization |
| **TerrainHeight** | `src/terrain/TerrainHeight.h` | ✅ Complete | Direct reuse |
| **Mesh** | `src/core/Mesh.h` | ✅ Complete | Extend with extrusion methods |
| **AABB** | `src/core/Mesh.h:11-56` | ✅ Complete | Direct reuse |
| **Vertex** | `src/core/Mesh.h:58-104` | ✅ Complete | Direct reuse |
| **TreeSystem** | `src/vegetation/TreeSystem.h` | ✅ Complete | Pattern for SettlementSystem |
| **RockSystem** | `src/vegetation/RockSystem.h` | ✅ Complete | Pattern for BuildingSystem |
| **GLTFLoader** | `src/loaders/GLTFLoader.h` | ✅ Complete | Import hand-crafted buildings |
| **TreeImpostorAtlas** | `src/vegetation/TreeImpostorAtlas.h` | ✅ Complete | Pattern for building impostors |

### 10.2 What Already Exists (Direct Reuse)

#### Settlement Detection
```cpp
// tools/biome_preprocess/BiomeGenerator.h - ALREADY EXISTS

enum class SettlementType : uint8_t {
    Hamlet = 0,        // 3-8 buildings
    Village = 1,       // 15-40 buildings
    Town = 2,          // 80-200+ buildings
    FishingVillage = 3 // 8-25 buildings (coastal)
};

struct Settlement {
    uint32_t id;
    SettlementType type;
    glm::vec2 position;         // World coordinates ✓
    float score;                // Suitability score ✓
    std::vector<std::string> features;
};
```

**Status**: Complete. BiomeGenerator already places settlements based on terrain analysis.

#### Inter-Settlement Roads
```cpp
// tools/road_generator/RoadPathfinder.h - ALREADY EXISTS

class RoadPathfinder {
public:
    // A* pathfinding with terrain awareness ✓
    bool findPath(glm::vec2 start, glm::vec2 end, std::vector<RoadControlPoint>& outPath);

    // Generate full road network connecting settlements ✓
    bool generateRoadNetwork(const std::vector<Settlement>& settlements,
                             RoadNetwork& outNetwork,
                             ProgressCallback callback = nullptr);
};
```

**Status**: Complete. Roads between settlements already generated with A* and terrain costs.

#### Road Types and Splines
```cpp
// tools/road_generator/RoadSpline.h - ALREADY EXISTS

enum class RoadType : uint8_t {
    Footpath = 0,       // 1.5m wide ✓
    Bridleway = 1,      // 3m wide ✓
    Lane = 2,           // 4m wide ✓
    Road = 3,           // 6m wide ✓
    MainRoad = 4,       // 8m wide ✓
};

struct RoadSpline {
    std::vector<RoadControlPoint> controlPoints;
    RoadType type;
    // Position sampling, width interpolation ✓
};
```

**Status**: Complete. Can extend for intra-settlement streets.

#### Terrain Height Queries
```cpp
// src/terrain/TerrainHeight.h - ALREADY EXISTS

namespace TerrainHeight {
    float toWorld(float normalizedHeight, float heightScale);
    void worldToUV(float worldX, float worldZ, float terrainSize, float& outU, float& outV);
}
```

**Status**: Complete. Standard height conversion used throughout.

#### Basic Mesh Primitives
```cpp
// src/core/Mesh.h - ALREADY EXISTS

class Mesh {
public:
    void createCube();
    void createPlane(float width, float depth);
    void createSphere(float radius, int stacks, int slices);
    void createCapsule(float radius, float height, int stacks, int slices);
    void createCylinder(float radius, float height, int segments);
    void createRock(...);      // Procedural rock mesh
    void createBranch(...);    // Procedural branch mesh
    void setCustomGeometry(const std::vector<Vertex>& verts, const std::vector<uint32_t>& inds);
};
```

**Status**: Basic primitives exist. Need to add polygon extrusion.

### 10.3 What Needs Extension

#### Extend RoadPathfinder for Streets
```cpp
// NEW: Add to RoadPathfinder or create StreetPathfinder

class StreetPathfinder : public RoadPathfinder {
public:
    // Generate street network WITHIN a settlement
    bool generateStreetNetwork(
        const Settlement& settlement,
        const std::vector<glm::vec2>& keyBuildingPositions,
        PathNetwork& outNetwork
    );

    // Use space colonization instead of just A*
    bool generateOrganicStreets(
        const std::vector<Attractor>& attractors,
        glm::vec2 entryPoint,
        PathNetwork& outNetwork
    );
};
```

#### Extend RoadSpline for All Path Types
```cpp
// NEW: Extend RoadType enum or create PathType

enum class PathType : uint8_t {
    // Existing road types
    Footpath = 0,
    Bridleway = 1,
    Lane = 2,
    Road = 3,
    MainRoad = 4,

    // NEW: Settlement streets
    MainStreet = 5,     // 8m wide
    Street = 6,         // 6m wide
    BackLane = 7,       // 3m wide
    Alley = 8,          // 2m wide

    // NEW: Defensive
    WallPath = 10,      // Wall alignment (not rendered as road)

    // NEW: Maritime
    QuayEdge = 15,      // Quay alignment
};
```

#### Extend Mesh for Building Generation
```cpp
// NEW: Add to Mesh class

class Mesh {
public:
    // NEW: Polygon extrusion
    void createExtrudedPolygon(
        const std::vector<glm::vec2>& polygon,
        float height,
        bool capTop = true,
        bool capBottom = true
    );

    // NEW: Path extrusion (for walls, quays)
    void createExtrudedPath(
        const std::vector<glm::vec2>& path,
        float width,
        float height
    );

    // NEW: Roof from footprint
    void createRoof(
        const std::vector<glm::vec2>& footprint,
        float pitch,
        RoofType type
    );
};
```

### 10.4 What Must Be Built New

| Component | Description | Priority |
|-----------|-------------|----------|
| **Polygon2D** | 2D polygon with area, centroid, contains, subdivision | Phase 0 |
| **FrontageSubdivision** | Subdivide blocks into lots along street frontage | Phase 0 |
| **SpaceColonization** | Street network generation algorithm | Phase 0 |
| **BlockIdentification** | Find enclosed areas between streets | Phase 1 |
| **ShapeGrammar** | CGA-style building geometry rules | Phase 3 |
| **RoofGenerator** | Straight skeleton → roof mesh | Phase 3 |
| **SettlementSystem** | Runtime system (like TreeSystem) | Phase 5 |
| **BuildingLOD** | LOD meshes and impostors | Phase 5 |
| **ConfigRegistry** | JSON config loading and validation | Phase 1 |
| **SVGExporter** | Debug visualization output | Phase 0 |
| **Web tools** | TypeScript + browser UI | Phase 0 |

### 10.5 Integration Points

#### Using Existing Settlement Data
```cpp
// BiomeGenerator already produces this - just consume it
const BiomeResult& biomes = biomeGenerator.getResult();

for (const Settlement& settlement : biomes.settlements) {
    // settlement.position  - world XZ coordinates
    // settlement.type      - Hamlet/Village/Town/FishingVillage
    // settlement.score     - terrain suitability

    SettlementLayout layout = layoutGenerator.generate(settlement, ...);
}
```

#### Using Existing Road Network
```cpp
// RoadPathfinder already produces this
RoadNetwork externalRoads;
pathfinder.generateRoadNetwork(settlements, externalRoads);

// Use road endpoints as street network entry points
for (const RoadSpline& road : externalRoads.roads) {
    if (road.toSettlementId == settlement.id) {
        glm::vec2 entryPoint = road.controlPoints.back().position;
        streetGenerator.addEntryPoint(entryPoint);
    }
}
```

#### Following TreeSystem/RockSystem Patterns
```cpp
// Pattern from TreeSystem - follow for SettlementSystem
class SettlementSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        std::string resourcePath;
        std::function<float(float, float)> getTerrainHeight;  // Same as TreeSystem
        float terrainSize;
    };

    static std::unique_ptr<SettlementSystem> create(const InitInfo& info);

    const std::vector<Renderable>& getBuildingRenderables() const;
    const std::vector<Renderable>& getWallRenderables() const;
    const std::vector<Renderable>& getRoadRenderables() const;
};
```

### 10.6 Recommended Implementation Order (Revised)

Based on existing code analysis:

```
PHASE 0: TypeScript 2D Preview (NEW - no existing code)
├── Polygon2D class
├── Frontage subdivision
├── Space colonization
├── SVG renderer
└── Browser UI

PHASE 1: C++ Foundation
├── Port Polygon2D from TypeScript
├── Extend RoadSpline for PathType
├── SVGExporter for C++ debugging
└── ConfigRegistry (NEW)

PHASE 2: Layout System (builds on existing)
├── Extend RoadPathfinder → StreetPathfinder
├── BlockIdentification (NEW)
├── LotSubdivision (NEW)
└── Integrate with BiomeGenerator settlements

PHASE 3: Building Generation (NEW)
├── Extend Mesh with extrusion methods
├── RoofGenerator (straight skeleton)
├── ShapeGrammar engine
└── BuildingAssembler

PHASE 4: Runtime (follows TreeSystem pattern)
├── SettlementSystem (like TreeSystem)
├── BuildingLOD (like TreeImpostorAtlas)
└── Streaming integration
```

### 10.7 Code Locations for New Systems

```
tools/
├── common/
│   ├── Polygon2D.h/.cpp          # NEW
│   ├── SVGExporter.h/.cpp        # NEW
│   └── ConfigRegistry.h/.cpp     # NEW
│
├── road_generator/
│   ├── RoadSpline.h              # EXTEND: Add PathType
│   ├── RoadPathfinder.h          # EXTEND: Add street generation
│   └── StreetGenerator.h/.cpp    # NEW: Space colonization
│
├── layout_generator/             # NEW DIRECTORY
│   ├── BlockIdentification.h/.cpp
│   ├── LotSubdivision.h/.cpp
│   └── SettlementLayout.h/.cpp
│
└── building_generator/           # NEW DIRECTORY
    ├── ShapeGrammar.h/.cpp
    ├── RoofGenerator.h/.cpp
    └── BuildingAssembler.h/.cpp

src/
├── core/
│   └── Mesh.h/.cpp               # EXTEND: Add extrusion methods
│
└── settlements/                  # NEW DIRECTORY
    ├── SettlementSystem.h/.cpp
    ├── SettlementLOD.h/.cpp
    └── SettlementStreaming.h/.cpp

web-tools/                        # NEW DIRECTORY
├── package.json
├── src/
│   ├── spatial/
│   ├── paths/
│   ├── layout/
│   ├── renderer/
│   └── ui/
└── tests/
```

### 10.8 Risk Assessment

| Risk | Mitigation |
|------|------------|
| Polygon operations are complex | Use established library (Clipper2) or simplified implementations |
| Straight skeleton is hard to implement | Start with simple gable roofs, add skeleton later |
| Street generation may conflict with external roads | Use road endpoints as constraints, validate connectivity |
| Performance with many buildings | Follow TreeSystem impostor pattern, aggressive LOD |
| Coordinate system confusion | Use existing TerrainHeight utilities consistently |

### 10.9 Quick Wins

These can be implemented immediately with minimal new code:

1. **SVG Export from Existing Data**
   - Export BiomeGenerator settlements as colored circles
   - Export RoadNetwork as polylines
   - View in browser immediately

2. **Blockout Buildings as Cubes**
   - Use existing Mesh::createCube()
   - Place at settlement positions with random offsets
   - Visible in engine immediately

3. **Extend RoadType**
   - Add Street/Lane/Alley to existing enum
   - Existing spline rendering works automatically

4. **Config Files**
   - Start with JSON templates
   - Parse with nlohmann::json (already in dependencies)
