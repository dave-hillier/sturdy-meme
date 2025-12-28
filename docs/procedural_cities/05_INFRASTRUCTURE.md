# Procedural Cities - Phase 4: Infrastructure

[← Back to Index](README.md)

## 6. Phase 4: Street & Infrastructure Networks

### 6.1 Street Mesh Generation

#### 6.1.1 Street Surface

```cpp
class StreetMeshGenerator {
public:
    struct Config {
        float curb_height = 0.15f;
        float camber = 0.02f;           // Cross-slope for drainage
        bool cobblestones = true;
        float tessellation = 2.0f;      // Vertices per meter
    };

    Mesh generate(
        const StreetNetwork& network,
        const TerrainAnalysis& terrain,
        const Config& config
    );

private:
    // Generate road surface following terrain
    void generateSurface(const StreetSegment& segment, Mesh& mesh);

    // Generate curbs along edges
    void generateCurbs(const StreetSegment& segment, Mesh& mesh);

    // Generate intersection geometry
    void generateIntersection(
        const std::vector<const StreetSegment*>& segments,
        Mesh& mesh
    );
};
```

#### 6.1.2 Street Materials

```json
// assets/materials/streets/cobblestone.json
{
    "id": "cobblestone",
    "albedo": "textures/streets/cobblestone_albedo.png",
    "normal": "textures/streets/cobblestone_normal.png",
    "roughness": 0.8,
    "ao": "textures/streets/cobblestone_ao.png",
    "displacement": "textures/streets/cobblestone_height.png",
    "displacement_scale": 0.05,
    "tiling": [4, 4]
}
```

### 6.2 Infrastructure Elements

#### 6.2.1 Wells and Pumps

```cpp
struct WellPlacement {
    glm::vec2 position;
    float rotation;
    enum class Type { Well, Pump, Trough } type;
    bool isShared;                      // Shared between lots
};

class WellPlacer {
public:
    std::vector<WellPlacement> place(
        const std::vector<BuildingLot>& lots,
        const StreetNetwork& streets,
        float maxDistanceBetweenWells = 60.0f
    );
};
```

#### 6.2.2 Fences and Walls

```cpp
class FenceGenerator {
public:
    enum class Type {
        PicketFence,
        PostAndRail,
        DryStoneWall,
        HedgeRow,
        WattleFence
    };

    Mesh generate(
        const std::vector<glm::vec2>& path,
        Type type,
        float height
    );
};
```

#### 6.2.3 Street Furniture

```cpp
struct StreetFurniture {
    enum class Type {
        Signpost,
        Hitching_Post,
        Mounting_Block,
        Market_Cross,
        Stocks,             // Medieval punishment device
        Lamp_Post,
        Bench
    };

    glm::vec3 position;
    float rotation;
    Type type;
};

class StreetFurniturePlacer {
public:
    std::vector<StreetFurniture> place(
        const SettlementDefinition& settlement,
        const StreetNetwork& streets,
        const std::vector<BuildingLot>& lots
    );
};
```

### 6.3 Special Structures

#### 6.3.1 Bridges

```cpp
class BridgeGenerator {
public:
    enum class Type {
        Stone_Arch,
        Timber_Beam,
        Clapper                 // Simple stone slab
    };

    Mesh generate(
        glm::vec2 start,
        glm::vec2 end,
        float waterLevel,
        Type type
    );
};
```

#### 6.3.2 Town Walls (for Towns)

```cpp
class TownWallGenerator {
public:
    struct Config {
        float wallHeight = 6.0f;
        float wallThickness = 2.0f;
        float towerSpacing = 50.0f;
        float towerHeight = 10.0f;
        int gateCount = 2;
    };

    struct WallSegment {
        std::vector<glm::vec2> path;
        bool hasTower;
        bool isGate;
    };

    std::vector<WallSegment> generate(
        const SettlementDefinition& settlement,
        const StreetNetwork& streets,
        const Config& config
    );

    Mesh generateMesh(const std::vector<WallSegment>& segments);
};
```

### 6.4 Deliverables - Phase 4

- [ ] StreetMeshGenerator with intersections
- [ ] Street texture generation (cobblestone, dirt, gravel)
- [ ] WellPlacer and well/pump models
- [ ] FenceGenerator with all types
- [ ] StreetFurniturePlacer
- [ ] BridgeGenerator
- [ ] TownWallGenerator (for town settlements)

