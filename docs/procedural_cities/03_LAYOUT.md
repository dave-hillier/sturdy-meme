# Procedural Cities - Phase 2: Settlement Layout

[← Back to Index](README.md)

## 4. Phase 2: Settlement Layout Generation

### 4.1 Layout Algorithm Overview

The layout generation uses a hybrid approach combining:

1. **Template-Based Seeding**: Initial placement of key landmarks
2. **Agent-Based Growth**: Organic street/lot expansion
3. **Constraint Satisfaction**: Ensuring accessibility and coherence

```
┌────────────────────────────────────────────────────────────────┐
│                    LAYOUT GENERATION PIPELINE                   │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  1. TERRAIN ANALYSIS                                           │
│     ├── Sample height grid around center                       │
│     ├── Compute slope map                                      │
│     ├── Identify buildable areas (slope < threshold)           │
│     └── Mark water, cliffs, existing roads                     │
│                                                                │
│  2. SEED PLACEMENT                                             │
│     ├── Place settlement core (church, green, market)          │
│     ├── Connect to external road network                       │
│     └── Establish main axis orientation                        │
│                                                                │
│  3. STREET NETWORK GENERATION                                  │
│     ├── Grow main street from external connection              │
│     ├── Branch secondary streets using L-system rules          │
│     ├── Add lanes and alleys for access                        │
│     └── Smooth and snap to terrain                             │
│                                                                │
│  4. LOT SUBDIVISION                                            │
│     ├── Identify blocks between streets                        │
│     ├── Subdivide blocks into lots                             │
│     ├── Assign zones based on template                         │
│     └── Validate street frontage                               │
│                                                                │
│  5. BUILDING ASSIGNMENT                                        │
│     ├── Place required buildings (church, inn, etc.)           │
│     ├── Fill remaining lots with appropriate types             │
│     └── Resolve conflicts and adjust                           │
│                                                                │
│  6. OUTPUT                                                     │
│     ├── Serialize layout to JSON                               │
│     ├── Generate debug visualization                           │
│     └── Compute terrain modification mask                      │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

### 4.2 Terrain Integration

#### 4.2.1 Buildable Area Analysis

```cpp
struct TerrainAnalysis {
    std::vector<float> heightGrid;      // Sampled heights
    std::vector<float> slopeGrid;       // Computed slopes
    std::vector<uint8_t> buildableGrid; // 0=unbuildable, 255=ideal

    uint32_t gridSize;
    float cellSize;                     // Meters per cell
    glm::vec2 origin;                   // World coords of grid corner

    // Sample buildability at world position
    float getBuildability(glm::vec2 worldPos) const;

    // Get terrain height at world position
    float getHeight(glm::vec2 worldPos) const;

    // Get slope at world position
    float getSlope(glm::vec2 worldPos) const;
};

class TerrainAnalyzer {
public:
    TerrainAnalysis analyze(
        const SettlementDefinition& settlement,
        float analysisRadius,           // How far to analyze
        float cellSize = 2.0f           // Analysis resolution
    );

private:
    // Integration with existing systems
    float sampleTerrainHeight(glm::vec2 pos) const;
    BiomeZone sampleBiome(glm::vec2 pos) const;
    bool isWater(glm::vec2 pos) const;
    bool isCliff(glm::vec2 pos) const;
};
```

#### 4.2.2 Terrain Modification

Buildings need flat foundations. Rather than flattening terrain globally, we compute per-lot modifications:

```cpp
struct TerrainModification {
    glm::vec2 center;
    glm::vec2 extents;
    float targetHeight;
    float blendRadius;          // How far to blend back to natural

    enum class Type {
        Flatten,                // Level to target height
        Cut,                    // Only remove material
        Fill,                   // Only add material
        Terrace                 // Create terraced steps
    } type;
};

class TerrainModifier {
public:
    // Compute modifications needed for a lot
    TerrainModification computeForLot(
        const BuildingLot& lot,
        const TerrainAnalysis& terrain
    );

