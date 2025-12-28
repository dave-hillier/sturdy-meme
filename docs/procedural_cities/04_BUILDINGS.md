# Procedural Cities - Phase 3: Building Generation

[← Back to Index](README.md)

## 5. Phase 3: Building Exterior Generation

### 5.1 Building Grammar (CGA-Style)

The building generation uses a shape grammar inspired by CityEngine's CGA, but simplified for our needs.

#### 5.1.1 Grammar Overview

```
Building → Footprint → Mass → Floors → Roof
                              ↓
                           Facade → Walls → Windows/Doors
                                          → Trim/Details
```

#### 5.1.2 Shape Operations

```cpp
// Core shape operations
enum class ShapeOp {
    Extrude,        // Extrude 2D shape to 3D
    Split,          // Divide shape along axis
    Repeat,         // Repeat element along axis
    ComponentSplit, // Split by face type (front, side, roof, etc.)
    Offset,         // Inset/outset shape boundary
    Roof,           // Generate roof from footprint
    Texture,        // Apply texture coordinates
    SetMaterial,    // Assign material
    Insert,         // Insert component at point
    Scope,          // Transform scope
    NIL             // Delete shape
};

struct ShapeNode {
    std::string name;
    glm::vec3 position;         // Local origin
    glm::vec3 size;             // Bounding dimensions
    glm::mat4 transform;        // Full transform

    enum class Type {
        Volume,                 // 3D volume
        Face,                   // 2D face
        Edge,                   // 1D edge
        Point                   // 0D point
    } type;

    std::vector<Vertex> geometry;
    std::string materialId;
};
```

#### 5.1.3 Example Building Rules

```cpp
// rules/cottage.grammar

// Start with lot, produce building
Rule("Lot") {
    setback(front: 2, sides: 1.5, rear: 3)
    Footprint
}

Rule("Footprint") {
    // Choose footprint shape based on lot
    case lot.aspect > 1.5: LShape(0.3)    // L-shaped for deep lots
    case lot.aspect > 1.2: Rectangle
    else: Rectangle
}

Rule("Rectangle") {
    extrude(floors * floorHeight) Mass
}

Rule("LShape", wingRatio) {
    split(x) {
        wingRatio: WingFootprint
        ~1: MainFootprint
    }
}

Rule("Mass") {
    split(y) {
        groundFloorHeight: GroundFloor
        repeat(upperFloorHeight): UpperFloor
        ~1: Attic
    }
}

Rule("GroundFloor") {
    componentSplit {
        front: GroundFrontFacade
        side: GroundSideFacade
        rear: RearFacade
        top: FloorPlate
    }
}

Rule("GroundFrontFacade") {
    split(x) {
        ~1: Wall
        doorWidth: Door
        ~1: Wall | Window(groundWindowStyle)
    }
}

Rule("Roof") {
    case roofType == "gable": GableRoof(pitch)
    case roofType == "hipped": HippedRoof(pitch)
    case roofType == "half_hipped": HalfHippedRoof(pitch)
}
```

### 5.2 Footprint Generation

#### 5.2.1 Footprint Shapes

```cpp
class FootprintGenerator {
public:
    enum class Shape {
        Rectangle,
        LShape,
        TShape,
        UShape,
        Irregular
    };

    struct Config {
        Shape shape;
        glm::vec2 dimensions;
        float wingWidth = 0.3f;         // For L/T/U shapes
        float irregularity = 0.0f;      // Random vertex displacement
    };

    Polygon generate(const Config& config, uint64_t seed);

private:
    Polygon generateRectangle(const Config& config);
    Polygon generateLShape(const Config& config, uint64_t seed);
    Polygon generateTShape(const Config& config, uint64_t seed);
    Polygon generateIrregular(const Config& config, uint64_t seed);
};
```

#### 5.2.2 Footprint Fitting

```cpp
class FootprintFitter {
public:
    // Fit footprint to lot constraints
    Polygon fitToLot(
        const Polygon& footprint,
        const BuildingLot& lot,
        float frontSetback,
        float sideSetback,
        float rearSetback
    );

    // Rotate footprint to face street
    Polygon alignToStreet(const Polygon& footprint, const BuildingLot& lot);
};
```