**Testing**: Generate complete village layout with:
- Connected street network rendered
- Fences around properties
- Wells distributed appropriately
- Street furniture at key locations

---

## 6b. Phase 4b: Defensive Structures

### 6b.1 Overview

Defensive structures are critical for south coast settlements. This phase covers walls, gates, towers, castles, and harbor defenses.

### 6b.2 Town Walls

#### 6b.2.1 Wall Generation

```cpp
struct TownWall {
    std::vector<glm::vec2> path;        // Wall centerline
    float height = 6.0f;                // Wall height
    float thickness = 2.0f;             // Wall thickness
    float wallWalkWidth = 1.5f;         // Width of parapet walk

    // Construction
    enum class Construction {
        Stone,              // Full stone (wealthy towns)
        StoneRubble,        // Rubble core with stone facing
        FlintWithDressing,  // Flint with stone quoins (regional)
        EarthAndTimber      // Earth bank with timber palisade (early/temporary)
    } construction;

    // Features
    bool hasMerlons = true;             // Crenellations
    bool hasArrowLoops = true;
    float merlonHeight = 1.0f;
    float merlonWidth = 0.8f;
    float crenelWidth = 0.5f;
};

class TownWallGenerator {
public:
    struct Config {
        float towerSpacing = 40.0f;         // Distance between towers
        float gateMinSpacing = 100.0f;      // Min distance between gates
        int maxGates = 4;
        float wallSetback = 5.0f;           // Distance from buildings to wall
    };

    // Generate wall path around settlement
    std::vector<glm::vec2> generateWallPath(
        const SettlementDefinition& settlement,
        const std::vector<BuildingLot>& lots,
        const TerrainAnalysis& terrain
    );

    // Generate wall mesh with towers
    WallSystem generate(
        const std::vector<glm::vec2>& path,
        const Config& config,
        uint64_t seed
    );

private:
    // Find optimal tower positions
    std::vector<glm::vec2> placeTowers(
        const std::vector<glm::vec2>& path,
        float spacing
    );

    // Find gate positions (at road entries)
    std::vector<GatePosition> placeGates(
        const std::vector<glm::vec2>& path,
        const StreetNetwork& streets,
        const RoadNetwork& externalRoads
    );

    // Adapt wall to terrain (follow contours, cut through slopes)
    void adaptToTerrain(
        std::vector<glm::vec2>& path,
        const TerrainAnalysis& terrain
    );
};
```

#### 6b.2.2 Wall Segments

```cpp
struct WallSegment {
    glm::vec2 start;
    glm::vec2 end;
    TownWall::Construction construction;

    // Terrain adaptation
    float startHeight;          // Ground level at start
    float endHeight;            // Ground level at end
    bool requiresCut;           // Needs terrain excavation
    bool requiresFill;          // Needs foundation fill

    // Features along segment
    bool hasWallWalk;
    std::vector<float> arrowLoopPositions;  // Parametric positions [0,1]
};

class WallMeshGenerator {
public:
    Mesh generateSegment(const WallSegment& segment, const TownWall& config);
    Mesh generateMerlons(const WallSegment& segment, const TownWall& config);
    Mesh generateWallWalk(const WallSegment& segment, const TownWall& config);
};
```

### 6b.3 Towers

#### 6b.3.1 Tower Types

```cpp
enum class TowerType {
    Interval,           // Regular wall tower (rectangular or D-shaped)
    Corner,             // Corner tower (larger, often round)
    Gate,               // Gate tower (flanking gate passage)
    Keep,               // Main defensive tower (castle)
    Watchtower,         // Standalone observation post
    Beacon              // Coastal warning beacon
};

struct Tower {
    glm::vec2 position;
    TowerType type;
    float rotation;             // Facing direction

    // Dimensions
    float width = 6.0f;
    float depth = 6.0f;
    float height = 12.0f;       // Total height

    // Shape
    enum class Shape {
        Square,
        Rectangular,
        Round,
        DShaped,                // Flat inside, curved outside
        Polygonal               // Octagonal, etc.
    } shape;

    // Features
    int floors = 3;
    bool hasBattlement = true;
    bool hasMachicolation = false;  // Overhanging parapet (later period)
    bool hasRoof = false;           // Some towers have conical roofs

    // Interior access
    bool hasInterior = true;
    glm::vec2 doorPosition;         // Access from wall walk
};

class TowerGenerator {
public:
    Tower generate(
        TowerType type,
        glm::vec2 position,
        const TownWall& wallConfig,
        uint64_t seed
    );

    Mesh generateExterior(const Tower& tower);
    FloorPlan generateInterior(const Tower& tower);
};
```

