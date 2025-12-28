# Procedural Cities - Phase 5: Details & Props

[← Back to Index](README.md)

## 7. Phase 5: Props & Detail Population

### 7.1 Vegetation Integration

#### 7.1.1 Settlement Vegetation

```cpp
class SettlementVegetation {
public:
    struct Config {
        float gardenTreeDensity = 0.02f;
        float orchardTreeSpacing = 8.0f;
        float hedgeHeight = 1.5f;
    };

    struct TreePlacement {
        glm::vec2 position;
        std::string treeType;       // "apple", "pear", "oak", etc.
        float scale;
    };

    // Generate garden trees
    std::vector<TreePlacement> placeGardenTrees(
        const std::vector<BuildingLot>& lots,
        const Config& config
    );

    // Generate orchard if settlement has agricultural zone
    std::vector<TreePlacement> generateOrchard(
        const Polygon& area,
        const Config& config
    );

    // Generate hedge boundaries
    std::vector<std::vector<glm::vec2>> generateHedges(
        const std::vector<BuildingLot>& lots
    );
};
```

#### 7.1.2 Tree Exclusion Zones

```cpp
class VegetationExclusion {
public:
    // Generate mask for TreeSystem to exclude trees
    void generateExclusionMask(
        const SettlementDefinition& settlement,
        const std::string& outputPath,
        uint32_t resolution = 512
    );
};
```

### 7.2 Ground Cover

#### 7.2.1 Ground Detail Decals

```cpp
class GroundDetailPlacer {
public:
    enum class DetailType {
        Mud_Puddle,
        Hay_Scatter,
        Fallen_Leaves,
        Manure,
        Dirt_Patch,
        Grass_Tuft,
        Moss_Patch
    };

    struct Placement {
        glm::vec2 position;
        float rotation;
        float scale;
        DetailType type;
    };

    std::vector<Placement> place(
        const SettlementDefinition& settlement,
        const std::vector<BuildingLot>& lots,
        const StreetNetwork& streets
    );
};
```

### 7.3 Props and Clutter

#### 7.3.1 Yard Props

```cpp
class YardPropPlacer {
public:
    enum class PropType {
        // Agricultural
        Hay_Bale,
        Cart,
        Barrel,
        Crate,
        Sack,
        Pitchfork,
        Scythe,

        // Domestic
        Bucket,
        Wash_Tub,
        Clothesline,
        Firewood_Stack,

        // Commercial
        Market_Stall,
        Cask,
        Anvil,
        Bellows,

        // Maritime
        Net_Rack,
        Lobster_Pot,
        Boat,
        Oar_Rack
    };

    struct Placement {
        glm::vec3 position;
        float rotation;
        PropType type;
        float scale = 1.0f;
    };

    std::vector<Placement> place(
        const BuildingLot& lot,
        const std::string& buildingType
    );
};
```

#### 7.3.2 Prop Models

Store props as GLB files with multiple LOD levels:

```
assets/props/
├── agricultural/
│   ├── hay_bale.glb
│   ├── cart.glb
│   └── barrel.glb
├── domestic/
│   ├── bucket.glb
│   └── firewood_stack.glb
├── commercial/
│   └── market_stall.glb
└── maritime/
    ├── net_rack.glb
    └── lobster_pot.glb
```

### 7.4 Ambient Life (Optional/Future)

```cpp
// For future implementation
struct AmbientCreature {
    enum class Type {
        Chicken,
        Goose,
        Pig,
        Sheep,
        Dog,
        Cat
    };

    glm::vec3 position;
    Type type;
    uint32_t wanderAreaId;      // Constrained to yard/pen
};
```

### 7.5 Deliverables - Phase 5

- [ ] SettlementVegetation integration with TreeSystem
- [ ] VegetationExclusion mask generation
- [ ] GroundDetailPlacer
- [ ] YardPropPlacer with zone-aware placement
- [ ] Prop model library (GLB)
- [ ] Prop LOD system integration

**Testing**: Full settlement with:
- Garden trees and orchards visible
- Ground clutter distributed appropriately
- Props matching building types (farm props near barns, maritime props near quays)