### 5.3 Roof Generation

#### 5.3.1 Straight Skeleton Algorithm

For complex footprints, we use the straight skeleton to generate proper roof geometry:

```cpp
class RoofGenerator {
public:
    enum class Type {
        Flat,
        Shed,
        Gable,
        Hipped,
        HalfHipped,         // Jerkinhead
        Gambrel,
        Mansard,
        Cross               // Multiple gables
    };

    struct Config {
        Type type;
        float pitch;                // Roof angle in degrees
        float overhang;             // Eave overhang distance
        bool dormers = false;
        int dormerCount = 0;
        std::string materialId;
    };

    Mesh generate(const Polygon& footprint, const Config& config);

private:
    // Straight skeleton computation
    std::vector<glm::vec2> computeStraightSkeleton(const Polygon& footprint);

    // Generate gable from skeleton
    Mesh generateGable(const Polygon& footprint, float pitch);

    // Generate hipped from skeleton
    Mesh generateHipped(const Polygon& footprint, float pitch);

    // Add dormers to roof surface
    void addDormers(Mesh& roof, const Config& config);
};
```

### 5.4 Facade Generation

#### 5.4.1 Facade Grammar

```cpp
class FacadeGenerator {
public:
    struct Style {
        std::string wallMaterial;
        std::string trimMaterial;

        struct WindowStyle {
            float width, height;
            float sillHeight;
            std::string frameType;
            std::string glassType;
            bool shutters;
        } windowStyle;

        struct DoorStyle {
            float width, height;
            std::string type;           // "plank", "battened", "paneled"
            bool porch;
        } doorStyle;

        float beamExposure;             // Timber frame exposure (0-1)
    };

    Mesh generate(
        const ShapeNode& facadeShape,
        const Style& style,
        uint64_t seed
    );

private:
    void subdivideFacade(ShapeNode& shape, const Style& style);
    void placeWindows(ShapeNode& wallSection, const Style& style);
    void placeDoor(ShapeNode& wallSection, const Style& style);
    void addTimberFrame(Mesh& facade, float exposure);
};
```

#### 5.4.2 Wave Function Collapse for Details

For coherent detail placement (window arrangements, decorative elements):

```cpp
class WFCFacadeDetail {
public:
    struct Tile {
        std::string id;
        Mesh geometry;
        std::array<std::string, 4> sockets;  // top, right, bottom, left
        float weight;
    };

    struct Config {
        std::vector<Tile> tileset;
        int gridWidth, gridHeight;
    };

    // Generate detail placement using WFC
    std::vector<std::string> generate(const Config& config, uint64_t seed);

private:
    // Standard WFC implementation
    void propagate();
    int findLowestEntropy();
    void collapse(int cell);
    bool backtrack();
};
```

### 5.5 Building Component Library

#### 5.5.1 Architectural Components

```cpp
// Pre-modeled components loaded at build time
struct ComponentLibrary {
    // Structural
    std::map<std::string, Mesh> chimneys;
    std::map<std::string, Mesh> dormers;
    std::map<std::string, Mesh> porches;

    // Openings
    std::map<std::string, Mesh> windows;
    std::map<std::string, Mesh> doors;
    std::map<std::string, Mesh> shutters;

    // Decorative
    std::map<std::string, Mesh> cornices;
    std::map<std::string, Mesh> brackets;
    std::map<std::string, Mesh> bargeboards;

    // Materials
    std::map<std::string, Material> materials;
};
```

### 5.6 Material Generation

#### 5.6.1 Procedural Materials

```cpp
class BuildingMaterialGenerator {
public:
    // Generate tiling wall textures
    void generateWallTexture(
        const std::string& type,        // "wattle_daub", "flint", "brick", etc.
        const std::string& outputPath,
        uint32_t size = 1024,
        uint64_t seed = 0
    );

    // Generate roof tile textures
    void generateRoofTexture(
        const std::string& type,        // "thatch", "slate", "clay_tile"
        const std::string& outputPath,
        uint32_t size = 1024,
        uint64_t seed = 0
    );

private:
    // Substance-style node graph evaluation
    void evaluateMaterialGraph(const MaterialGraph& graph);
};
```

