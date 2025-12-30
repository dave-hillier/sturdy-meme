#pragma once

// Debug visualization for roads and rivers using cones
// - Rivers: directional cones pointing downstream
// - Roads: bidirectional cones (pair pointing in opposite directions)

#include <glm/glm.hpp>

class DebugLineSystem;
class TerrainHeightMap;
struct WaterPlacementData;
struct RoadNetwork;

struct RoadRiverVisConfig {
    // Visualization toggles
    bool showRivers = true;
    bool showRoads = true;

    // Cone appearance
    float coneRadius = 0.5f;        // Radius of cone base
    float coneLength = 2.0f;        // Length from base to tip
    float heightAboveGround = 1.0f; // Height offset above terrain

    // Spacing between cones along path
    float riverConeSpacing = 50.0f;
    float roadConeSpacing = 50.0f;

    // Colors (RGBA)
    glm::vec4 riverColor = glm::vec4(0.2f, 0.5f, 1.0f, 1.0f);  // Blue
    glm::vec4 roadColor = glm::vec4(0.8f, 0.6f, 0.2f, 1.0f);   // Orange/tan
};

class RoadRiverVisualization {
public:
    RoadRiverVisualization() = default;
    ~RoadRiverVisualization() = default;

    // Configure visualization parameters
    void setConfig(const RoadRiverVisConfig& config) { config_ = config; }
    RoadRiverVisConfig& getConfig() { return config_; }
    const RoadRiverVisConfig& getConfig() const { return config_; }

    // Set data sources
    void setWaterData(const WaterPlacementData* waterData) { waterData_ = waterData; }
    void setRoadNetwork(const RoadNetwork* roadNetwork) { roadNetwork_ = roadNetwork; }
    void setTerrainHeightMap(const TerrainHeightMap* heightMap) { heightMap_ = heightMap; }

    // Add visualization to debug line system
    // Call this each frame before DebugLineSystem::uploadLines()
    void addToDebugLines(DebugLineSystem& debugLines);

private:
    void addRiverVisualization(DebugLineSystem& debugLines);
    void addRoadVisualization(DebugLineSystem& debugLines);

    // Get height at world position from terrain height map
    float getTerrainHeight(float x, float z) const;

    RoadRiverVisConfig config_;
    const WaterPlacementData* waterData_ = nullptr;
    const RoadNetwork* roadNetwork_ = nullptr;
    const TerrainHeightMap* heightMap_ = nullptr;
};
