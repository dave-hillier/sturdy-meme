# Procedural Cities - Implementation Systems

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

## 1. Foundation Layer

These systems have no dependencies on other settlement systems and must be built first.

### 1.1 Spatial Utilities

Core 2D geometry operations used throughout the codebase.

**Used by**: Layout System, Path Network, Placement System, Mesh Generation

```cpp
// tools/common/SpatialUtils.h

namespace spatial {

// ─────────────────────────────────────────────────────────────────────
// POLYGON OPERATIONS
// ─────────────────────────────────────────────────────────────────────

struct Polygon {
    std::vector<glm::vec2> vertices;  // CCW winding

    float area() const;
    glm::vec2 centroid() const;
    bool contains(glm::vec2 point) const;
    AABB2D bounds() const;
};

// Polygon boolean operations
Polygon polygonUnion(const Polygon& a, const Polygon& b);
Polygon polygonIntersection(const Polygon& a, const Polygon& b);
Polygon polygonDifference(const Polygon& a, const Polygon& b);
std::vector<Polygon> polygonOffset(const Polygon& p, float distance);  // Inset/outset

// ─────────────────────────────────────────────────────────────────────
// SUBDIVISION
// ─────────────────────────────────────────────────────────────────────

// Split polygon into sub-regions
struct SubdivisionResult {
    std::vector<Polygon> cells;
    std::vector<uint32_t> parentEdges;  // Which original edge each cell touches
};

// Binary space partition
SubdivisionResult bspSubdivide(
    const Polygon& region,
    float minCellArea,
    float minCellWidth,
    uint64_t seed
);

// Grid subdivision (respects polygon boundary)
SubdivisionResult gridSubdivide(
    const Polygon& region,
    float cellWidth,
    float cellDepth,
    glm::vec2 alignment  // Grid origin/direction
);

// Lot subdivision along an edge (for street frontage)
SubdivisionResult frontageSubdivide(
    const Polygon& block,
    const std::vector<glm::vec2>& frontageEdge,
    float minLotWidth,
    float maxLotWidth,
    float targetDepth
);

// ─────────────────────────────────────────────────────────────────────
// LINE/PATH OPERATIONS
// ─────────────────────────────────────────────────────────────────────

struct Polyline {
    std::vector<glm::vec2> points;
    bool closed = false;

    float length() const;
    glm::vec2 pointAt(float t) const;      // t in [0, length]
    glm::vec2 tangentAt(float t) const;
    glm::vec2 normalAt(float t) const;     // Perpendicular (left side)
};

// Polyline to polygon (offset both sides)
Polygon polylineToPolygon(const Polyline& line, float width);

// Smooth polyline with Catmull-Rom or similar
Polyline smoothPolyline(const Polyline& line, int subdivisions);

// ─────────────────────────────────────────────────────────────────────
// STRAIGHT SKELETON
// ─────────────────────────────────────────────────────────────────────

struct SkeletonResult {
    std::vector<glm::vec2> skeletonVertices;
    std::vector<std::pair<uint32_t, uint32_t>> skeletonEdges;
    std::vector<Polygon> faces;  // One per original edge
};

SkeletonResult computeStraightSkeleton(const Polygon& footprint);

// ─────────────────────────────────────────────────────────────────────
// SPATIAL QUERIES
// ─────────────────────────────────────────────────────────────────────

class SpatialIndex2D {
public:
    void insert(uint32_t id, const AABB2D& bounds);
    void remove(uint32_t id);

    std::vector<uint32_t> queryBox(const AABB2D& bounds) const;
    std::vector<uint32_t> queryRadius(glm::vec2 center, float radius) const;
    uint32_t queryNearest(glm::vec2 point) const;
};

} // namespace spatial
```

**Implementation notes**:
- Use a robust polygon library (e.g., Clipper2) for boolean operations
- Straight skeleton is complex; consider CGAL or a simplified version for common cases
- SpatialIndex2D can use R-tree or grid-based acceleration

**Deliverables**:
- [ ] Polygon struct with area, centroid, contains, bounds
- [ ] BSP subdivision
- [ ] Grid subdivision
- [ ] Frontage subdivision (critical for lot generation)
- [ ] Polyline operations
- [ ] Straight skeleton (can be simplified initially)
- [ ] SpatialIndex2D

---

### 1.2 Config System

Data-driven configuration for all procedural content.

**Used by**: All generation systems

```cpp
// tools/common/ConfigSystem.h

class ConfigRegistry {
public:
    // Load all configs from directory
    void loadFromDirectory(const std::filesystem::path& dir);

    // Type-safe config access
    template<typename T>
    const T& get(const std::string& id) const;

    // Query configs by attribute
    std::vector<std::string> findByTag(const std::string& tag) const;
    std::vector<std::string> findByType(const std::string& type) const;

private:
    std::unordered_map<std::string, nlohmann::json> configs_;
};

// ─────────────────────────────────────────────────────────────────────
// CONFIG TYPES
// ─────────────────────────────────────────────────────────────────────

struct BuildingTypeConfig {
    std::string id;
    std::string category;                    // "residential", "commercial", etc.
    std::vector<std::string> applicableZones;

    // Footprint constraints
    struct {
        float minWidth, maxWidth;
        float minDepth, maxDepth;
        std::vector<std::string> allowedShapes;
    } footprint;

    // Floor configuration
    struct {
        int minFloors, maxFloors;
        float groundFloorHeight;
        float upperFloorHeight;
        bool hasLoft;
    } floors;

    // Roof configuration
    struct {
        std::vector<std::string> allowedTypes;
        float minPitch, maxPitch;
        std::string defaultMaterial;
    } roof;

    // Material selection by biome
    std::unordered_map<std::string, std::vector<std::string>> wallMaterialsByBiome;

    // Prop placement rules
    std::vector<PropRule> props;
};

struct SettlementTemplateConfig {
    std::string id;
    std::vector<std::string> applicableTypes;  // Hamlet, Village, Town

    // Layout parameters
    struct {
        std::string pattern;          // "linear", "nucleated", "grid"
        float mainStreetWidth;
        float secondaryStreetWidth;
        glm::vec2 minLotSize, maxLotSize;
    } layout;

    // Zone requirements
    std::vector<ZoneRequirement> zones;

    // Feature probabilities
    std::unordered_map<std::string, float> featureProbabilities;
};

struct MaterialConfig {
    std::string id;
    std::string textureAlbedo;
    std::string textureNormal;
    std::string texturePBR;

    // Physical properties
    float uvScale;
    bool hasVariants;
    int variantCount;
};
```

**Directory structure**:
```
assets/
├── settlements/
│   └── templates/
│       ├── english_village.json
│       ├── fishing_village.json
│       └── walled_town.json
├── buildings/
│   └── types/
│       ├── peasant_cottage.json
│       ├── longhouse.json
│       ├── church.json
│       └── ...
├── materials/
│   ├── flint_rubble.json
│   ├── wattle_daub.json
│   └── thatch.json
└── props/
    ├── furniture/
    ├── exterior/
    └── maritime/
```