### 5.7 Mesh Output

#### 5.7.1 Combined Building Mesh

```cpp
struct BuildingMesh {
    // Geometry
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Submeshes by material
    struct Submesh {
        uint32_t indexOffset;
        uint32_t indexCount;
        std::string materialId;
    };
    std::vector<Submesh> submeshes;

    // LOD variants
    std::vector<BuildingMesh> lodLevels;    // Lower detail versions

    // Collision
    std::vector<glm::vec3> collisionHull;   // Simplified collision shape

    // Metadata
    std::string buildingTypeId;
    glm::vec3 boundingBoxMin;
    glm::vec3 boundingBoxMax;
};
```

### 5.8 Deliverables - Phase 3

- [ ] FootprintGenerator with all shape types
- [ ] RoofGenerator with straight skeleton
- [ ] FacadeGenerator with grammar system
- [ ] WFCFacadeDetail for coherent details
- [ ] ComponentLibrary loader
- [ ] BuildingMaterialGenerator for procedural textures
- [ ] Mesh export to GLB format
- [ ] Building type configs for all required types

**Testing**: Generate sample buildings of each type, render in engine:
- Cottage, farmhouse, barn (hamlet)
- Church, inn, smithy, market stall (village)
- Guildhall, townhouse, warehouse (town)
- Net shed, quay building, fish market (fishing village)

---

## 5b. Phase 3b: Building Interior Generation

### 5b.1 Floor Plan Generation

Interior layout generation using space partitioning algorithms.

#### 5b.1.1 Room Types by Building

| Building Type | Rooms | Notes |
|--------------|-------|-------|
| Peasant Cottage | Single hall, loft | Open plan, central hearth |
| Longhouse | Hall, cross-passage, byre | Animals separated by passage |
| Cruck Hall | Hall, solar, buttery, pantry | High-status open hall |
| Stone House | Ground floor (storage/workshop), upper hall, chamber | Vertical separation |
| Parish Church | Nave, chancel, porch, tower base | Ritual sequence |
| Inn | Hall/drinking room, kitchen, chambers, stable | Multiple functions |
| Smithy | Forge area, storage | Open work area |
| Mill | Grinding floor, storage, machinery space | Functional layout |

#### 5b.1.2 Space Partitioning Algorithm

```cpp
class FloorPlanGenerator {
public:
    enum class PartitionMethod {
        None,               // Single room (cottages)
        CrossPassage,       // Longhouse style - single bisection
        Medieval,           // Hall + services + chambers
        Ecclesiastical,     // Nave/chancel/aisles
        Commercial          // Shop front + back rooms
    };

    struct RoomSpec {
        std::string name;
        float minArea;
        float maxArea;
        float aspectRatioMin;
        float aspectRatioMax;
        bool requiresExteriorWall;  // Needs window
        bool requiresHearth;
        std::vector<std::string> adjacentTo;  // Required adjacencies
    };

    struct FloorPlan {
        std::vector<Room> rooms;
        std::vector<Wall> interiorWalls;
        std::vector<Opening> doorways;
        std::vector<Opening> windows;
        glm::vec2 hearthPosition;
    };

    FloorPlan generate(
        const Polygon& footprint,
        const std::vector<RoomSpec>& requiredRooms,
        PartitionMethod method,
        uint64_t seed
    );

private:
    // Binary Space Partition for rectangular division
    void bspPartition(const Polygon& space, std::vector<Room>& rooms);

    // Squarified treemap for more regular rooms
    void treemapPartition(const Polygon& space,
                          const std::vector<float>& areas,
                          std::vector<Room>& rooms);

    // Validate room adjacencies
    bool validateAdjacencies(const FloorPlan& plan,
                            const std::vector<RoomSpec>& specs);
};
```

#### 5b.1.3 Room Definition