---

## 7b. Phase 5b: Terrain Integration & Subterranean

### 7b.1 Terrain Modification System

Integration with the existing terrain cut system for building foundations and subterranean spaces.

#### 7b.1.1 Foundation Types

```cpp
enum class FoundationType {
    Surface,            // Building sits directly on terrain
    Leveled,            // Terrain flattened under footprint
    Cut,                // Cut into hillside
    CutAndFill,         // Combination for sloped sites
    Terraced,           // Multiple levels on steep slopes
    Raised,             // Platform above terrain (waterlogged areas)
    Piled               // Posts driven into soft ground
};

struct BuildingFoundation {
    FoundationType type;
    Polygon footprint;
    float targetElevation;

    // Cut parameters
    float cutDepth;
    float cutSlope;             // Angle of cut walls

    // Fill parameters
    float fillHeight;
    bool retainingWall;
    std::string wallMaterial;   // "flint", "chalk", "timber"

    // Blending
    float blendRadius;          // Distance to blend to natural terrain
};
```

#### 7b.1.2 Terrain Cut Integration

```cpp
class SettlementTerrainModifier {
public:
    // Generate all terrain modifications for a settlement
    std::vector<TerrainModification> generate(
        const SettlementLayout& layout,
        const TerrainAnalysis& terrain
    );

    // Export as heightmap modification layer
    void exportHeightModification(
        const std::vector<TerrainModification>& mods,
        const std::string& outputPath,
        uint32_t resolution
    );

    // Export as terrain material mask (where to blend settlement ground)
    void exportMaterialMask(
        const std::vector<TerrainModification>& mods,
        const std::string& outputPath,
        uint32_t resolution
    );

private:
    // Calculate optimal foundation for building on slope
    FoundationType selectFoundationType(
        const BuildingLot& lot,
        float maxSlope,
        float averageSlope
    );

    // Generate cut geometry
    Mesh generateCutWalls(const TerrainModification& mod);
};
```

### 7b.2 Cellar Generation

#### 7b.2.1 Cellar Types

| Building Type | Cellar Probability | Cellar Type |
|--------------|-------------------|-------------|
| Peasant Cottage | 0.1 | Storage pit |
| Stone House | 0.7 | Full cellar |
| Inn | 0.9 | Barrel cellar |
| Church | 0.4 | Crypt |
| Manor House | 0.8 | Wine cellar |
| Warehouse | 0.6 | Storage cellar |

```cpp
struct Cellar {
    Polygon footprint;          // May be smaller than building
    float depth;                // Below ground level
    float ceilingHeight;

    enum class Type {
        StoragePit,     // Simple pit with ladder
        VaultedCellar,  // Stone vaulted ceiling
        TimberCellar,   // Timber-lined
        Crypt,          // Church undercroft
        Undercroft      // Ground floor below hall
    } type;

    // Access
    enum class Access {
        InternalStair,      // Stair inside building
        ExternalStair,      // Steps from outside
        Trapdoor,           // Hatch in floor
        Ramp                // Cart access (warehouses)
    } access;
    glm::vec2 accessPosition;

    // Contents
    std::vector<std::string> propTypes;  // Barrels, crates, etc.
};
```

#### 7b.2.2 Cellar Generator

```cpp
class CellarGenerator {
public:
    Cellar generate(
        const Building& building,
        const Polygon& footprint,
        float groundLevel,
        uint64_t seed
    );

    // Generate cellar geometry
    Mesh generateMesh(const Cellar& cellar);

    // Generate terrain cut for cellar
    TerrainModification generateTerrainCut(
        const Cellar& cellar,
        glm::vec2 buildingPosition
    );

private:
    // Generate vaulted ceiling
    Mesh generateVaultedCeiling(const Polygon& footprint, float height);

    // Generate access stairs
    Mesh generateStairs(
        const Cellar& cellar,
        Cellar::Access accessType
    );
};
```

### 7b.3 Church Crypts and Undercrofts

#### 7b.3.1 Crypt Structure