### 6b.4 Gates

#### 6b.4.1 Gate Types

```cpp
enum class GateType {
    Simple,             // Single arch, no towers
    Towered,            // Flanking towers
    Barbican,           // Extended gate complex
    Postern,            // Small secondary gate
    Water               // Water gate (harbor access)
};

struct TownGate {
    glm::vec2 position;
    float rotation;             // Perpendicular to wall
    GateType type;

    // Passage
    float passageWidth = 4.0f;
    float passageHeight = 5.0f;
    int archType;               // 0=round (Norman), 1=pointed (Gothic)

    // Defenses
    bool hasPortcullis = true;
    bool hasDrawbridge = false; // If over ditch
    bool hasMurderHoles = true; // Holes in passage ceiling
    int doorCount = 2;          // Outer and inner doors

    // Flanking towers
    bool hasTowers = true;
    Tower leftTower;
    Tower rightTower;

    // Barbican (extended outer defense)
    bool hasBarbican = false;
    float barbicanLength = 15.0f;
};

class GateGenerator {
public:
    TownGate generate(
        GateType type,
        glm::vec2 position,
        float rotation,
        const TownWall& wallConfig,
        uint64_t seed
    );

    Mesh generateExterior(const TownGate& gate);
    FloorPlan generateInterior(const TownGate& gate);

private:
    Mesh generatePortcullis(const TownGate& gate);
    Mesh generateDrawbridge(const TownGate& gate);
};
```

### 6b.5 Castles

#### 6b.5.1 Castle Types

```cpp
enum class CastleType {
    MotteAndBailey,     // Earth mound + enclosed yard (early Norman)
    ShellKeep,          // Stone wall on motte top
    RectangularKeep,    // Classic Norman tower keep
    RoundKeep,          // Cylindrical keep (later)
    Concentric,         // Multiple wall circuits (advanced)
    Coastal             // Adapted for harbor defense
};

struct Castle {
    glm::vec2 position;
    CastleType type;
    float rotation;

    // Keep (main tower)
    struct Keep {
        glm::vec2 position;
        float width, depth, height;
        int floors;
        Tower::Shape shape;
        bool hasCornerTurrets;
        bool hasForebuilding;       // Protected entrance structure
    } keep;

    // Bailey (enclosed courtyard)
    struct Bailey {
        std::vector<glm::vec2> wallPath;
        float wallHeight;
        std::vector<Tower> towers;
        std::vector<TownGate> gates;

        // Buildings within bailey
        std::vector<BuildingPlacement> buildings;  // Hall, chapel, stables, etc.
    };
    std::vector<Bailey> baileys;    // Can have inner and outer bailey

    // Motte (earth mound) - for motte-and-bailey
    struct Motte {
        glm::vec2 center;
        float baseRadius;
        float topRadius;
        float height;
        bool hasShellWall;
    };
    std::optional<Motte> motte;

    // Ditch/moat
    struct Ditch {
        std::vector<glm::vec2> path;
        float width;
        float depth;
        bool isWet;                 // Water-filled moat
    };
    std::optional<Ditch> ditch;
};

class CastleGenerator {
public:
    struct Config {
        CastleType type;
        float sizeMultiplier = 1.0f;
        bool generateInteriors = true;
        float age = 0.5f;           // 0=new, 1=ancient (affects weathering)
    };

    Castle generate(
        glm::vec2 position,
        const TerrainAnalysis& terrain,
        const Config& config,
        uint64_t seed
    );

private:
    // Type-specific generators
    Castle generateMotteAndBailey(glm::vec2 position, const Config& config, uint64_t seed);
    Castle generateRectangularKeep(glm::vec2 position, const Config& config, uint64_t seed);
    Castle generateCoastalCastle(glm::vec2 position, const Config& config, uint64_t seed);

    // Terrain modification for castle
    std::vector<TerrainModification> generateTerrainMods(const Castle& castle);
};
```