**Deliverables**:
- [ ] ConfigRegistry class
- [ ] BuildingTypeConfig schema and parser
- [ ] SettlementTemplateConfig schema and parser
- [ ] MaterialConfig schema and parser
- [ ] PropConfig schema and parser
- [ ] Schema validation

---

### 1.3 Terrain Integration

Interface with existing terrain system for height queries and modifications.

**Used by**: Layout System, Path Network, Building Assembler

```cpp
// tools/common/TerrainInterface.h

class TerrainInterface {
public:
    // Initialize with terrain data paths
    TerrainInterface(
        const std::filesystem::path& heightmapPath,
        const std::filesystem::path& biomemapPath
    );

    // ─────────────────────────────────────────────────────────────────
    // QUERIES (read-only)
    // ─────────────────────────────────────────────────────────────────

    float getHeight(glm::vec2 worldPos) const;
    glm::vec3 getNormal(glm::vec2 worldPos) const;
    float getSlope(glm::vec2 worldPos) const;           // Angle in degrees
    BiomeZone getBiome(glm::vec2 worldPos) const;
    bool isWater(glm::vec2 worldPos) const;
    bool isCoastline(glm::vec2 worldPos, float tolerance = 5.0f) const;

    // Batch queries for efficiency
    void getHeights(
        const std::vector<glm::vec2>& positions,
        std::vector<float>& outHeights
    ) const;

    // ─────────────────────────────────────────────────────────────────
    // ANALYSIS
    // ─────────────────────────────────────────────────────────────────

    struct AreaAnalysis {
        float minHeight, maxHeight, avgHeight;
        float avgSlope, maxSlope;
        BiomeZone dominantBiome;
        float waterFraction;
        bool hasCliff;
    };

    AreaAnalysis analyzeArea(glm::vec2 center, float radius) const;

    // Find buildable regions within area
    std::vector<Polygon> findBuildableRegions(
        glm::vec2 center,
        float radius,
        float maxSlope,
        float minArea
    ) const;

    // ─────────────────────────────────────────────────────────────────
    // MODIFICATIONS (output layers)
    // ─────────────────────────────────────────────────────────────────

    struct TerrainModification {
        Polygon area;
        float targetHeight;
        float blendRadius;
        enum { Flatten, Cut, Fill } type;
    };

    // Accumulate modifications
    void addModification(const TerrainModification& mod);

    // Export modification layer (heightmap delta)
    void exportModificationLayer(const std::filesystem::path& output) const;

    // Export material mask (for settlement ground textures)
    void exportMaterialMask(const std::filesystem::path& output) const;

private:
    // Cached heightmap for fast queries
    std::vector<float> heightmap_;
    uint32_t heightmapWidth_, heightmapHeight_;
    float worldScale_;

    // Accumulated modifications
    std::vector<TerrainModification> modifications_;
};
```

**Integration with existing systems**:
- Reads from existing `heightmap.png` generated by terrain preprocessing
- Reads biome data from `BiomeGenerator` output
- Outputs modification layers that the runtime terrain system applies

**Deliverables**:
- [ ] TerrainInterface with height/slope/biome queries
- [ ] AreaAnalysis for settlement placement validation
- [ ] findBuildableRegions for complex terrain
- [ ] Modification accumulation and export
- [ ] Integration tests with existing terrain data

---

## 2. Geometry Generation Layer

These systems produce geometry and spatial layouts. They depend on Foundation systems.

### 2.1 Path Network System

Generates all linear path-based elements: roads, streets, walls, quay edges.

**Used by**: Settlement layout, defensive walls, waterfront, field boundaries

```cpp
// tools/settlement_generator/PathNetwork.h

// ─────────────────────────────────────────────────────────────────────
// PATH TYPES (unified across all uses)
// ─────────────────────────────────────────────────────────────────────

enum class PathType : uint8_t {
    // Roads/Streets
    MainRoad,           // 8m wide, connects settlements
    Street,             // 6m wide, main settlement streets
    Lane,               // 4m wide, secondary streets
    Alley,              // 2m wide, rear access
    Footpath,           // 1.5m wide, pedestrian

    // Defensive
    WallPath,           // Town wall alignment
    DitchPath,          // Defensive ditch

    // Maritime
    QuayEdge,           // Waterfront quay line
    JettyPath,          // Pier/jetty alignment

    // Agricultural
    FieldBoundary,      // Field edge
    Hedgerow            // Hedge line
};

struct PathProperties {
    float width;
    float height;           // For walls
    bool hasSurface;        // Generate ground mesh
    bool hasEdging;         // Curbs, wall tops, etc.
    std::string material;
};

PathProperties getDefaultProperties(PathType type);

// ─────────────────────────────────────────────────────────────────────
// PATH NETWORK
// ─────────────────────────────────────────────────────────────────────

struct PathNode {
    uint32_t id;
    glm::vec2 position;
    std::vector<uint32_t> connections;

    enum class NodeType {
        Endpoint,
        Junction,
        Gate,           // Wall gate location
        Bridge,         // Water crossing
        Entry           // Settlement entry point
    } type = NodeType::Endpoint;
};

struct PathSegment {
    uint32_t id;
    uint32_t startNode, endNode;
    PathType type;
    std::vector<glm::vec2> controlPoints;  // For curved paths

    // Derived data (computed after network is built)
    float length;
    Polyline geometry;
};

class PathNetwork {
public:
    // Build network
    uint32_t addNode(glm::vec2 position, PathNode::NodeType type = PathNode::NodeType::Endpoint);
    uint32_t addSegment(uint32_t startNode, uint32_t endNode, PathType type);
    void setSegmentCurve(uint32_t segmentId, const std::vector<glm::vec2>& controlPoints);

    // Finalize (computes derived data)
    void finalize();

    // Queries
    const PathNode& getNode(uint32_t id) const;
    const PathSegment& getSegment(uint32_t id) const;
    std::vector<uint32_t> getSegmentsOfType(PathType type) const;
    std::vector<uint32_t> getAdjacentSegments(uint32_t nodeId) const;

    // Spatial queries
    uint32_t findNearestNode(glm::vec2 position) const;
    std::vector<uint32_t> findSegmentsInRadius(glm::vec2 center, float radius) const;

    // Pathfinding within network
    std::vector<uint32_t> findPath(uint32_t startNode, uint32_t endNode) const;

    // Export
    void exportToJSON(const std::filesystem::path& path) const;

private:
    std::vector<PathNode> nodes_;
    std::vector<PathSegment> segments_;
    SpatialIndex2D nodeIndex_;
    SpatialIndex2D segmentIndex_;
};

// ─────────────────────────────────────────────────────────────────────
// PATH GENERATION ALGORITHMS
// ─────────────────────────────────────────────────────────────────────

class SpaceColonization {
public:
    struct Attractor {
        glm::vec2 position;
        float weight = 1.0f;
        enum { KeyBuilding, Boundary, LotFrontage, External } type;
    };

    struct Config {
        float killDistance = 10.0f;
        float influenceRadius = 50.0f;
        float segmentLength = 15.0f;
        float branchAngleLimit = 60.0f;
        int maxIterations = 200;
    };

    // Generate organic street tree
    PathNetwork generate(
        const std::vector<Attractor>& attractors,
        glm::vec2 seedPosition,
        glm::vec2 seedDirection,
        const Config& config
    );
};

class TerrainAwarePaths {
public:
    // Refine path network for terrain
    void refinePaths(
        PathNetwork& network,
        const TerrainInterface& terrain,
        float maxSlope,
        float slopeCostMultiplier
    );

    // Use existing RoadPathfinder for A* routing
    std::vector<glm::vec2> findTerrainPath(
        glm::vec2 start,
        glm::vec2 end,
        const TerrainInterface& terrain
    );
};

class WallPathGenerator {
public:
    // Generate wall path enclosing settlement
    PathNetwork generateWallPath(
        const Polygon& settlementBoundary,
        const std::vector<glm::vec2>& gatePositions,
        const TerrainInterface& terrain
    );
};

class QuayPathGenerator {
public:
    // Generate quay edge following coastline
    PathNetwork generateQuayPath(
        const std::vector<glm::vec2>& coastline,
        float quayLength,
        const TerrainInterface& terrain
    );
};
```

