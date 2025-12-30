#pragma once

#include <vector>
#include <cstdint>
#include <functional>
#include <string>
#include <glm/glm.hpp>

namespace RoadGen {

// A node in the road network graph
struct NetworkNode {
    uint32_t id;
    glm::vec2 position;
    bool isSettlement;          // True if this is a settlement, false if junction
    uint32_t settlementId;      // Valid only if isSettlement
    std::vector<uint32_t> connections;  // Connected node IDs
};

// An edge in the road network
struct NetworkEdge {
    uint32_t fromNode;
    uint32_t toNode;
    float length;
    int depth;  // Depth in tree (0 = main trunk, higher = branches)
};

// Result of space colonization
struct ColonizationResult {
    std::vector<NetworkNode> nodes;
    std::vector<NetworkEdge> edges;

    // Find node by settlement ID
    const NetworkNode* findSettlementNode(uint32_t settlementId) const {
        for (const auto& node : nodes) {
            if (node.isSettlement && node.settlementId == settlementId) {
                return &node;
            }
        }
        return nullptr;
    }
};

// Configuration for space colonization
struct ColonizationConfig {
    float attractionRadius = 5000.0f;   // Max distance to influence growth
    float killRadius = 100.0f;          // Distance at which attraction point is "reached"
    float branchLength = 200.0f;        // Length of each growth step
    float branchAngle = 0.5f;           // Max angle deviation per step (radians)
    int maxIterations = 1000;           // Safety limit
    float minBranchLength = 50.0f;      // Minimum branch before splitting
};

class SpaceColonization {
public:
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;

    SpaceColonization() = default;

    // Run space colonization to build network topology
    // attractionPoints: positions of all settlements to connect
    // rootPoints: starting points (typically towns)
    // settlementIds: parallel array of settlement IDs for each attraction point
    bool buildNetwork(
        const std::vector<glm::vec2>& attractionPoints,
        const std::vector<glm::vec2>& rootPoints,
        const std::vector<uint32_t>& settlementIds,
        const std::vector<uint32_t>& rootSettlementIds,
        const ColonizationConfig& config,
        ColonizationResult& outResult,
        ProgressCallback callback = nullptr
    );

private:
    struct GrowthNode {
        uint32_t nodeId;
        glm::vec2 position;
        glm::vec2 growthDirection;
        int depth;
        bool active;
    };

    struct AttractionPoint {
        glm::vec2 position;
        uint32_t settlementId;
        bool reached;
    };

    // Find the closest active growth node to a position
    GrowthNode* findClosestGrowthNode(
        std::vector<GrowthNode>& growthNodes,
        const glm::vec2& target,
        float maxDist
    );

    // Calculate growth direction based on nearby attraction points
    glm::vec2 calculateGrowthDirection(
        const GrowthNode& node,
        const std::vector<AttractionPoint>& attractions,
        float attractionRadius
    );
};

} // namespace RoadGen