#### 6b.5.2 Castle Interior Buildings

```cpp
// Buildings typically found within a castle bailey
enum class CastleBuildingType {
    GreatHall,          // Main hall for feasting/courts
    SolarBlock,         // Lord's private chambers
    Chapel,             // Castle chapel
    Kitchen,            // Separate building (fire risk)
    Stables,            // Horse stabling
    Barracks,           // Garrison accommodation
    Armoury,            // Weapon storage
    Granary,            // Food storage
    Well,               // Water supply (critical)
    Smithy,             // Blacksmith
    Bakehouse,          // Bread oven
    Brewhouse,          // Beer brewing
    Prison              // Dungeon/cells
};

struct CastleBuildingSpec {
    CastleBuildingType type;
    bool required;              // Must be present
    float minArea;
    float maxArea;
    bool mustBeAgainstWall;     // Built against curtain wall
    std::vector<CastleBuildingType> nearTo;  // Preferred adjacencies
};
```

### 6b.6 Harbor Defenses

#### 6b.6.1 Port Fortifications

```cpp
struct HarborDefense {
    // Harbor entrance control
    struct ChainBoom {
        glm::vec2 tower1Position;   // Chain anchor point 1
        glm::vec2 tower2Position;   // Chain anchor point 2
        float chainLength;
        bool hasWindlass;           // For raising/lowering
    };
    std::optional<ChainBoom> chainBoom;

    // Defensive towers
    std::vector<Tower> harborTowers;

    // Quay walls (defensible)
    struct DefensibleQuay {
        std::vector<glm::vec2> path;
        float wallHeight;
        bool hasBattlements;
        std::vector<float> arrowLoopPositions;
    };
    std::vector<DefensibleQuay> quayWalls;

    // Coastal watchtower
    std::optional<Tower> watchtower;

    // Beacon for warning signals
    struct Beacon {
        glm::vec2 position;
        float height;
        bool hasShelter;            // Weather protection for fire
    };
    std::optional<Beacon> beacon;
};

class HarborDefenseGenerator {
public:
    HarborDefense generate(
        const SettlementDefinition& settlement,
        const std::vector<glm::vec2>& harborPath,
        const TerrainAnalysis& terrain,
        uint64_t seed
    );

private:
    // Find optimal chain boom positions
    ChainBoom placeChainBoom(
        const std::vector<glm::vec2>& harborPath,
        float harborWidth
    );

    // Place watchtower on high ground
    Tower placeWatchtower(
        const SettlementDefinition& settlement,
        const TerrainAnalysis& terrain
    );
};
```

### 6b.7 Ditches and Moats

#### 6b.7.1 Defensive Earthworks

```cpp
struct DefensiveDitch {
    std::vector<glm::vec2> path;    // Centerline
    float width = 8.0f;
    float depth = 4.0f;

    enum class Type {
        Dry,                // Empty ditch
        Wet,                // Water-filled moat
        Tidal               // Fills at high tide (coastal)
    } type;

    enum class Profile {
        VShape,             // Steep sides meeting at bottom
        FlatBottom,         // Flat bottom, steep sides
        Stepped             // With berm partway down
    } profile;

    // Associated bank (often on inside)
    bool hasBank = true;
    float bankHeight = 2.0f;
};

class DitchGenerator {
public:
    DefensiveDitch generate(
        const std::vector<glm::vec2>& wallPath,
        float offsetDistance,
        const TerrainAnalysis& terrain,
        uint64_t seed
    );

    // Generate terrain modification
    TerrainModification generateTerrainCut(const DefensiveDitch& ditch);

    // Generate water surface mesh (for wet moats)
    Mesh generateWaterSurface(const DefensiveDitch& ditch);
};
```

### 6b.8 Integration with Settlement Layout

#### 6b.8.1 Wall-Aware Layout

```cpp
class DefensiveLayoutIntegration {
public:
    // Modify settlement layout to respect walls
    void integrateWalls(
        SettlementLayout& layout,
        const WallSystem& walls
    );

    // Ensure streets align with gates
    void alignStreetsToGates(
        StreetNetwork& streets,
        const std::vector<TownGate>& gates
    );

    // Place buildings against walls where appropriate
    void placeWallBuildings(
        std::vector<BuildingLot>& lots,
        const WallSystem& walls
    );

    // Reserve space for wall walk access stairs
    void reserveWallAccessPoints(
        SettlementLayout& layout,
        const WallSystem& walls
    );
};
```

