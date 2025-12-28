# Implementation - Foundation Layer

[← Back to Index](README.md)

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
