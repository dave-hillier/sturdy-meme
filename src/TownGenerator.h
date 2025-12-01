#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>
#include "VoronoiDiagram.h"
#include "BuildingModules.h"

// Types of zones in the settlement
enum class ZoneType {
    TownCenter,      // Central market/square area
    Residential,     // Houses and homes
    Commercial,      // Shops, taverns, workshops
    Agricultural,    // Farms and fields
    Wilderness,      // Empty/forest areas
    Road             // Road segments
};

// Building types for medieval settlements
enum class BuildingType {
    SmallHouse,      // Simple peasant dwelling
    MediumHouse,     // Larger home
    Tavern,          // Inn/pub
    Workshop,        // Blacksmith, carpenter, etc.
    Church,          // Religious building
    WatchTower,      // Defensive tower
    Well,            // Town well
    Market,          // Market stall
    Barn,            // Farm building
    Windmill         // Grain mill
};

// A building placement in the town - now includes modular building data
struct BuildingPlacement {
    BuildingType type;
    glm::vec3 position;       // World position (including terrain height)
    float rotation;           // Y-axis rotation in radians
    float scale;              // Uniform scale factor
    glm::vec3 dimensions;     // Width, height, depth of building
    uint32_t cellIndex;       // Which Voronoi cell it belongs to

    // Modular building grid dimensions (in modules)
    glm::ivec3 gridSize{2, 2, 2};

    // WFC result - indices of chosen modules for each grid cell
    // Stored as a flat array: [x + y * gridSize.x + z * gridSize.x * gridSize.y]
    std::vector<size_t> moduleGrid;
};

// A road segment connecting points
struct RoadSegment {
    glm::vec3 start;
    glm::vec3 end;
    float width;
    bool isMainRoad;          // Main roads are wider
};

// Zone assignment for a Voronoi cell
struct ZoneAssignment {
    ZoneType type;
    float suitability;        // How suitable this cell is for its zone (0-1)
    bool hasCentralBuilding;  // Has a key building (church, well, etc.)
};

// Configuration for town generation
struct TownConfig {
    glm::vec2 center = glm::vec2(0.0f);      // Town center in world XZ
    float radius = 100.0f;                    // Approximate town radius
    int numCells = 50;                        // Voronoi cells for layout
    int relaxIterations = 3;                  // Lloyd relaxation passes
    float roadWidth = 2.0f;                   // Base road width
    float mainRoadWidth = 3.5f;               // Main road width
    float maxBuildingSlope = 0.3f;            // Max terrain slope for buildings
    float buildingDensity = 0.6f;             // How densely packed buildings are (0-1)
    uint32_t seed = 12345;                    // Random seed
    float minBuildingSpacing = 2.0f;          // Minimum distance between buildings
};

// Terrain height sampling function type
using TerrainHeightFunc = std::function<float(float x, float z)>;

class TownGenerator {
public:
    TownGenerator();
    ~TownGenerator() = default;

    // Generate a town layout
    // heightFunc: function to sample terrain height at (x, z)
    void generate(const TownConfig& config, TerrainHeightFunc heightFunc);

    // Accessors for generated data
    const std::vector<BuildingPlacement>& getBuildings() const { return buildings; }
    const std::vector<RoadSegment>& getRoads() const { return roads; }
    const VoronoiDiagram& getVoronoi() const { return voronoi; }
    const std::vector<ZoneAssignment>& getZones() const { return zones; }
    const ModuleLibrary& getModuleLibrary() const { return moduleLibrary; }

    // Get zone type at a world position
    ZoneType getZoneAt(const glm::vec2& worldPos) const;

    // Check if a position is on a road
    bool isOnRoad(const glm::vec2& worldPos, float tolerance = 1.0f) const;

private:
    // Generation steps
    void generateVoronoiLayout();
    void assignZones();
    void generateRoads();
    void placeBuildings();

    // Helper functions
    float evaluateBuildingSuitability(const glm::vec2& pos) const;
    float getTerrainSlope(const glm::vec2& pos) const;
    float getTerrainHeight(const glm::vec2& pos) const;
    bool canPlaceBuilding(const glm::vec2& pos, const glm::vec2& size) const;
    BuildingType selectBuildingType(ZoneType zone, float random) const;
    glm::vec3 getBuildingDimensions(BuildingType type) const;
    glm::ivec3 getBuildingGridSize(BuildingType type) const;

    // Generate modular building using WFC
    void generateModularBuilding(BuildingPlacement& building, uint32_t seed);

    // Hash functions for deterministic randomness
    float hash(const glm::vec2& p) const;
    glm::vec2 hash2(const glm::vec2& p) const;

    TownConfig config;
    TerrainHeightFunc heightFunc;

    VoronoiDiagram voronoi;
    std::vector<ZoneAssignment> zones;
    std::vector<BuildingPlacement> buildings;
    std::vector<RoadSegment> roads;

    // Building placement tracking (to prevent overlaps)
    std::vector<glm::vec4> placedBuildingBounds;  // xy = center, zw = half-extents

    // Module library for WFC building generation
    ModuleLibrary moduleLibrary;
};