**Key insight**: Streets, walls, quays, and field boundaries are all path networks with different properties. One system handles them all.

**Deliverables**:
- [ ] PathNetwork class with node/segment management
- [ ] SpaceColonization algorithm
- [ ] TerrainAwarePaths using existing RoadPathfinder
- [ ] WallPathGenerator
- [ ] QuayPathGenerator
- [ ] Field boundary generation

---

### 2.2 Layout System

Generates spatial layouts for settlements: zones, lots, blocks.

**Depends on**: Path Network, Spatial Utilities, Terrain Integration

```cpp
// tools/settlement_generator/LayoutSystem.h

// ─────────────────────────────────────────────────────────────────────
// LAYOUT DATA
// ─────────────────────────────────────────────────────────────────────

struct Lot {
    uint32_t id;
    Polygon boundary;
    glm::vec2 centroid;
    float area;

    // Street relationship
    uint32_t frontageSegmentId;     // Which street segment this lot faces
    std::vector<glm::vec2> frontageEdge;
    float frontageWidth;

    // Assignment (filled during generation)
    SettlementZone zone;
    std::string buildingTypeId;
    uint64_t seed;
};

struct Block {
    uint32_t id;
    Polygon boundary;
    std::vector<uint32_t> lotIds;
    std::vector<uint32_t> boundingSegmentIds;  // Street segments that bound this block
};

struct SettlementLayout {
    glm::vec2 center;
    float radius;

    PathNetwork paths;
    std::vector<Block> blocks;
    std::vector<Lot> lots;

    // Key locations
    std::unordered_map<std::string, glm::vec2> keyLocations;  // "church", "market", etc.

    // Zone boundaries
    std::unordered_map<SettlementZone, Polygon> zoneBoundaries;
};

// ─────────────────────────────────────────────────────────────────────
// LAYOUT GENERATOR
// ─────────────────────────────────────────────────────────────────────

class LayoutGenerator {
public:
    SettlementLayout generate(
        const SettlementDefinition& settlement,
        const SettlementTemplateConfig& templateConfig,
        const TerrainInterface& terrain,
        uint64_t seed
    );

private:
    // Step 1: Place key buildings (attractors for street network)
    std::vector<SpaceColonization::Attractor> placeKeyBuildings(
        const SettlementDefinition& settlement,
        const SettlementTemplateConfig& config,
        const TerrainInterface& terrain
    );

    // Step 2: Generate street network
    PathNetwork generateStreets(
        const std::vector<SpaceColonization::Attractor>& attractors,
        const SettlementDefinition& settlement,
        const TerrainInterface& terrain
    );

    // Step 3: Identify blocks (areas enclosed by streets)
    std::vector<Block> identifyBlocks(const PathNetwork& streets);

    // Step 4: Subdivide blocks into lots (aligned to street frontage)
    std::vector<Lot> subdivideLots(
        const std::vector<Block>& blocks,
        const PathNetwork& streets,
        const SettlementTemplateConfig& config
    );

    // Step 5: Assign zones and building types to lots
    void assignBuildingTypes(
        std::vector<Lot>& lots,
        const SettlementTemplateConfig& config,
        const std::vector<SpaceColonization::Attractor>& keyBuildings
    );
};

// ─────────────────────────────────────────────────────────────────────
// SPECIALIZED LAYOUTS
// ─────────────────────────────────────────────────────────────────────

class WaterfrontLayout {
public:
    struct WaterfrontLot : Lot {
        bool hasWaterAccess;
        float quayFrontageWidth;
    };

    std::vector<WaterfrontLot> generateWaterfrontLots(
        const PathNetwork& quayPath,
        const SettlementLayout& baseLayout,
        float lotDepth
    );
};

class AgriculturalLayout {
public:
    struct FieldPlot {
        Polygon boundary;
        enum { StripField, Pasture, Orchard, Fallow } type;
    };

    std::vector<FieldPlot> generateFields(
        glm::vec2 villageCenter,
        float radius,
        const TerrainInterface& terrain,
        const PathNetwork& roads
    );
};

class DefensiveLayout {
public:
    struct DefensivePerimeter {
        PathNetwork wallPath;
        std::vector<glm::vec2> gatePositions;
        std::vector<glm::vec2> towerPositions;
        Polygon enclosedArea;
    };

    DefensivePerimeter generateDefenses(
        const SettlementLayout& settlement,
        const TerrainInterface& terrain,
        bool hasWalls,
        bool hasCastle
    );
};
```

**Deliverables**:
- [ ] LayoutGenerator with key building placement
- [ ] Block identification from street network
- [ ] Lot subdivision with street alignment (CRITICAL)
- [ ] Zone and building type assignment
- [ ] WaterfrontLayout
- [ ] AgriculturalLayout for farm fields
- [ ] DefensiveLayout for walls/gates

---

### 2.3 Mesh Generation System

Low-level mesh creation used by all geometry generators.

**Used by**: Shape Grammar, Building Assembler, Path Network (surfaces)