### 6b.9 Deliverables - Phase 4b

- [ ] TownWallGenerator with terrain adaptation
- [ ] WallMeshGenerator for wall segments
- [ ] TowerGenerator for all tower types
- [ ] GateGenerator with portcullis and drawbridge
- [ ] CastleGenerator for all castle types
- [ ] Castle interior building placement
- [ ] HarborDefenseGenerator for coastal settlements
- [ ] DitchGenerator with water support
- [ ] DefensiveLayoutIntegration
- [ ] Defensive structure blockout meshes

**Testing**:
- Generate walled town with multiple gates
- Generate motte-and-bailey castle
- Generate coastal castle with harbor defenses
- Verify walls follow terrain correctly
- Verify streets align with gates
- Walk through gate passages and tower interiors

---

## 6c. Phase 4c: Port & Maritime Infrastructure

Ports are critical for south coast settlements. The Cinque Ports (Hastings, Romney, Hythe, Dover, Sandwich, plus Rye and Winchelsea) were major defensive and commercial centers. This phase covers all maritime infrastructure beyond the defensive harbor elements in Phase 4b.

### 6c.1 Port Town Classification

```cpp
enum class PortType : uint8_t {
    FishingHarbor,      // Small, local fishing
    CoastalPort,        // Regional trade, fishing
    MajorPort,          // Continental trade, defense
    CinquePort          // Special status, naval obligations
};

enum class HarborType : uint8_t {
    NaturalBay,         // Sheltered natural inlet
    RiverMouth,         // At river/sea junction
    Constructed,        // Man-made harbor with moles
    Tidal               // Exposed at low tide
};

struct PortSettings {
    PortType type;
    HarborType harborType;

    // Scale parameters
    uint32_t quayLength = 100;          // Total quay length in meters
    uint32_t numBerths = 4;             // Ship berth capacity
    float harborDepth = 3.0f;           // Water depth at high tide
    bool hasTidalBasin = false;         // Enclosed basin for tidal range

    // Features based on port type
    bool hasCustomsHouse = false;       // For trade ports
    bool hasShipyard = false;           // Ship repair/building
    bool hasRopewalk = false;           // Rope manufacture
    bool hasSaltWorks = false;          // Salt production
    bool hasWarehouse = true;           // Storage
    bool hasFishMarket = true;          // Fish sales
};
```

### 6c.2 Harbor Layout Generation

```cpp
struct HarborLayout {
    // Water area
    std::vector<glm::vec2> harborPerimeter;  // Water edge
    std::vector<glm::vec2> channelPath;      // Entrance channel
    float channelWidth = 15.0f;

    // Quay infrastructure
    struct Quay {
        std::vector<glm::vec2> edge;         // Quay face (water side)
        float height = 2.0f;                 // Height above low water
        bool hasSteps = true;                // Access steps to water
        bool hasBollards = true;             // Mooring points
        bool hasCrane = false;               // Cargo crane
    };
    std::vector<Quay> quays;

    // Jetties and piers
    struct Jetty {
        glm::vec2 basePosition;
        float length;
        float width;
        bool isTimber = true;               // vs stone
        int numBerths;
    };
    std::vector<Jetty> jetties;

    // Slipways for boat hauling
    struct Slipway {
        glm::vec2 position;
        float angle;                        // Perpendicular to shore
        float width = 4.0f;
        float gradient = 0.1f;              // 1:10 slope typical
    };
    std::vector<Slipway> slipways;

    // Beach areas (for smaller boats)
    std::vector<glm::vec2> beachZone;       // Boat landing area
};

class HarborLayoutGenerator {
public:
    HarborLayout generate(
        const SettlementLayout& settlement,
        const CoastlineData& coastline,
        const PortSettings& settings,
        uint32_t seed
    );

private:
    // Find suitable harbor location
    glm::vec2 findHarborSite(
        const CoastlineData& coastline,
        HarborType type
    );

    // Generate quay alignment
    std::vector<glm::vec2> generateQuayLine(
        const CoastlineData& coastline,
        float desiredLength
    );

    // Place jetties for additional berths
    std::vector<Jetty> placeJetties(
        const std::vector<Quay>& quays,
        int additionalBerths
    );

    // Connect harbor to street network
    void connectToStreets(
        HarborLayout& harbor,
        SettlementLayout& settlement
    );
};
```