```cpp
struct Room {
    std::string typeId;             // "hall", "solar", "byre", etc.
    Polygon boundary;               // 2D floor polygon
    float floorHeight;              // Y offset from building base
    float ceilingHeight;            // Room height

    enum class FloorType {
        Earth,          // Beaten earth (common)
        Stone,          // Stone flags (churches, wealthy)
        Timber,         // Timber boards (upper floors)
        Rush            // Rush-strewn earth
    } floorType;

    bool hasHearth;
    glm::vec2 hearthPosition;

    bool hasLoft;                   // Storage loft above
    float loftHeight;

    std::vector<uint32_t> connectedRooms;  // Doorway connections
};
```

### 5b.2 Interior Architectural Elements

#### 5b.2.1 Structural Elements

```cpp
struct InteriorStructure {
    // Cruck frames (visible A-frames)
    struct CruckBlade {
        glm::vec2 basePosition;
        float height;
        float curve;                // Curvature of blade
    };
    std::vector<CruckBlade> cruckBlades;

    // Timber posts and beams
    struct TimberFrame {
        glm::vec3 start;
        glm::vec3 end;
        float width;
        float height;
    };
    std::vector<TimberFrame> beams;

    // Stone pillars (churches)
    struct Pillar {
        glm::vec2 position;
        float radius;
        float height;
        enum class Style { Round, Octagonal, Clustered } style;
    };
    std::vector<Pillar> pillars;
};
```

#### 5b.2.2 Central Hearth

Medieval buildings typically had central hearths (chimneys were rare before 1300):

```cpp
struct Hearth {
    glm::vec2 position;
    float width;
    float depth;

    enum class Type {
        Open,           // Simple fire on floor
        Raised,         // Stone platform
        Brazier         // Portable metal container
    } type;

    bool hasSmokeHole;          // Hole in roof above
    bool hasHoodLouvre;         // Timber smoke hood
};

class HearthGenerator {
public:
    Mesh generateHearth(const Hearth& hearth);
    Mesh generateSmokeHood(const Hearth& hearth, float ceilingHeight);

    // Smoke staining effect on roof interior
    void generateSmokeStaining(
        Mesh& roofInterior,
        const Hearth& hearth,
        float intensity
    );
};
```

### 5b.3 Interior Wall Surfaces

#### 5b.3.1 Wall Treatments

```cpp
enum class WallTreatment {
    ExposeTimber,       // Visible timber frame + infill
    Limewash,           // White lime wash (common)
    Painted,            // Decorative painting (churches)
    Hung,               // Textile hangings (wealthy)
    Bare                // Exposed stone/daub
};

class InteriorWallGenerator {
public:
    Mesh generate(
        const Room& room,
        WallTreatment treatment,
        bool hasTimberFrame
    );

private:
    void addTimberFrame(Mesh& wall, const std::vector<TimberFrame>& frames);
    void addWattleTexture(Mesh& wall);  // Visible between timbers
    void addLimewash(Mesh& wall);
};
```

### 5b.4 Interior Props and Furniture

#### 5b.4.1 Furniture by Room Type

| Room Type | Essential Furniture | Optional Furniture |
|-----------|--------------------|--------------------|
| Hall | Trestle table, benches, hearth | Chest, candlesticks |
| Solar | Bed, chest | Chair, prie-dieu |
| Kitchen | Work table, cauldron, spit | Barrels, shelves |
| Byre | Stalls, mangers | Hay racks |
| Church Nave | Benches/none, font | Lectern |
| Church Chancel | Altar, sedilia | Reredos, piscina |
| Inn Hall | Tables, benches, barrels | Fireplace, tap |
| Smithy | Anvil, forge, bellows, water trough | Tool racks |

#### 5b.4.2 Furniture Placement Algorithm

