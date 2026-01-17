#pragma once

// Debug visualization for roads and rivers using cones
// - Rivers: directional cones pointing downstream
// - Roads: bidirectional cones (pair pointing in opposite directions)

#include <glm/glm.hpp>
#include <vector>
#include <optional>
#include <functional>

class DebugLineSystem;
class TerrainTileCache;
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

    // Configure visualization parameters (marks cache dirty)
    void setConfig(const RoadRiverVisConfig& config) { config_ = config; dirty_ = true; }
    RoadRiverVisConfig& getConfig() { return config_; }
    const RoadRiverVisConfig& getConfig() const { return config_; }

    // Set data sources (marks cache dirty)
    void setWaterData(const WaterPlacementData* waterData) {
        if (waterData) waterData_.emplace(*waterData);
        else waterData_.reset();
        dirty_ = true;
    }
    void setRoadNetwork(const RoadNetwork* roadNetwork) {
        if (roadNetwork) roadNetwork_.emplace(*roadNetwork);
        else roadNetwork_.reset();
        dirty_ = true;
    }
    void setTerrainTileCache(const TerrainTileCache* tileCache) {
        if (tileCache) tileCache_.emplace(*tileCache);
        else tileCache_.reset();
        dirty_ = true;
    }

    // Force rebuild of cached geometry
    void invalidateCache() { dirty_ = true; }

    // Add visualization to debug line system
    // Uses cached geometry, only rebuilds when dirty
    void addToDebugLines(DebugLineSystem& debugLines);

    // Statistics
    size_t getCachedLineVertexCount() const { return cachedLineVertices_.size(); }
    size_t getEstimatedConeCount() const { return cachedLineVertices_.size() / 32; } // 16 lines * 2 verts = 32 verts/cone

private:
    // Cached vertex (matches DebugLineSystem format)
    struct CachedVertex {
        glm::vec3 position;
        glm::vec4 color;
    };

    void rebuildCache();
    void buildRiverCones();
    void buildRoadCones();
    void addConeToCache(const glm::vec3& base, const glm::vec3& tip, float radius, const glm::vec4& color);

    // Get height at world position from terrain height map
    float getTerrainHeight(float x, float z) const;

    RoadRiverVisConfig config_;
    std::optional<std::reference_wrapper<const WaterPlacementData>> waterData_;
    std::optional<std::reference_wrapper<const RoadNetwork>> roadNetwork_;
    std::optional<std::reference_wrapper<const TerrainTileCache>> tileCache_;

    // Cached line vertices (pairs for each line segment)
    std::vector<CachedVertex> cachedLineVertices_;
    bool dirty_ = true;
};