### 6c.3 Maritime Buildings

```cpp
// Maritime-specific building types
enum class MaritimeBuildingType : uint8_t {
    // Waterfront buildings
    NetLoft,            // Fish net storage/repair (upper floor)
    BoatShed,           // Small boat storage
    Warehouse,          // Goods storage
    FishMarket,         // Open-sided fish sales
    CustomsHouse,       // Port authority (major ports)

    // Industrial
    Shipyard,           // Ship building/repair
    Ropewalk,           // Rope manufacture (long, narrow)
    SailLoft,           // Sail making/repair
    Cooperage,          // Barrel making
    SaltWorks,          // Salt production
    Smokehouse,         // Fish smoking

    // Service
    Tavern,             // Sailor accommodation
    Chandlery,          // Ship supplies
    PilotHouse,         // Harbor pilot station
    BeaconTower,        // Navigation aid

    // Processing
    FishCuring,         // Salting/drying fish
    OilPress            // Fish oil production
};

struct MaritimeBuildingConfig {
    MaritimeBuildingType type;

    // Size ranges by type
    static constexpr float getMinWidth(MaritimeBuildingType t);
    static constexpr float getMaxWidth(MaritimeBuildingType t);
    static constexpr float getMinLength(MaritimeBuildingType t);
    static constexpr float getMaxLength(MaritimeBuildingType t);

    // Special requirements
    bool requiresWaterfrontage = false;
    bool requiresSlipway = false;
    float minDistanceFromWater = 0.0f;
    float maxDistanceFromWater = 50.0f;
};

// Building dimensions by type (meters)
/*
    NetLoft:        5-8m × 8-12m, 2 stories
    BoatShed:       6-10m × 10-20m, 1 story (tall)
    Warehouse:      10-15m × 20-40m, 2-3 stories
    FishMarket:     8-12m × 15-25m, 1 story (open sides)
    CustomsHouse:   12-18m × 15-20m, 2 stories (imposing)

    Shipyard:       15-30m × 40-80m (includes slip)
    Ropewalk:       3-5m × 200-400m (extremely long!)
    SailLoft:       10-15m × 20-30m, upper floor
    Cooperage:      8-12m × 12-18m
    SaltWorks:      varies (pans + buildings)
    Smokehouse:     4-6m × 6-10m (tall chimney)
*/

class MaritimeBuildingGenerator {
public:
    struct MaritimeBuilding {
        MaritimeBuildingType type;
        BuildingFootprint footprint;
        std::vector<BuildingComponent> components;
        bool hasUpperFloor = false;
        bool hasLoftDoor = false;        // Cargo hoist door
        bool hasWideEntrance = false;    // Cart/boat access
    };

    MaritimeBuilding generate(
        const MaritimeBuildingConfig& config,
        const LotData& lot,
        uint32_t seed
    );

private:
    // Net loft with characteristic upper-floor access
    MaritimeBuilding generateNetLoft(const LotData& lot, uint32_t seed);

    // Open-sided fish market
    MaritimeBuilding generateFishMarket(const LotData& lot, uint32_t seed);

    // Long, narrow ropewalk
    MaritimeBuilding generateRopewalk(const LotData& lot, uint32_t seed);

    // Shipyard with slipway
    MaritimeBuilding generateShipyard(
        const LotData& lot,
        const Slipway& slipway,
        uint32_t seed
    );
};
```

### 6c.4 Waterfront Street Layout

