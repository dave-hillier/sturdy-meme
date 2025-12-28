# Implementation - Assembly Layer

[← Back to Index](README.md)

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