```cpp
// tools/common/MeshGeneration.h

// ─────────────────────────────────────────────────────────────────────
// MESH PRIMITIVES
// ─────────────────────────────────────────────────────────────────────

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::string materialId;

    // Merge another mesh into this one
    void merge(const Mesh& other);

    // Transform all vertices
    void transform(const glm::mat4& matrix);

    // Generate normals from geometry
    void computeNormals();
    void computeTangents();

    // Compute bounds
    AABB3D bounds() const;
};

// ─────────────────────────────────────────────────────────────────────
// EXTRUSION
// ─────────────────────────────────────────────────────────────────────

class ExtrusionGenerator {
public:
    // Extrude polygon upward
    Mesh extrudePolygon(
        const Polygon& footprint,
        float height,
        bool capTop = true,
        bool capBottom = true
    );

    // Extrude along path (for walls, quays)
    Mesh extrudeAlongPath(
        const Polyline& path,
        const Polygon& profile,     // Cross-section
        bool alignToPath = true
    );

    // Extrude with taper (for towers)
    Mesh extrudeWithTaper(
        const Polygon& footprint,
        float height,
        float topScale             // 0.8 = 80% size at top
    );

    // Extrude with batter (for castle walls)
    Mesh extrudeWithBatter(
        const Polygon& footprint,
        float height,
        float batterAngle          // Inward slope angle
    );
};

// ─────────────────────────────────────────────────────────────────────
// ROOF GENERATION
// ─────────────────────────────────────────────────────────────────────

class RoofGenerator {
public:
    enum class RoofType {
        Flat, Shed, Gable, Hipped, HalfHipped, Gambrel, Mansard
    };

    struct RoofConfig {
        RoofType type;
        float pitch;            // Degrees
        float overhang;         // Eave overhang
        float ridgeHeight;      // For flat-topped roofs
        std::string material;
    };

    // Generate roof from footprint
    Mesh generate(const Polygon& footprint, const RoofConfig& config);

private:
    // Uses straight skeleton from Spatial Utilities
    Mesh generateGable(const Polygon& footprint, float pitch, float overhang);
    Mesh generateHipped(const Polygon& footprint, float pitch, float overhang);
};

// ─────────────────────────────────────────────────────────────────────
// BOOLEAN OPERATIONS (CSG)
// ─────────────────────────────────────────────────────────────────────

class CSGOperations {
public:
    // Cut hole in mesh (for windows, doors, gates)
    Mesh subtract(const Mesh& solid, const Mesh& cutter);

    // Combine meshes
    Mesh unionMeshes(const Mesh& a, const Mesh& b);

    // Simple hole cutting (box-shaped, faster than full CSG)
    void cutRectangularHole(
        Mesh& wall,
        glm::vec3 position,
        glm::vec2 size,
        glm::vec3 normal
    );
};

// ─────────────────────────────────────────────────────────────────────
// PATH SURFACES
// ─────────────────────────────────────────────────────────────────────

class PathMeshGenerator {
public:
    // Generate road/street surface mesh
    Mesh generatePathSurface(
        const Polyline& centerline,
        float width,
        const TerrainInterface& terrain,
        float embankmentSlope = 0.3f
    );

    // Generate wall mesh from path
    Mesh generateWallMesh(
        const Polyline& path,
        float height,
        float thickness,
        float battlementHeight = 0.0f
    );

    // Generate quay mesh
    Mesh generateQuayMesh(
        const Polyline& path,
        float height,
        float width,
        bool hasSteps
    );
};
```

**Deliverables**:
- [ ] Mesh struct with merge, transform, normal computation
- [ ] ExtrusionGenerator (polygon, path, taper, batter)
- [ ] RoofGenerator with straight skeleton integration
- [ ] CSGOperations (at least simple rectangular holes)
- [ ] PathMeshGenerator for roads, walls, quays

---

### 2.4 Shape Grammar System

Hierarchical geometry generation using CGA-style shape grammars.

**Depends on**: Mesh Generation

```cpp
// tools/building_generator/ShapeGrammar.h

// ─────────────────────────────────────────────────────────────────────
// SHAPE NODES
// ─────────────────────────────────────────────────────────────────────

enum class ShapeType {
    Volume,         // 3D box
    Face,           // 2D surface
    Edge,           // 1D line
    Point           // 0D point
};

struct Shape {
    uint32_t id;
    ShapeType type;
    glm::mat4 transform;        // Local transform
    glm::vec3 size;             // Dimensions
    std::string symbol;         // Grammar symbol (e.g., "Facade", "Window")

    std::vector<uint32_t> children;
    uint32_t parent = UINT32_MAX;
};

class ShapeTree {
public:
    uint32_t createRoot(const Polygon& footprint, float height);
    uint32_t addChild(uint32_t parent, const Shape& child);

    Shape& getShape(uint32_t id);
    const Shape& getShape(uint32_t id) const;

    std::vector<uint32_t> findBySymbol(const std::string& symbol) const;

private:
    std::vector<Shape> shapes_;
};

// ─────────────────────────────────────────────────────────────────────
// GRAMMAR RULES
// ─────────────────────────────────────────────────────────────────────

class GrammarRule {
public:
    virtual ~GrammarRule() = default;
    virtual bool matches(const Shape& shape) const = 0;
    virtual void apply(ShapeTree& tree, uint32_t shapeId, uint64_t& seed) = 0;
};

// Split shape along axis
class SplitRule : public GrammarRule {
public:
    enum Axis { X, Y, Z };

    struct Segment {
        enum Type { Absolute, Relative, Repeat, Floating } type;
        float value;
        std::string symbol;
    };

    SplitRule(const std::string& matchSymbol, Axis axis, std::vector<Segment> segments);

    bool matches(const Shape& shape) const override;
    void apply(ShapeTree& tree, uint32_t shapeId, uint64_t& seed) override;

private:
    std::string matchSymbol_;
    Axis axis_;
    std::vector<Segment> segments_;
};

// Split into faces (front, back, left, right, top, bottom)
class ComponentSplitRule : public GrammarRule {
public:
    ComponentSplitRule(
        const std::string& matchSymbol,
        std::unordered_map<std::string, std::string> faceSymbols
    );

    bool matches(const Shape& shape) const override;
    void apply(ShapeTree& tree, uint32_t shapeId, uint64_t& seed) override;
};

// Conditional rule
class ConditionalRule : public GrammarRule {
public:
    using Condition = std::function<bool(const Shape&, const BuildingContext&)>;

    ConditionalRule(
        const std::string& matchSymbol,
        std::vector<std::pair<Condition, std::shared_ptr<GrammarRule>>> branches
    );

    bool matches(const Shape& shape) const override;
    void apply(ShapeTree& tree, uint32_t shapeId, uint64_t& seed) override;
};

// ─────────────────────────────────────────────────────────────────────
// GRAMMAR ENGINE
// ─────────────────────────────────────────────────────────────────────

class ShapeGrammarEngine {
public:
    void addRule(std::shared_ptr<GrammarRule> rule);

    // Execute grammar until no more rules match
    void execute(ShapeTree& tree, uint64_t seed, int maxIterations = 1000);

    // Convert shape tree to mesh
    Mesh generateMesh(const ShapeTree& tree, const MaterialResolver& materials);

private:
    std::vector<std::shared_ptr<GrammarRule>> rules_;
};

// ─────────────────────────────────────────────────────────────────────
// BUILDING-SPECIFIC GRAMMARS
// ─────────────────────────────────────────────────────────────────────

class BuildingGrammarFactory {
public:
    // Create grammar from building type config
    std::vector<std::shared_ptr<GrammarRule>> createGrammar(
        const BuildingTypeConfig& config
    );

    // Pre-built grammars for common types
    static std::vector<std::shared_ptr<GrammarRule>> cottageGrammar();
    static std::vector<std::shared_ptr<GrammarRule>> longhouseGrammar();
    static std::vector<std::shared_ptr<GrammarRule>> churchGrammar();
    static std::vector<std::shared_ptr<GrammarRule>> towerGrammar();
};
```