```cpp
struct WaterfrontLayout {
    // Primary waterfront road (parallel to quay)
    std::vector<glm::vec2> quaysideStreet;
    float quaysideWidth = 6.0f;     // Wider for cargo handling

    // Access roads perpendicular to waterfront
    struct AccessLane {
        std::vector<glm::vec2> path;
        float width;
        bool forCarts = true;        // Wide enough for carts
    };
    std::vector<AccessLane> accessLanes;

    // Working areas
    struct WorkingQuay {
        std::vector<glm::vec2> boundary;
        bool hasCrane = false;
        bool hasWeighStation = false;
        std::vector<glm::vec2> barrelStorage;
    };
    std::vector<WorkingQuay> workingAreas;

    // Lot subdivision along waterfront
    std::vector<LotData> waterfrontLots;
};

class WaterfrontLayoutGenerator {
public:
    WaterfrontLayout generate(
        const HarborLayout& harbor,
        const SettlementLayout& settlement,
        const PortSettings& settings,
        uint32_t seed
    );

private:
    // Subdivide waterfront into lots
    std::vector<LotData> subdivideWaterfront(
        const std::vector<glm::vec2>& quayLine,
        const PortSettings& settings
    );

    // Assign building types to lots
    void assignBuildingTypes(
        std::vector<LotData>& lots,
        const PortSettings& settings
    );
};
```

### 6c.5 Cinque Ports Special Features

The Cinque Ports had special privileges and obligations:

```cpp
struct CinquePortFeatures {
    // Naval obligations
    bool hasShipService = true;      // Obligated to provide ships
    int shipQuota = 21;              // Ships owed to Crown (varied by port)

    // Special buildings
    bool hasCourt = true;            // Court of Shepway (confederacy court)
    bool hasGuildHall = true;        // Portsmen guild
    bool hasBaronHall = true;        // Baron of Cinque Ports residence

    // Defensive priority
    bool hasEnhancedDefenses = true;
    bool hasBeaconChain = true;      // Warning beacon network

    // Trade privileges
    bool hasFreePassage = true;      // Toll-free trade
    bool hasYarmouthRights = true;   // Herring fair privileges
};

// The seven Cinque Ports (5 original + 2 "ancient towns")
enum class CinquePortName : uint8_t {
    // Head Ports
    Hastings,
    Romney,
    Hythe,
    Dover,
    Sandwich,
    // Ancient Towns (added later)
    Rye,
    Winchelsea
};

// Each port had "limbs" (subsidiary ports)
// e.g., Hastings had Seaford, Pevensey, etc.
```

### 6c.6 Fishing Village Layout

Smaller fishing settlements have distinct patterns:

```cpp
struct FishingVillageLayout {
    // Beach-oriented (no formal harbor)
    std::vector<glm::vec2> beachLine;

    // Boat storage above high water
    struct BoatPark {
        glm::vec2 position;
        float area;
        int boatCapacity;
    };
    std::vector<BoatPark> boatParks;

    // Net drying areas
    std::vector<glm::vec2> netDryingPoles;

    // Simple buildings
    struct FishermanCottage {
        LotData lot;
        bool hasAttachedStore = false;   // Net/gear storage
    };
    std::vector<FishermanCottage> cottages;

    // Communal features
    glm::vec2 fishLandingPoint;
    glm::vec2 netMendingArea;
    bool hasSimpleQuay = false;          // Basic stone quay
    bool hasChapel = true;               // Often dedicated to maritime saints
};

class FishingVillageGenerator {
public:
    FishingVillageLayout generate(
        const CoastlineData& coastline,
        const BiomeZone surroundingBiome,
        uint32_t populationTarget,
        uint32_t seed
    );
};
```

### 6c.7 Tidal Considerations

South coast ports deal with significant tidal ranges:

```cpp
struct TidalData {
    float highWaterMark = 2.0f;      // Height above datum
    float lowWaterMark = -2.0f;      // Often exposes mudflats
    float tidalRange = 4.0f;         // Typical spring tide
    float meanSeaLevel = 0.0f;

    // Mud/sand exposure at low tide
    bool exposesFlats = true;
    float flatExtent = 50.0f;        // How far flats extend
};

struct TidalInfrastructure {
    // Tidal basin (enclosed area)
    struct TidalBasin {
        std::vector<glm::vec2> boundary;
        glm::vec2 gatePosition;
        float gateWidth = 8.0f;
        float sillHeight = -1.0f;    // Minimum water retained
    };
    std::optional<TidalBasin> basin;

    // Causeways for low-tide access
    struct Causeway {
        std::vector<glm::vec2> path;
        float width = 3.0f;
        float height = 1.0f;         // Above mean low water
    };
    std::vector<Causeway> causeways;

    // Hard standings (reinforced beach areas)
    std::vector<glm::vec2> hardStandings;
};
```

### 6c.8 Salt Production

Salt was valuable and produced at coastal sites:

