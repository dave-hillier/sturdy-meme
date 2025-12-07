#pragma once

#include "RoadSpline.h"
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

namespace RoadGen {

// Configuration for the pathfinder
struct PathfinderConfig {
    float terrainSize = 16384.0f;       // World size in meters
    float minAltitude = 0.0f;           // Min heightmap altitude
    float maxAltitude = 200.0f;         // Max heightmap altitude
    float seaLevel = 0.0f;              // Height at which sea starts
    uint32_t gridResolution = 512;      // Pathfinding grid resolution

    // Cost weights
    float slopeCostMultiplier = 5.0f;   // Extra cost per unit slope
    float waterPenalty = 1000.0f;       // Penalty for crossing water
    float cliffPenalty = 500.0f;        // Penalty for cliff areas
    float cliffSlopeThreshold = 0.5f;   // Slope above this is a cliff

    // Simplification
    float simplifyEpsilon = 10.0f;      // Douglas-Peucker simplification threshold (meters)
};

// Terrain data loaded for pathfinding
struct TerrainData {
    std::vector<float> heights;         // Normalized [0,1] heights
    std::vector<uint8_t> biomeZones;    // BiomeZone values
    uint32_t width = 0;
    uint32_t height = 0;

    float sampleHeight(float x, float z, float terrainSize) const;
    float sampleSlope(float x, float z, float terrainSize) const;
    BiomeZone sampleBiome(float x, float z, float terrainSize) const;
    bool isWater(float x, float z, float terrainSize) const;
};

// A* pathfinder for road generation
class RoadPathfinder {
public:
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;

    RoadPathfinder();
    ~RoadPathfinder() = default;

    // Initialize with configuration
    void init(const PathfinderConfig& config);

    // Load terrain data from files
    bool loadHeightmap(const std::string& path);
    bool loadBiomeMap(const std::string& path);

    // Find path between two world positions
    // Returns true if path found, fills outPath with control points
    bool findPath(glm::vec2 start, glm::vec2 end, std::vector<RoadControlPoint>& outPath);

    // Generate the full road network connecting settlements
    bool generateRoadNetwork(const std::vector<Settlement>& settlements,
                             RoadNetwork& outNetwork,
                             ProgressCallback callback = nullptr);

    // Get the terrain data (for debugging)
    const TerrainData& getTerrainData() const { return terrain; }

private:
    // A* node for pathfinding
    struct PathNode {
        int x, y;               // Grid coordinates
        float gCost;            // Cost from start
        float hCost;            // Heuristic cost to end
        float fCost() const { return gCost + hCost; }
        int parentX, parentY;   // Parent node (-1 if none)
    };

    // Convert between world and grid coordinates
    glm::ivec2 worldToGrid(glm::vec2 worldPos) const;
    glm::vec2 gridToWorld(glm::ivec2 gridPos) const;

    // A* helper functions
    float calculateCost(glm::ivec2 from, glm::ivec2 to) const;
    float heuristic(glm::ivec2 from, glm::ivec2 to) const;
    bool isValidGridPos(glm::ivec2 pos) const;
    std::vector<glm::ivec2> getNeighbors(glm::ivec2 pos) const;

    // Path simplification using Douglas-Peucker algorithm
    void simplifyPath(std::vector<RoadControlPoint>& path) const;
    void douglasPeucker(const std::vector<glm::vec2>& points, float epsilon,
                        std::vector<glm::vec2>& outPoints, size_t startIdx, size_t endIdx) const;

    // Determine which settlements should be connected
    struct ConnectionCandidate {
        size_t fromIdx;
        size_t toIdx;
        float distance;
        RoadType roadType;
    };
    std::vector<ConnectionCandidate> determineConnections(const std::vector<Settlement>& settlements) const;

    PathfinderConfig config;
    TerrainData terrain;
    uint32_t gridSize = 0;
};

} // namespace RoadGen