**Key insight**: One shape grammar system handles all building types (cottages, churches, towers, warehouses) - just different rule sets.

**Deliverables**:
- [ ] Shape and ShapeTree classes
- [ ] SplitRule (X, Y, Z splits)
- [ ] ComponentSplitRule (face extraction)
- [ ] ConditionalRule (branching)
- [ ] ShapeGrammarEngine
- [ ] BuildingGrammarFactory with config-to-rules
- [ ] Pre-built grammars for core building types

---

### 2.5 Placement System

Places objects (props, furniture, vegetation) within spatial constraints.

**Depends on**: Spatial Utilities

```cpp
// tools/common/PlacementSystem.h

// ─────────────────────────────────────────────────────────────────────
// PLACEMENT TYPES
// ─────────────────────────────────────────────────────────────────────

struct PlacementInstance {
    std::string objectId;
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;

    // Metadata
    uint32_t zoneId;            // Which lot/room/area
    std::string category;       // "furniture", "prop", "vegetation"
};

struct PlacementRule {
    std::string objectId;
    std::string category;

    // Spatial constraints
    enum Placement {
        Random,             // Random within area
        Center,             // Center of area
        Edge,               // Along edges
        Corner,             // In corners
        Grid,               // Regular grid
        NearPoint,          // Near specific point
        AlongPath           // Along a path
    } placement;

    float minSpacing;           // Minimum distance between instances
    float maxSpacing;           // For grid/along path

    // Quantity
    float density;              // Instances per square meter
    int minCount, maxCount;     // Or absolute counts
    float probability;          // Chance of appearing at all

    // Orientation
    bool alignToSurface;
    bool randomRotation;
    float rotationVariance;

    // Scale
    glm::vec3 minScale, maxScale;
};

// ─────────────────────────────────────────────────────────────────────
// PLACEMENT ENGINE
// ─────────────────────────────────────────────────────────────────────

class PlacementEngine {
public:
    // Place objects in polygon area
    std::vector<PlacementInstance> placeInArea(
        const Polygon& area,
        const std::vector<PlacementRule>& rules,
        const std::vector<Polygon>& exclusionZones,
        uint64_t seed
    );

    // Place objects in room (interior)
    std::vector<PlacementInstance> placeInRoom(
        const Room& room,
        const std::vector<PlacementRule>& rules,
        uint64_t seed
    );

    // Place objects along path
    std::vector<PlacementInstance> placeAlongPath(
        const Polyline& path,
        const PlacementRule& rule,
        uint64_t seed
    );

private:
    // Poisson disk sampling for random placement
    std::vector<glm::vec2> poissonDiskSample(
        const Polygon& area,
        float minDistance,
        int maxAttempts = 30
    );

    // Check placement validity
    bool isValidPlacement(
        const PlacementInstance& instance,
        const std::vector<PlacementInstance>& existing,
        const std::vector<Polygon>& exclusionZones
    );
};

// ─────────────────────────────────────────────────────────────────────
// SPECIALIZED PLACERS
// ─────────────────────────────────────────────────────────────────────

class ExteriorPropPlacer {
public:
    std::vector<PlacementInstance> placeYardProps(
        const Lot& lot,
        const Polygon& buildingFootprint,
        const std::vector<PlacementRule>& rules,
        uint64_t seed
    );

    std::vector<PlacementInstance> placeStreetProps(
        const PathSegment& street,
        const std::vector<PlacementRule>& rules,
        uint64_t seed
    );
};

class InteriorPropPlacer {
public:
    std::vector<PlacementInstance> placeFurniture(
        const Room& room,
        const std::vector<PlacementRule>& rules,
        uint64_t seed
    );
};

class MaritimePropPlacer {
public:
    std::vector<PlacementInstance> placeQuayProps(
        const Polyline& quayEdge,
        bool hascranes,
        uint64_t seed
    );

    std::vector<PlacementInstance> placeBeachProps(
        const Polygon& beachArea,
        uint64_t seed
    );
};
```

**Deliverables**:
- [ ] PlacementEngine with Poisson disk sampling
- [ ] Area, room, and path-based placement
- [ ] ExteriorPropPlacer
- [ ] InteriorPropPlacer
- [ ] MaritimePropPlacer

---

## 3. Assembly Layer

These systems combine lower-level systems to produce complete assets.

### 3.1 Building Assembler

Assembles complete buildings from components.

```cpp
// tools/building_generator/BuildingAssembler.h

struct BuildingOutput {
    // Geometry
    Mesh exteriorMesh;
    Mesh interiorMesh;          // If has interior
    Mesh collisionMesh;         // Simplified for physics

    // LOD meshes
    std::vector<Mesh> lodMeshes;

    // Metadata
    std::string buildingTypeId;
    glm::vec3 dimensions;
    std::vector<glm::vec3> entryPoints;

    // Interior data
    std::vector<Room> rooms;
    std::vector<PlacementInstance> furniture;

    // Prop attachment points
    std::vector<PlacementInstance> exteriorProps;
};

class BuildingAssembler {
public:
    BuildingAssembler(
        const ConfigRegistry& configs,
        const ShapeGrammarEngine& grammar,
        const PlacementEngine& placement
    );

    BuildingOutput generate(
        const Lot& lot,
        const BuildingTypeConfig& config,
        const TerrainInterface& terrain,
        uint64_t seed
    );

private:
    // Step 1: Generate footprint
    Polygon generateFootprint(const Lot& lot, const BuildingTypeConfig& config, uint64_t seed);

    // Step 2: Generate exterior using shape grammar
    Mesh generateExterior(const Polygon& footprint, const BuildingTypeConfig& config, uint64_t seed);

    // Step 3: Generate interior (if applicable)
    std::pair<Mesh, std::vector<Room>> generateInterior(
        const Polygon& footprint,
        const BuildingTypeConfig& config,
        uint64_t seed
    );

    // Step 4: Generate LODs
    std::vector<Mesh> generateLODs(const Mesh& fullMesh, const BuildingTypeConfig& config);

    // Step 5: Place props
    std::vector<PlacementInstance> placeProps(
        const Lot& lot,
        const Polygon& footprint,
        const BuildingTypeConfig& config,
        uint64_t seed
    );
};
```