    // Generate height modification texture
    void bakeModificationMap(
        const std::vector<TerrainModification>& mods,
        const std::string& outputPath
    );
};
```

### 4.3 Street Network Algorithm

Hybrid approach combining space colonization for organic growth with A* for terrain-aware pathfinding.

#### 4.3.1 Algorithm Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                 STREET NETWORK GENERATION                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. ATTRACTOR PLACEMENT                                         │
│     ├── Key buildings (church, inn, market)                     │
│     ├── External road connections                               │
│     ├── Settlement boundary points                              │
│     └── Lot frontage requirements                               │
│                                                                 │
│  2. SPACE COLONIZATION                                          │
│     ├── Grow street tree from seed (main road entry)            │
│     ├── Branches compete for nearby attractors                  │
│     ├── Kill attractors when reached                            │
│     └── Natural organic branching pattern                       │
│                                                                 │
│  3. A* TERRAIN REFINEMENT                                       │
│     ├── For each street segment: find terrain-optimal path      │
│     ├── Cost function: slope + water crossing + cliff penalty   │
│     ├── Reuse existing RoadPathfinder infrastructure            │
│     └── Smooth paths with spline interpolation                  │
│                                                                 │
│  4. HIERARCHY ASSIGNMENT                                        │
│     ├── Main street: connects to external roads                 │
│     ├── Streets: branch from main, serve multiple lots          │
│     ├── Lanes: serve individual lots                            │
│     └── Alleys: rear access                                     │
│                                                                 │
│  5. INTERSECTION RESOLUTION                                     │
│     ├── Merge nearby endpoints                                  │
│     ├── Validate connectivity                                   │
│     └── Generate intersection geometry                          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

#### 4.3.2 Space Colonization Algorithm

Based on Runions et al. (2007) "Modeling Trees with a Space Colonization Algorithm":

```cpp
struct Attractor {
    glm::vec2 position;
    float weight;               // Importance (key buildings = high)
    bool reached = false;

    enum class Type {
        ExternalRoad,           // Connection to other settlements
        KeyBuilding,            // Church, inn, market - must be accessible
        LotFrontage,            // Lots need street access
        BoundaryPoint           // Settlement perimeter coverage
    } type;
};

struct StreetNode {
    glm::vec2 position;
    StreetNode* parent = nullptr;
    std::vector<StreetNode*> children;

    float width;                // Street width at this node
    int depth;                  // Distance from root in tree
};

class SpaceColonizationStreets {
public:
    struct Config {
        float attractorKillDistance = 10.0f;    // Remove attractor when this close
        float attractorInfluenceRadius = 50.0f; // Attractors affect nodes within this
        float segmentLength = 15.0f;            // Growth step size
        float branchAngleLimit = 60.0f;         // Max deviation from parent direction
        int maxIterations = 200;
    };

    StreetNetwork generate(
        const std::vector<Attractor>& attractors,
        glm::vec2 seedPosition,                 // Main road entry point
        glm::vec2 seedDirection,                // Initial direction
        const Config& config
    );

private:
    // Find attractors influencing a node
    std::vector<const Attractor*> findInfluencingAttractors(
        const StreetNode& node,
        const std::vector<Attractor>& attractors
    );

    // Calculate growth direction from attractors
    glm::vec2 calculateGrowthDirection(
        const StreetNode& node,
        const std::vector<const Attractor*>& influencers
    );

    // Check if node can branch (enough attractors in cone)
    bool shouldBranch(
        const StreetNode& node,
        const std::vector<Attractor>& attractors
    );
};
```

#### 4.3.3 Terrain-Aware Path Refinement

```cpp
class TerrainAwareStreetRefiner {
public:
    // Refine street segment using A* on terrain
    std::vector<glm::vec2> refinePath(
        glm::vec2 start,
        glm::vec2 end,
        const TerrainAnalysis& terrain,
        const RoadPathfinder& pathfinder     // Reuse existing infrastructure
    );

    // Cost function for street placement
    float calculateCost(glm::vec2 from, glm::vec2 to, const TerrainAnalysis& terrain) {
        float baseCost = glm::length(to - from);

        // Slope penalty (prefer following contours)
        float slope = terrain.getSlope((from + to) * 0.5f);
        float slopeCost = slope * slopePenaltyMultiplier;

        // Water crossing penalty
        bool crossesWater = terrain.isWater(to);
        float waterCost = crossesWater ? waterPenalty : 0.0f;

        // Cliff penalty (impassable)
        bool isCliff = slope > cliffThreshold;
        float cliffCost = isCliff ? INFINITY : 0.0f;

        // Prefer following existing paths/desire lines
        float existingPathBonus = terrain.hasExistingPath(to) ? -0.5f : 0.0f;

        return baseCost + slopeCost + waterCost + cliffCost + existingPathBonus;
    }

private:
    float slopePenaltyMultiplier = 5.0f;
    float waterPenalty = 100.0f;
    float cliffThreshold = 0.7f;
};
```

#### 4.3.4 Street Hierarchy Assignment

```cpp
class StreetHierarchyAssigner {
public:
    void assignHierarchy(StreetNetwork& network) {
        // Main street: connects settlement to external road network
        markMainStreet(network);

        // Streets: branches from main serving multiple buildings
        markStreets(network);

        // Lanes: short segments serving 1-3 buildings
        markLanes(network);

        // Alleys: rear access, very narrow
        markAlleys(network);
    }

private:
    void markMainStreet(StreetNetwork& network) {
        // Path from external connection through settlement center
        // Widest street type (8m for main road)
    }