```cpp
class InteriorPropPlacer {
public:
    struct FurnitureRule {
        std::string propId;
        std::string roomType;

        enum class Placement {
            Center,             // Center of room
            Wall,               // Against wall
            Corner,             // In corner
            NearHearth,         // Close to fire
            NearWindow,         // By window for light
            NearDoor,           // By entrance
            Custom              // Specific position logic
        } placement;

        float minClearance;     // Space around item
        bool required;
        float probability;
    };

    std::vector<PropPlacement> place(
        const Room& room,
        const std::vector<FurnitureRule>& rules,
        uint64_t seed
    );

private:
    // Place furniture avoiding collisions
    bool tryPlace(
        const FurnitureRule& rule,
        const Room& room,
        std::vector<PropPlacement>& placed
    );

    // Find valid wall positions
    std::vector<glm::vec2> findWallPositions(
        const Room& room,
        float itemWidth,
        float clearance
    );
};
```

#### 5b.4.3 Furniture Models

```json
// assets/props/furniture/trestle_table.json
{
    "id": "trestle_table",
    "category": "furniture",
    "applicable_rooms": ["hall", "inn_hall", "kitchen"],

    "dimensions": {
        "width": [1.5, 3.0],
        "depth": 0.8,
        "height": 0.75
    },

    "construction": {
        "material": "oak",
        "style": "trestle"
    },

    "placement": {
        "preferred": "center",
        "orientation": "long_axis_to_door",
        "clearance": 0.8
    },

    "lod": {
        "lod0_triangles": 500,
        "lod1_triangles": 100,
        "impostor_distance": 15
    }
}
```

### 5b.5 Interior Lighting

#### 5b.5.1 Light Sources

```cpp
struct InteriorLightSource {
    enum class Type {
        Hearth,             // Central fire
        Candle,             // Single candle
        Candlestick,        // Multi-candle holder
        Rushlight,          // Rush dipped in fat
        Cresset,            // Oil lamp
        Window              // Daylight through window
    } type;

    glm::vec3 position;
    glm::vec3 color;
    float intensity;
    float radius;
    float flicker;          // Flicker intensity for flames
};

class InteriorLightingSystem {
public:
    std::vector<InteriorLightSource> generate(
        const Room& room,
        float timeOfDay,        // 0-24 hours
        float wealthLevel       // Affects number of candles
    );

    // Bake ambient occlusion for room corners
    void bakeAO(Mesh& roomMesh);
};
```

#### 5b.5.2 Window Light

```cpp
struct WindowLight {
    glm::vec3 position;
    glm::vec3 direction;        // Light direction
    glm::vec2 size;             // Window dimensions

    bool hasGlass;              // Rare in medieval period
    bool hasOiledLinen;         // Translucent covering
    bool hasShutter;            // Solid shutter

    // Light shaft visualization
    bool renderLightShaft;
    float dustParticleDensity;
};
```

### 5b.6 Interior/Exterior Transition

#### 5b.6.1 Portal System

```cpp
struct Portal {
    glm::vec3 position;
    glm::vec2 size;
    glm::quat orientation;

    enum class State {
        Open,
        Closed,
        Broken
    } state;

    // Visibility determination
    uint32_t interiorZoneId;
    uint32_t exteriorZoneId;

    // For rendering
    bool renderDoorMesh;
    std::string doorMeshId;
};

class PortalSystem {
public:
    // Determine visible zones from camera position
    std::vector<uint32_t> getVisibleZones(
        const Camera& camera,
        const std::vector<Portal>& portals
    );

    // Cull interior when door closed and outside
    bool shouldRenderInterior(
        const Building& building,
        const Camera& camera
    );
};
```

### 5b.7 Deliverables - Phase 3b

- [ ] FloorPlanGenerator with partition methods
- [ ] Room type definitions for all building types
- [ ] InteriorStructure generation (crucks, beams, pillars)
- [ ] HearthGenerator with smoke hood
- [ ] InteriorWallGenerator with treatments
- [ ] InteriorPropPlacer with furniture rules
- [ ] Furniture model library
- [ ] InteriorLightingSystem
- [ ] PortalSystem for visibility

**Testing**: For each building type:
- Walk through interior and verify room layout
- Check furniture placement is sensible
- Verify lighting responds to time of day
- Test portal visibility culling

---