---

### 3.2 Settlement Assembler

Assembles complete settlements from layout and buildings.

```cpp
// tools/settlement_generator/SettlementAssembler.h

struct SettlementOutput {
    std::string settlementId;
    glm::vec2 center;
    float radius;

    // Meshes
    Mesh groundMesh;            // Settlement ground surface
    Mesh roadMesh;              // All roads/streets
    Mesh wallMesh;              // Defensive walls (if any)
    std::vector<BuildingOutput> buildings;

    // Props
    std::vector<PlacementInstance> streetProps;
    std::vector<PlacementInstance> vegetationProps;

    // Navigation
    NavMesh navMesh;

    // Metadata for streaming
    std::vector<AABB3D> buildingBounds;
    AABB3D settlementBounds;
};

class SettlementAssembler {
public:
    SettlementAssembler(
        const ConfigRegistry& configs,
        const LayoutGenerator& layout,
        const BuildingAssembler& buildings,
        const PathMeshGenerator& paths,
        const PlacementEngine& placement
    );

    SettlementOutput generate(
        const SettlementDefinition& settlement,
        const TerrainInterface& terrain,
        uint64_t seed
    );

private:
    // Pipeline stages
    SettlementLayout generateLayout(
        const SettlementDefinition& settlement,
        const TerrainInterface& terrain,
        uint64_t seed
    );

    std::vector<BuildingOutput> generateBuildings(
        const SettlementLayout& layout,
        const TerrainInterface& terrain,
        uint64_t seed
    );

    Mesh generateRoads(
        const PathNetwork& streets,
        const TerrainInterface& terrain
    );

    Mesh generateDefenses(
        const DefensiveLayout::DefensivePerimeter& defenses,
        const TerrainInterface& terrain
    );

    std::vector<PlacementInstance> populateProps(
        const SettlementLayout& layout,
        const std::vector<BuildingOutput>& buildings,
        uint64_t seed
    );
};
```

---

## 4. Runtime Layer

Systems that run in the game engine.

### 4.1 Streaming System

Loads settlement data on demand.

```cpp
// src/settlements/SettlementStreaming.h

class SettlementStreamingSystem {
public:
    SettlementStreamingSystem(
        vk::Device device,
        const std::filesystem::path& settlementDataPath
    );

    // Called each frame with camera position
    void update(const glm::vec3& cameraPosition, float dt);

    // Get visible settlements for rendering
    std::vector<const LoadedSettlement*> getVisibleSettlements() const;

private:
    struct SettlementSlot {
        std::string id;
        enum State { Unloaded, Loading, Loaded, Unloading } state;
        float distanceToCamera;
        std::unique_ptr<LoadedSettlement> data;
    };

    std::vector<SettlementSlot> slots_;
    std::priority_queue<LoadRequest> loadQueue_;

    void prioritizeLoading(const glm::vec3& cameraPosition);
    void processLoadQueue();
    void unloadDistant(const glm::vec3& cameraPosition, float maxDistance);
};
```

---

### 4.2 LOD System

Manages level-of-detail transitions.

```cpp
// src/settlements/SettlementLOD.h

class SettlementLODSystem {
public:
    struct LODLevel {
        float maxDistance;
        Mesh* mesh;
        bool useImpostor;
    };

    void update(
        const glm::vec3& cameraPosition,
        const std::vector<LoadedSettlement*>& settlements
    );

    // Get renderable instances for this frame
    std::vector<RenderInstance> getRenderInstances() const;

private:
    // Select LOD level based on distance and screen size
    int selectLODLevel(
        const glm::vec3& objectPosition,
        float objectRadius,
        const glm::vec3& cameraPosition
    );

    // Impostor atlas for distant buildings
    ImpostorAtlas impostorAtlas_;
};
```

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

## 9. 2D Preview Layer & Browser Tooling

The foundation and layout systems are fundamentally **2D operations**. Before generating any 3D meshes, we should be able to visualize and iterate on layouts using SVG preview. This enables:

- Rapid iteration on layout algorithms without 3D rebuild
- Interactive parameter tweaking in browser
- Visual debugging of path networks, lot subdivision, zone assignment
- Sharing previews without requiring the full engine

### 9.1 Architecture: Dual Implementation

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           BROWSER PREVIEW LAYER                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐                │
│    │   Browser    │    │     SVG      │    │  Interactive │                │
│    │   UI (HTML)  │───▶│   Renderer   │───▶│   Controls   │                │
│    └──────────────┘    └──────────────┘    └──────────────┘                │
│           │                                        │                        │
│           ▼                                        ▼                        │
│    ┌──────────────────────────────────────────────────────────┐            │
│    │              TypeScript Layout Library                    │            │
│    │  (Spatial utils, Path network, Layout, Subdivision)       │            │
│    └──────────────────────────────────────────────────────────┘            │
│                              │                                              │
├──────────────────────────────┼──────────────────────────────────────────────┤
│                              │   SHARED ALGORITHM SPEC                      │
├──────────────────────────────┼──────────────────────────────────────────────┤
│                              ▼                                              │
│    ┌──────────────────────────────────────────────────────────┐            │
│    │                C++ Layout Library                         │            │
│    │  (Spatial utils, Path network, Layout, Subdivision)       │            │
│    └──────────────────────────────────────────────────────────┘            │
│                              │                                              │
│                              ▼                                              │
│    ┌──────────────────────────────────────────────────────────┐            │
│    │              3D Mesh Generation (C++ only)                │            │
│    └──────────────────────────────────────────────────────────┘            │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Strategy**: Implement 2D algorithms in both TypeScript (for browser preview) and C++ (for build-time generation). The algorithms are simple enough that maintaining both is feasible, and the TypeScript version serves as executable specification.

### 9.2 Browser Tool Features

```
┌─────────────────────────────────────────────────────────────────────┐
│  Settlement Layout Preview                              [Export SVG] │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────────────────────────────┐  ┌──────────────────────┐ │
│  │                                     │  │ Parameters           │ │
│  │                                     │  │ ─────────────────    │ │
│  │          [SVG Canvas]               │  │ Seed: [12345    ]    │ │
│  │                                     │  │ Type: [Village  ▼]   │ │
│  │     Roads ═══                       │  │ Density: [0.7═══]    │ │
│  │     Streets ───                     │  │ Organicness: [══0.6] │ │
│  │     Lots █ (colored by zone)        │  │                      │ │
│  │     Buildings ▢ (footprints)        │  │ Show Layers          │ │
│  │     Walls ▓▓▓                       │  │ ☑ Roads              │ │
│  │                                     │  │ ☑ Streets            │ │
│  │                                     │  │ ☑ Lots               │ │
│  │                                     │  │ ☑ Buildings          │ │
│  │                                     │  │ ☐ Zone colors        │ │
│  │                                     │  │ ☐ Walls              │ │
│  │                                     │  │ ☐ Fields             │ │
│  └─────────────────────────────────────┘  │                      │ │
│                                           │ [Regenerate]         │ │
│  Lot #47: Residential, 8.2m × 32m        │ [Export JSON]         │ │
│  Street frontage: Main Street             └──────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

**Interactive features**:
- Pan/zoom SVG canvas
- Click elements to inspect (lot dimensions, zone, street frontage)
- Slider controls for all parameters with live regeneration
- Layer visibility toggles
- Seed input for reproducibility
- Export to SVG (for documentation) or JSON (for C++ import)

### 9.3 TypeScript Library Structure

```typescript
// web-tools/src/spatial/