```cpp
struct SaltWorks {
    enum class Type {
        SolarPans,      // Evaporation pans (warmer climates)
        BoilingHouse    // Heated evaporation (more common in England)
    };

    Type type = Type::BoilingHouse;

    struct EvaporationPan {
        glm::vec2 position;
        float size;                 // 2-5m square
    };
    std::vector<EvaporationPan> pans;

    struct BoilingHouse {
        BuildingFootprint building;
        int numFurnaces;
        float leadPanSize;          // Large lead evaporation pans
    };
    std::optional<BoilingHouse> boilingHouse;

    // Brine intake
    glm::vec2 brineSource;         // Seawater intake point
    std::vector<glm::vec2> brineChannel;

    // Storage
    BuildingFootprint saltStore;   // Dry storage for finished salt
};
```

### 6c.9 Maritime Props

```cpp
enum class MaritimePropType : uint8_t {
    // Mooring
    Bollard,            // Stone or timber mooring post
    RingBolt,           // Iron ring in quay

    // Cargo handling
    Crane,              // Wooden cargo crane
    Winch,              // Rope winch
    CartRamp,           // Ramped access to quay

    // Fishing
    NetPole,            // For drying nets
    NetRack,            // Horizontal net drying
    FishBasket,         // Wicker fish baskets
    LobsterPot,         // Lobster/crab pots

    // Boats
    RowingBoat,         // Small boat on beach
    FishingBoat,        // Larger fishing vessel
    Anchor,             // Beached anchor
    Oars,               // Stacked oars

    // Navigation
    Beacon,             // Warning beacon
    DayMark,            // Navigation marker

    // Barrels/storage
    FishBarrel,         // Herring barrels
    SaltSack,           // Salt bags
    CoiledRope,         // Rope coils

    // Miscellaneous
    Capstan,            // For hauling boats
    SeaWall,            // Stone sea defense
    Groyne              // Timber beach groyne
};

class MaritimePropPlacer {
public:
    void placeQuayProps(
        const Quay& quay,
        std::vector<PropInstance>& outProps
    );

    void placeBeachProps(
        const std::vector<glm::vec2>& beachZone,
        std::vector<PropInstance>& outProps
    );

    void placeFishingProps(
        const FishingVillageLayout& village,
        std::vector<PropInstance>& outProps
    );
};
```

### 6c.10 Integration with Settlement System

```cpp
class PortIntegration {
public:
    // Modify settlement layout for port
    void adaptSettlementForPort(
        SettlementLayout& settlement,
        const PortSettings& portSettings,
        const CoastlineData& coastline
    );

    // Generate complete port infrastructure
    struct PortComplex {
        HarborLayout harbor;
        WaterfrontLayout waterfront;
        std::vector<MaritimeBuilding> buildings;
        std::optional<TidalInfrastructure> tidalFeatures;
        std::optional<SaltWorks> saltWorks;
        std::vector<PropInstance> props;
    };

    PortComplex generatePort(
        const Settlement& settlement,
        const CoastlineData& coastline,
        const PortSettings& settings,
        uint32_t seed
    );

private:
    // Detect if settlement should be a port
    bool shouldBePort(
        const Settlement& settlement,
        const CoastlineData& coastline
    );

    // Determine port type based on settlement
    PortSettings determinePortSettings(
        const Settlement& settlement,
        SettlementType type
    );
};
```

### 6c.11 Deliverables - Phase 4c

- [ ] PortType classification and settings
- [ ] HarborLayoutGenerator for quays, jetties, slipways
- [ ] WaterfrontLayoutGenerator with lot subdivision
- [ ] MaritimeBuildingGenerator for all maritime building types
- [ ] FishingVillageGenerator for small coastal settlements
- [ ] TidalInfrastructure generation
- [ ] SaltWorks generation
- [ ] MaritimePropPlacer for harbor/beach props
- [ ] PortIntegration with main settlement system
- [ ] Mesh generation for quays, jetties, harbor walls

**Testing**:
- Generate fishing village with beach landing
- Generate coastal port with quays and warehouses
- Generate Cinque Port-style major harbor with shipyard
- Verify waterfront lots align to quay
- Verify tidal basin gates operate conceptually
- Walk along quayside and access jetties
- Verify maritime buildings have correct proportions

---