```cpp
struct ChurchCrypt {
    // Location
    enum class Position {
        UnderChancel,       // Most common
        UnderNave,          // Larger churches
        UnderTower          // Tower base crypt
    } position;

    Polygon footprint;
    float depth;

    // Architecture
    bool hasAisles;
    int numBays;
    float vaultHeight;

    // Features
    bool hasShrineNiche;        // For relics
    bool hasAltarPlatform;
    std::vector<glm::vec2> pillarPositions;

    // Access
    glm::vec2 stairPosition;
    bool externalAccess;        // Can be entered from outside
};

class CryptGenerator {
public:
    ChurchCrypt generate(
        const Building& church,
        uint64_t seed
    );

    Mesh generateMesh(const ChurchCrypt& crypt);
    TerrainModification generateTerrainCut(const ChurchCrypt& crypt);
};
```

### 7b.4 Wells and Water Access

#### 7b.4.1 Well Types

```cpp
struct Well {
    glm::vec2 position;

    enum class Type {
        OpenWell,           // Simple stone-lined shaft
        CoveredWell,        // With roof structure
        DrawWell,           // With winding gear
        Pump,               // Hand pump (later period)
        Spring,             // Natural spring with stone surround
    } type;

    float shaftDepth;           // To water table
    float shaftDiameter;
    float waterLevel;           // Current water depth

    // Structure above ground
    float wallHeight;
    bool hasRoof;
    bool hasWindlass;
};

class WellGenerator {
public:
    Well generate(
        glm::vec2 position,
        float groundLevel,
        float waterTableDepth,
        uint64_t seed
    );

    // Generate visible structure
    Mesh generateAboveGround(const Well& well);

    // Generate shaft (for terrain integration)
    TerrainModification generateShaft(const Well& well);

    // Generate water surface
    Mesh generateWaterSurface(const Well& well);
};
```

### 7b.5 Tunnels and Passages

#### 7b.5.1 Underground Connections

```cpp
struct UndergroundPassage {
    glm::vec2 startPosition;
    glm::vec2 endPosition;

    std::vector<glm::vec2> waypoints;   // Path through terrain

    float width;
    float height;
    float depth;                        // Below ground

    enum class Type {
        ServiceTunnel,      // Connecting buildings
        Escape,             // Secret escape route
        Drainage,           // Water management
        Crypt_Connection    // Between church areas
    } type;

    // Construction
    bool isLined;           // Stone/timber lined
    std::string liningMaterial;
};

class TunnelGenerator {
public:
    UndergroundPassage generate(
        glm::vec2 start,
        glm::vec2 end,
        const TerrainAnalysis& terrain,
        uint64_t seed
    );

    Mesh generateMesh(const UndergroundPassage& passage);
    std::vector<TerrainModification> generateTerrainCuts(
        const UndergroundPassage& passage
    );
};
```

### 7b.6 Terrain Physics Integration

*Note: Physics for terrain cuts is pending implementation*

```cpp
// Future integration point
class TerrainPhysicsModifier {
public:
    // Update physics heightfield for terrain cuts
    void applyModifications(
        PhysicsHeightfield& heightfield,
        const std::vector<TerrainModification>& mods
    );

    // Generate collision geometry for cellar walls
    void generateCellarCollision(
        PhysicsSystem& physics,
        const Cellar& cellar
    );

    // Generate collision for tunnel
    void generateTunnelCollision(
        PhysicsSystem& physics,
        const UndergroundPassage& passage
    );
};
```

### 7b.7 Deliverables - Phase 5b

- [ ] SettlementTerrainModifier integration with existing terrain cut
- [ ] Foundation type selection algorithm
- [ ] CellarGenerator for all building types
- [ ] CryptGenerator for churches
- [ ] WellGenerator with shaft terrain integration
- [ ] TunnelGenerator for underground passages
- [ ] Terrain modification export (heightmap layer)
- [ ] Material mask export for settlement ground

**Testing**:
- Buildings on slopes have appropriate foundations
- Cellars are accessible and don't clip terrain
- Wells connect to water table correctly
- Church crypts render correctly below ground
- Terrain cuts blend smoothly to natural terrain

*Physics TODO*: Integrate terrain cuts with Jolt Physics heightfield

---