// ─────────────────────────────────────────────────────────────────────
// CORE TYPES (matching C++ structs)
// ─────────────────────────────────────────────────────────────────────

interface Vec2 {
  x: number;
  y: number;
}

interface Polygon {
  vertices: Vec2[];

  area(): number;
  centroid(): Vec2;
  contains(point: Vec2): boolean;
  bounds(): AABB2D;
}

interface Polyline {
  points: Vec2[];
  closed: boolean;

  length(): number;
  pointAt(t: number): Vec2;
  tangentAt(t: number): Vec2;
}

// ─────────────────────────────────────────────────────────────────────
// SUBDIVISION
// ─────────────────────────────────────────────────────────────────────

interface SubdivisionResult {
  cells: Polygon[];
  parentEdges: number[];  // Which original edge each cell touches
}

function frontageSubdivide(
  block: Polygon,
  frontageEdge: Vec2[],
  minLotWidth: number,
  maxLotWidth: number,
  targetDepth: number,
  seed: number
): SubdivisionResult;

function bspSubdivide(
  region: Polygon,
  minCellArea: number,
  minCellWidth: number,
  seed: number
): SubdivisionResult;

// ─────────────────────────────────────────────────────────────────────
// PATH NETWORK
// ─────────────────────────────────────────────────────────────────────

interface PathNode {
  id: number;
  position: Vec2;
  connections: number[];
  type: 'endpoint' | 'junction' | 'gate' | 'entry';
}

interface PathSegment {
  id: number;
  startNode: number;
  endNode: number;
  type: PathType;
  controlPoints: Vec2[];
  geometry?: Polyline;  // Computed after finalize
}

class PathNetwork {
  addNode(position: Vec2, type?: PathNode['type']): number;
  addSegment(startNode: number, endNode: number, type: PathType): number;
  finalize(): void;

  // Queries
  getNode(id: number): PathNode;
  getSegment(id: number): PathSegment;
  findNearestNode(position: Vec2): number;
}

// ─────────────────────────────────────────────────────────────────────
// SPACE COLONIZATION
// ─────────────────────────────────────────────────────────────────────

interface Attractor {
  position: Vec2;
  weight: number;
  type: 'keyBuilding' | 'boundary' | 'lotFrontage' | 'external';
}

interface SpaceColonizationConfig {
  killDistance: number;
  influenceRadius: number;
  segmentLength: number;
  branchAngleLimit: number;
  maxIterations: number;
}

function spaceColonization(
  attractors: Attractor[],
  seedPosition: Vec2,
  seedDirection: Vec2,
  config: SpaceColonizationConfig
): PathNetwork;

// ─────────────────────────────────────────────────────────────────────
// LAYOUT GENERATION
// ─────────────────────────────────────────────────────────────────────

interface Lot {
  id: number;
  boundary: Polygon;
  centroid: Vec2;
  area: number;

  frontageSegmentId: number;
  frontageEdge: Vec2[];
  frontageWidth: number;

  zone: SettlementZone;
  buildingTypeId?: string;
}

interface SettlementLayout {
  center: Vec2;
  radius: number;
  paths: PathNetwork;
  lots: Lot[];
  keyLocations: Map<string, Vec2>;
}

function generateLayout(
  settlement: SettlementDefinition,
  template: SettlementTemplateConfig,
  terrainSampler: (pos: Vec2) => { height: number; slope: number },
  seed: number
): SettlementLayout;
```

### 9.4 SVG Renderer

```typescript
// web-tools/src/renderer/SVGRenderer.ts

interface RenderOptions {
  showRoads: boolean;
  showStreets: boolean;
  showLots: boolean;
  showBuildings: boolean;
  showZoneColors: boolean;
  showWalls: boolean;
  showFields: boolean;
  showLabels: boolean;

  roadColor: string;
  streetColor: string;
  lotStrokeColor: string;
  buildingColor: string;

  zoneColors: Record<SettlementZone, string>;
}

class SVGRenderer {
  constructor(private svg: SVGSVGElement) {}

  render(layout: SettlementLayout, options: RenderOptions): void {
    this.clear();

    if (options.showRoads) {
      this.renderPaths(layout.paths, 'MainRoad', options.roadColor, 8);
    }
    if (options.showStreets) {
      this.renderPaths(layout.paths, 'Street', options.streetColor, 4);
      this.renderPaths(layout.paths, 'Lane', options.streetColor, 2);
    }
    if (options.showLots) {
      this.renderLots(layout.lots, options);
    }
    if (options.showBuildings) {
      this.renderBuildingFootprints(layout.lots, options.buildingColor);
    }
  }

  private renderPaths(
    network: PathNetwork,
    type: PathType,
    color: string,
    width: number
  ): void {
    for (const segment of network.getSegmentsOfType(type)) {
      const path = document.createElementNS(SVG_NS, 'path');
      path.setAttribute('d', this.polylineToPath(segment.geometry));
      path.setAttribute('stroke', color);
      path.setAttribute('stroke-width', String(width));
      path.setAttribute('fill', 'none');
      path.setAttribute('stroke-linecap', 'round');
      path.setAttribute('stroke-linejoin', 'round');
      this.svg.appendChild(path);
    }
  }

  private renderLots(lots: Lot[], options: RenderOptions): void {
    for (const lot of lots) {
      const polygon = document.createElementNS(SVG_NS, 'polygon');
      polygon.setAttribute('points', this.polygonToPoints(lot.boundary));

      if (options.showZoneColors) {
        polygon.setAttribute('fill', options.zoneColors[lot.zone]);
        polygon.setAttribute('fill-opacity', '0.3');
      } else {
        polygon.setAttribute('fill', 'none');
      }

      polygon.setAttribute('stroke', options.lotStrokeColor);
      polygon.setAttribute('stroke-width', '1');
      polygon.dataset.lotId = String(lot.id);
      polygon.dataset.zone = lot.zone;

      this.svg.appendChild(polygon);
    }
  }
}
```

### 9.5 Integration with C++ Build

The browser tool generates JSON that the C++ pipeline can consume:

```typescript
// Export format
interface LayoutExport {
  version: 1;
  seed: number;
  settlement: SettlementDefinition;
  template: string;  // Template ID

