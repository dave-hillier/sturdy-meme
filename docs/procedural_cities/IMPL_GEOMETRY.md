# Implementation - Geometry Layer

[← Back to Index](README.md)

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

// ─────────────────────────────────────────────────────────────────────
// WATER CROSSING DETECTION (Bridges & Fords)
// ─────────────────────────────────────────────────────────────────────

struct WaterCrossing {
    uint32_t id;
    uint32_t pathSegmentId;
    glm::vec2 position;
    glm::vec2 approachDir;           // Direction road approaches from

    enum class Type : uint8_t {
        Ford,               // Shallow water, road dips through
        SteppingStones,     // Footpath crossing
        ClapperBridge,      // Simple stone slabs (moorland)
        TimberBridge,       // Wood construction
        StoneBridge         // Masonry arch (expensive, durable)
    } type;

    float span;                      // Distance across water (meters)
    float waterDepth;                // Depth at crossing point
    float bankSlopeLeft;             // Slope of left bank
    float bankSlopeRight;            // Slope of right bank
    bool hasTollPoint = false;
    uint32_t nearestSettlementId = UINT32_MAX;
};

class WaterCrossingDetector {
public:
    struct Config {
        // Ford viability thresholds
        float maxFordDepth = 0.5f;           // Max water depth for ford
        float maxFordWidth = 20.0f;          // Max river width for ford
        float maxFordBankSlope = 0.26f;      // ~15 degrees

        // Pathfinder cost equivalents (for type selection)
        float fordCostThreshold = 300.0f;
        float timberBridgeCostThreshold = 800.0f;
        // Above timber threshold → stone bridge
    };

    // Detect all water crossings in a path network
    std::vector<WaterCrossing> detectCrossings(
        const PathNetwork& roads,
        const TerrainInterface& terrain,
        const Config& config
    );

    // Classify crossing type based on road importance and water characteristics
    WaterCrossing::Type classifyCrossing(
        const PathSegment& segment,
        float waterDepth,
        float riverWidth,
        float bankSlope,
        const Config& config
    );

    // Check if ford is viable at location
    bool isFordViable(
        glm::vec2 position,
        const TerrainInterface& terrain,
        const Config& config
    );

private:
    // Find where path crosses from land to water and back
    std::vector<std::pair<glm::vec2, glm::vec2>> findWaterIntersections(
        const PathSegment& segment,
        const TerrainInterface& terrain
    );
};

// Integration with settlement scoring
class BridgeSettlementBooster {
public:
    // Adjust settlement scores based on bridge proximity
    void applyBridgeBonus(
        std::vector<Settlement>& settlements,
        const std::vector<WaterCrossing>& crossings,
        float searchRadius = 500.0f,
        float majorRouteBonus = 0.15f,
        float minorRouteBonus = 0.05f
    );

    // Identify "bridge towns" - settlements that grew around crossings
    std::vector<uint32_t> identifyBridgeTowns(
        const std::vector<Settlement>& settlements,
        const std::vector<WaterCrossing>& crossings
    );
};
```

**Key insight**: Streets, walls, quays, field boundaries, and water crossings are all path networks with different properties. One system handles them all.

**Deliverables**:
- [ ] PathNetwork class with node/segment management
- [ ] SpaceColonization algorithm
- [ ] TerrainAwarePaths using existing RoadPathfinder
- [ ] WallPathGenerator
- [ ] QuayPathGenerator
- [ ] Field boundary generation
- [ ] WaterCrossingDetector (bridge/ford detection and classification)
- [ ] BridgeSettlementBooster (settlement scoring based on crossings)

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
