#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>
#include "BiomeGenerator.h"

namespace RoadGen {

// Road types with their associated widths (in meters)
// Inter-settlement road types
enum class RoadType : uint8_t {
    Footpath = 0,       // 1.5m wide - hiking trails
    Bridleway = 1,      // 3m wide - horse paths
    Lane = 2,           // 4m wide - narrow country lanes
    Road = 3,           // 6m wide - standard roads
    MainRoad = 4,       // 8m wide - major routes
    // Intra-settlement street types (for procedural cities)
    Street = 5,         // 5m wide - main settlement streets
    Alley = 6,          // 2m wide - narrow passages between buildings
    Count
};

// Get road width in meters for a given road type
inline float getRoadWidth(RoadType type) {
    switch (type) {
        case RoadType::Footpath:  return 1.5f;
        case RoadType::Bridleway: return 3.0f;
        case RoadType::Lane:      return 4.0f;
        case RoadType::Road:      return 6.0f;
        case RoadType::MainRoad:  return 8.0f;
        case RoadType::Street:    return 5.0f;
        case RoadType::Alley:     return 2.0f;
        default:                  return 4.0f;
    }
}

// Get road type name for debugging/serialization
inline const char* getRoadTypeName(RoadType type) {
    switch (type) {
        case RoadType::Footpath:  return "footpath";
        case RoadType::Bridleway: return "bridleway";
        case RoadType::Lane:      return "lane";
        case RoadType::Road:      return "road";
        case RoadType::MainRoad:  return "main_road";
        case RoadType::Street:    return "street";
        case RoadType::Alley:     return "alley";
        default:                  return "unknown";
    }
}

// Parse road type from string
inline RoadType parseRoadType(const std::string& name) {
    if (name == "footpath") return RoadType::Footpath;
    if (name == "bridleway") return RoadType::Bridleway;
    if (name == "lane") return RoadType::Lane;
    if (name == "road") return RoadType::Road;
    if (name == "main_road") return RoadType::MainRoad;
    if (name == "street") return RoadType::Street;
    if (name == "alley") return RoadType::Alley;
    return RoadType::Lane; // default
}

// A single control point along a road spline
struct RoadControlPoint {
    glm::vec2 position;     // World XZ coordinates
    float widthOverride;    // Override width (0 = use default from RoadType)

    RoadControlPoint() : position(0.0f), widthOverride(0.0f) {}
    RoadControlPoint(glm::vec2 pos) : position(pos), widthOverride(0.0f) {}
    RoadControlPoint(glm::vec2 pos, float width) : position(pos), widthOverride(width) {}
    RoadControlPoint(float x, float z) : position(x, z), widthOverride(0.0f) {}
};

// A road spline connecting two settlements
struct RoadSpline {
    std::vector<RoadControlPoint> controlPoints;
    RoadType type = RoadType::Lane;
    uint32_t fromSettlementId = 0;
    uint32_t toSettlementId = 0;

    // Get total length of the spline
    float getLength() const {
        float length = 0.0f;
        for (size_t i = 1; i < controlPoints.size(); i++) {
            length += glm::length(controlPoints[i].position - controlPoints[i-1].position);
        }
        return length;
    }

    // Get width at a control point (uses override if set, else default)
    float getWidthAt(size_t index) const {
        if (index >= controlPoints.size()) return getRoadWidth(type);
        float override = controlPoints[index].widthOverride;
        return override > 0.0f ? override : getRoadWidth(type);
    }

    // Sample position along the spline (t in range [0, totalLength])
    glm::vec2 samplePosition(float t) const {
        if (controlPoints.empty()) return glm::vec2(0.0f);
        if (controlPoints.size() == 1) return controlPoints[0].position;

        float accumulated = 0.0f;
        for (size_t i = 1; i < controlPoints.size(); i++) {
            glm::vec2 segStart = controlPoints[i-1].position;
            glm::vec2 segEnd = controlPoints[i].position;
            float segLength = glm::length(segEnd - segStart);

            if (accumulated + segLength >= t) {
                float localT = (t - accumulated) / segLength;
                return glm::mix(segStart, segEnd, localT);
            }
            accumulated += segLength;
        }
        return controlPoints.back().position;
    }

    // Sample width along the spline (interpolates between control points)
    float sampleWidth(float t) const {
        if (controlPoints.empty()) return getRoadWidth(type);
        if (controlPoints.size() == 1) return getWidthAt(0);

        float accumulated = 0.0f;
        for (size_t i = 1; i < controlPoints.size(); i++) {
            float segLength = glm::length(controlPoints[i].position - controlPoints[i-1].position);

            if (accumulated + segLength >= t) {
                float localT = (t - accumulated) / segLength;
                return glm::mix(getWidthAt(i-1), getWidthAt(i), localT);
            }
            accumulated += segLength;
        }
        return getWidthAt(controlPoints.size() - 1);
    }
};

// Collection of all roads in the network
struct RoadNetwork {
    std::vector<RoadSpline> roads;
    float terrainSize = 16384.0f;

    // Get total road length in the network
    float getTotalLength() const {
        float total = 0.0f;
        for (const auto& road : roads) {
            total += road.getLength();
        }
        return total;
    }

    // Count roads by type
    size_t countByType(RoadType type) const {
        size_t count = 0;
        for (const auto& road : roads) {
            if (road.type == type) count++;
        }
        return count;
    }
};

// Determine road type based on settlement types being connected
inline RoadType determineRoadType(SettlementType from, SettlementType to) {
    // Ensure consistent ordering (larger settlement first)
    if (static_cast<int>(to) > static_cast<int>(from)) {
        std::swap(from, to);
    }

    // Town to Town -> Main Road
    if (from == SettlementType::Town && to == SettlementType::Town) {
        return RoadType::MainRoad;
    }

    // Town to Village -> Road
    if (from == SettlementType::Town && to == SettlementType::Village) {
        return RoadType::Road;
    }

    // Town to anything else -> Lane
    if (from == SettlementType::Town) {
        return RoadType::Lane;
    }

    // Village to Village -> Lane
    if (from == SettlementType::Village && to == SettlementType::Village) {
        return RoadType::Lane;
    }

    // Village to Hamlet -> Bridleway or Lane
    if (from == SettlementType::Village && to == SettlementType::Hamlet) {
        return RoadType::Bridleway;
    }

    // Fishing villages get Lane connections
    if (from == SettlementType::FishingVillage || to == SettlementType::FishingVillage) {
        return RoadType::Lane;
    }

    // Hamlet to Hamlet -> Footpath
    if (from == SettlementType::Hamlet && to == SettlementType::Hamlet) {
        return RoadType::Footpath;
    }

    // Default to Lane
    return RoadType::Lane;
}

} // namespace RoadGen