  // Generated data (can be imported by C++ to skip regeneration)
  paths: {
    nodes: PathNode[];
    segments: PathSegment[];
  };
  lots: Lot[];
  keyLocations: Record<string, Vec2>;
}

function exportLayout(layout: SettlementLayout): LayoutExport;
```

C++ can either:
1. **Regenerate from seed**: Use same algorithms, verify output matches
2. **Import layout**: Skip 2D generation, just do 3D mesh generation

```cpp
// tools/settlement_generator/LayoutImporter.h

class LayoutImporter {
public:
    // Import layout from browser tool JSON
    SettlementLayout importFromJSON(const std::filesystem::path& jsonPath);

    // Verify our C++ generation matches browser output
    bool verifyAgainstJSON(
        const SettlementLayout& generated,
        const std::filesystem::path& jsonPath,
        float tolerance = 0.01f
    );
};
```

### 9.6 C++ SVG Export (for debugging)

The C++ tools should also be able to export SVG for debugging:

```cpp
// tools/common/SVGExporter.h

class SVGExporter {
public:
    void begin(float width, float height);

    // Primitives
    void drawPolygon(
        const Polygon& poly,
        const std::string& fill = "none",
        const std::string& stroke = "black",
        float strokeWidth = 1.0f
    );

    void drawPolyline(
        const Polyline& line,
        const std::string& stroke = "black",
        float strokeWidth = 1.0f
    );

    void drawCircle(
        glm::vec2 center,
        float radius,
        const std::string& fill = "none",
        const std::string& stroke = "black"
    );

    void drawText(
        glm::vec2 position,
        const std::string& text,
        float fontSize = 12.0f
    );

    // High-level
    void drawPathNetwork(const PathNetwork& network, const RenderOptions& options);
    void drawLots(const std::vector<Lot>& lots, bool colorByZone);
    void drawSettlement(const SettlementLayout& layout, const RenderOptions& options);

    void save(const std::filesystem::path& path);

private:
    std::stringstream svg_;
    float width_, height_;
};
```

### 9.7 File Structure for Web Tools

```
web-tools/
├── package.json
├── tsconfig.json
├── vite.config.ts              # Or webpack/esbuild config
│
├── src/
│   ├── spatial/
│   │   ├── types.ts            # Vec2, Polygon, Polyline, AABB
│   │   ├── polygon.ts          # Polygon operations
│   │   ├── subdivision.ts      # BSP, grid, frontage subdivision
│   │   └── straightSkeleton.ts # Straight skeleton (simplified)
│   │
│   ├── paths/
│   │   ├── PathNetwork.ts
│   │   ├── spaceColonization.ts
│   │   └── pathSmoothing.ts
│   │
│   ├── layout/
│   │   ├── LayoutGenerator.ts
│   │   ├── blockIdentification.ts
│   │   ├── lotSubdivision.ts
│   │   └── zoneAssignment.ts
│   │
│   ├── renderer/
│   │   ├── SVGRenderer.ts
│   │   └── interactivity.ts    # Pan, zoom, selection
│   │
│   ├── ui/
│   │   ├── App.tsx             # Main React/Preact component
│   │   ├── ParameterPanel.tsx
│   │   ├── LayerControls.tsx
│   │   └── Inspector.tsx
│   │
│   └── export/
│       ├── jsonExport.ts
│       └── svgExport.ts
│
├── public/
│   └── index.html
│
└── tests/
    ├── subdivision.test.ts
    ├── spaceColonization.test.ts
    └── layout.test.ts
```

### 9.8 Development Workflow

```
┌─────────────────────────────────────────────────────────────────────┐
│                      DEVELOPMENT WORKFLOW                            │
└─────────────────────────────────────────────────────────────────────┘

1. DESIGN PHASE (Browser)
   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
   │  Tweak      │────▶│  Preview    │────▶│  Export     │
   │  Parameters │     │  SVG        │     │  JSON       │
   └─────────────┘     └─────────────┘     └─────────────┘
         │                                        │
         │ Fast iteration                         │ Lock in design
         ▼                                        ▼

2. IMPLEMENTATION PHASE (C++)
   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
   │  Import or  │────▶│  Generate   │────▶│  Verify     │
   │  Regenerate │     │  3D Meshes  │     │  In Engine  │
   └─────────────┘     └─────────────┘     └─────────────┘
         │
         │ Verify matches browser output
         ▼

3. TESTING PHASE
   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
   │  C++ SVG    │────▶│  Compare    │────▶│  Fix        │
   │  Export     │     │  Outputs    │     │  Diffs      │
   └─────────────┘     └─────────────┘     └─────────────┘
```

### 9.9 Algorithm Parity Testing

To ensure C++ and TypeScript implementations produce identical results:

```typescript
// web-tools/tests/parity.test.ts

describe('Algorithm Parity', () => {
  const testCases = loadTestCases('fixtures/layout-tests.json');

  for (const testCase of testCases) {
    it(`should match C++ output for ${testCase.name}`, () => {
      const tsResult = generateLayout(
        testCase.settlement,
        testCase.template,
        testCase.seed
      );

      // Compare against pre-generated C++ output
      const cppResult = loadCppOutput(`fixtures/cpp-output/${testCase.name}.json`);

      expect(tsResult.lots.length).toBe(cppResult.lots.length);

      for (let i = 0; i < tsResult.lots.length; i++) {
        expect(tsResult.lots[i].centroid.x).toBeCloseTo(cppResult.lots[i].centroid.x, 2);
        expect(tsResult.lots[i].centroid.y).toBeCloseTo(cppResult.lots[i].centroid.y, 2);
        expect(tsResult.lots[i].area).toBeCloseTo(cppResult.lots[i].area, 1);
      }
    });
  }
});
```

### 9.10 Deliverables - 2D Preview Layer

**TypeScript Library**:
- [ ] Spatial types (Vec2, Polygon, Polyline, AABB)
- [ ] Polygon operations (area, centroid, contains, bounds)
- [ ] Frontage subdivision algorithm
- [ ] BSP subdivision algorithm
- [ ] PathNetwork class
- [ ] Space colonization algorithm
- [ ] Layout generator

**Browser UI**:
- [ ] SVG renderer with pan/zoom
- [ ] Parameter panel with live update
- [ ] Layer visibility toggles
- [ ] Click-to-inspect functionality
- [ ] JSON export
- [ ] SVG export

**C++ Integration**:
- [ ] SVGExporter class
- [ ] LayoutImporter (JSON → SettlementLayout)
- [ ] Parity verification tests

**Testing**:
- Open browser tool at `http://localhost:5173`
- Adjust parameters, see layout update in real-time
- Export JSON, import into C++, verify mesh generation
- Compare C++ SVG export against browser preview

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