    void markStreets(StreetNetwork& network) {
        // Branches serving 4+ lots
        // Medium width (5m)
    }

    void markLanes(StreetNetwork& network) {
        // Segments serving 1-3 lots
        // Narrow (3.5m)
    }

    void markAlleys(StreetNetwork& network) {
        // Rear access only
        // Very narrow (2m)
    }
};
```

#### 4.3.5 Complete Street Generator

```cpp
class StreetNetworkGenerator {
public:
    struct Config {
        // Street widths
        float mainStreetWidth = 8.0f;
        float streetWidth = 5.0f;
        float laneWidth = 3.5f;
        float alleyWidth = 2.0f;

        // Space colonization params
        float attractorKillDistance = 10.0f;
        float attractorInfluenceRadius = 50.0f;
        float segmentLength = 15.0f;

        // Terrain params
        float maxStreetSlope = 0.15f;       // 15% grade max for streets
        float maxLaneSlope = 0.25f;         // Lanes can be steeper
    };

    StreetNetwork generate(
        const SettlementDefinition& settlement,
        const TerrainAnalysis& terrain,
        const std::vector<BuildingPlacement>& keyBuildings,
        const Config& config
    );

private:
    SpaceColonizationStreets colonization;
    TerrainAwareStreetRefiner refiner;
    StreetHierarchyAssigner hierarchy;

    // Generate attractors from key buildings and settlement bounds
    std::vector<Attractor> generateAttractors(
        const SettlementDefinition& settlement,
        const std::vector<BuildingPlacement>& keyBuildings
    );

    // Connect to external road network
    glm::vec2 findExternalConnection(
        const SettlementDefinition& settlement,
        const RoadNetwork& externalRoads
    );

    // Detect and merge street intersections
    void mergeIntersections(StreetNetwork& network, float threshold = 5.0f);

    // Validate all key buildings are accessible
    bool validateConnectivity(
        const StreetNetwork& network,
        const std::vector<BuildingPlacement>& keyBuildings
    );
};
```

### 4.4 Lot Subdivision Algorithm

#### 4.4.1 Block Identification

```cpp
class BlockIdentifier {
public:
    // Find closed polygons formed by streets
    std::vector<Polygon> findBlocks(const StreetNetwork& network);

private:
    // Use Boost.Polygon or similar for polygon operations
    std::vector<Polygon> computeVoronoi(const std::vector<glm::vec2>& sites);
    Polygon clipToSettlementBounds(const Polygon& poly, float radius);
};
```

#### 4.4.2 Lot Subdivision

Using OBB (Oriented Bounding Box) recursive subdivision:

```cpp
class LotSubdivider {
public:
    struct Config {
        glm::vec2 minLotSize = {10, 15};
        glm::vec2 maxLotSize = {25, 40};
        float streetSetback = 2.0f;
        float rearSetback = 3.0f;
        float sideSetback = 1.5f;
    };

    std::vector<BuildingLot> subdivide(
        const Polygon& block,
        const StreetNetwork& streets,
        const Config& config
    );

private:
    // Recursive OBB split
    void splitBlock(
        const Polygon& block,
        std::vector<BuildingLot>& lots,
        int depth = 0
    );

    // Determine split axis (parallel to longest street frontage)
    glm::vec2 getSplitAxis(const Polygon& block, const StreetNetwork& streets);

    // Check if lot has valid street access
    bool validateStreetAccess(const BuildingLot& lot, const StreetNetwork& streets);
};
```

### 4.5 Zone Assignment

#### 4.5.1 Zone Placement Strategy

```cpp
class ZonePlanner {
public:
    void assignZones(
        std::vector<BuildingLot>& lots,
        const SettlementDefinition& settlement,
        const StreetNetwork& streets,
        const SettlementTemplate& tmpl
    );

private:
    // Place required features first
    void placeRequiredFeatures(std::vector<BuildingLot>& lots);

    // Score each lot for each zone type
    float scoreLotForZone(const BuildingLot& lot, SettlementZone zone);

    // Use Hungarian algorithm for optimal assignment
    void optimizeAssignment(std::vector<BuildingLot>& lots);
};
```

### 4.6 Deliverables - Phase 2

- [ ] TerrainAnalyzer implementation
- [ ] StreetNetworkGenerator with L-system rules
- [ ] LotSubdivider with OBB algorithm
- [ ] ZonePlanner with constraint satisfaction
- [ ] Layout serialization to JSON
- [ ] Debug visualization output (SVG/PNG)
- [ ] Integration tests with existing biome system

**Testing**: Generate layout for each settlement type, verify:
- All lots have street access
- No overlapping lots
- Streets connect to external road network
- Zones distributed according to template

---
