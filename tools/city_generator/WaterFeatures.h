// Water Features: Rivers, ponds, piers, and bridges
// Additional features for coastal and river cities
//
// Semantic rules:
// - Rivers can flow through the city, affecting ward placement
// - Ponds/lakes occupy patches and prevent buildings
// - Piers extend from the coast for harbor access
// - Bridges span rivers at street crossings

#pragma once

#include "Geometry.h"
#include "Patch.h"
#include <vector>
#include <random>
#include <cmath>

namespace city {

// River segment with variable width
struct RiverSegment {
    Vec2 start;
    Vec2 end;
    float startWidth = 3.0f;
    float endWidth = 4.0f;

    // Get polygon representation of the river segment
    Polygon toPolygon() const {
        Vec2 dir = (end - start).normalized();
        Vec2 perp = dir.perpendicular();

        return Polygon({
            start + perp * (startWidth / 2),
            start - perp * (startWidth / 2),
            end - perp * (endWidth / 2),
            end + perp * (endWidth / 2)
        });
    }
};

// River flowing through or around the city
struct River {
    std::vector<Vec2> path;          // Center line of river
    std::vector<float> widths;       // Width at each point
    std::string name;

    // Get polygon segments for the river
    std::vector<Polygon> getSegments() const {
        std::vector<Polygon> segments;
        if (path.size() < 2) return segments;

        for (size_t i = 0; i < path.size() - 1; i++) {
            RiverSegment seg;
            seg.start = path[i];
            seg.end = path[i + 1];
            seg.startWidth = widths.empty() ? 4.0f : widths[std::min(i, widths.size()-1)];
            seg.endWidth = widths.empty() ? 4.0f : widths[std::min(i+1, widths.size()-1)];
            segments.push_back(seg.toPolygon());
        }

        return segments;
    }

    // Get the river as a single merged polygon (simplified)
    Polygon getMergedShape() const {
        if (path.size() < 2) return Polygon();

        // Build left and right banks
        std::vector<Vec2> leftBank, rightBank;

        for (size_t i = 0; i < path.size(); i++) {
            Vec2 dir;
            if (i == 0) {
                dir = (path[1] - path[0]).normalized();
            } else if (i == path.size() - 1) {
                dir = (path[i] - path[i-1]).normalized();
            } else {
                dir = (path[i+1] - path[i-1]).normalized();
            }

            Vec2 perp = dir.perpendicular();
            float w = widths.empty() ? 4.0f : widths[std::min(i, widths.size()-1)];

            leftBank.push_back(path[i] + perp * (w / 2));
            rightBank.push_back(path[i] - perp * (w / 2));
        }

        // Combine into polygon (left bank forward, right bank reverse)
        std::vector<Vec2> verts = leftBank;
        for (auto it = rightBank.rbegin(); it != rightBank.rend(); ++it) {
            verts.push_back(*it);
        }

        return Polygon(verts);
    }
};

// Pond or lake within the city
struct Pond {
    Polygon shape;
    std::string name;
    bool isNatural = true;  // Natural pond vs man-made fountain/basin
};

// Pier extending into water
struct Pier {
    Vec2 start;              // Land end
    Vec2 end;                // Water end
    float width = 2.0f;

    Polygon toPolygon() const {
        Vec2 dir = (end - start).normalized();
        Vec2 perp = dir.perpendicular();

        return Polygon({
            start + perp * (width / 2),
            start - perp * (width / 2),
            end - perp * (width / 2),
            end + perp * (width / 2)
        });
    }
};

// Bridge over water
struct Bridge {
    Vec2 start;
    Vec2 end;
    float width = 3.0f;
    bool isArched = true;    // Arched vs flat bridge

    Polygon toPolygon() const {
        Vec2 dir = (end - start).normalized();
        Vec2 perp = dir.perpendicular();

        return Polygon({
            start + perp * (width / 2),
            start - perp * (width / 2),
            end - perp * (width / 2),
            end + perp * (width / 2)
        });
    }
};

// Water feature configuration
struct WaterConfig {
    bool hasRiver = false;           // City has a river
    bool hasCoast = false;           // City is coastal
    bool hasPonds = false;           // City has ponds/fountains
    int numPiers = 0;                // Number of piers
    float riverWidth = 5.0f;         // Base river width
    float coastDirection = 0.0f;     // Angle to coast (radians)
};

// Water feature generator
class WaterFeatures {
public:
    std::vector<River> rivers;
    std::vector<Pond> ponds;
    std::vector<Pier> piers;
    std::vector<Bridge> bridges;

    // Generate water features for a city
    void generate(const WaterConfig& config, float cityRadius,
                  const std::vector<Patch*>& patches,
                  std::mt19937& rng);

    // Check if a point is in water
    bool isInWater(const Vec2& point) const;

    // Get patches that contain water
    std::vector<Patch*> getWaterPatches(const std::vector<Patch*>& patches) const;

private:
    void generateRiver(const WaterConfig& config, float cityRadius, std::mt19937& rng);
    void generateCoast(const WaterConfig& config, float cityRadius, std::mt19937& rng);
    void generatePonds(const WaterConfig& config, const std::vector<Patch*>& patches, std::mt19937& rng);
    void generatePiers(const WaterConfig& config, float cityRadius, std::mt19937& rng);
    void findBridgeLocations(const std::vector<Patch*>& patches);
};

inline void WaterFeatures::generate(const WaterConfig& config, float cityRadius,
                                     const std::vector<Patch*>& patches,
                                     std::mt19937& rng) {
    if (config.hasRiver) {
        generateRiver(config, cityRadius, rng);
    }

    if (config.hasCoast) {
        generateCoast(config, cityRadius, rng);
    }

    if (config.hasPonds) {
        generatePonds(config, patches, rng);
    }

    if (config.numPiers > 0) {
        generatePiers(config, cityRadius, rng);
    }

    // Find bridge locations where streets cross rivers
    findBridgeLocations(patches);
}

inline void WaterFeatures::generateRiver(const WaterConfig& config, float cityRadius,
                                          std::mt19937& rng) {
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159265f);
    std::uniform_real_distribution<float> offsetDist(-0.3f, 0.3f);

    River river;
    river.name = "River";

    // River enters from one side and exits the other
    float entryAngle = angleDist(rng);
    float exitAngle = entryAngle + 3.14159265f + offsetDist(rng);

    Vec2 entry{std::cos(entryAngle) * cityRadius * 1.2f,
               std::sin(entryAngle) * cityRadius * 1.2f};
    Vec2 exit{std::cos(exitAngle) * cityRadius * 1.2f,
              std::sin(exitAngle) * cityRadius * 1.2f};

    // Create meandering path
    int numPoints = 8;
    river.path.push_back(entry);
    river.widths.push_back(config.riverWidth * 1.2f);

    for (int i = 1; i < numPoints - 1; i++) {
        float t = static_cast<float>(i) / (numPoints - 1);
        Vec2 base = Vec2::lerp(entry, exit, t);

        // Add meander
        Vec2 perp = (exit - entry).perpendicular().normalized();
        float meander = std::sin(t * 3.14159265f * 2) * cityRadius * 0.2f;
        meander += offsetDist(rng) * cityRadius * 0.1f;

        river.path.push_back(base + perp * meander);
        river.widths.push_back(config.riverWidth * (0.8f + 0.4f * t));
    }

    river.path.push_back(exit);
    river.widths.push_back(config.riverWidth * 1.5f);

    rivers.push_back(river);
}

inline void WaterFeatures::generateCoast(const WaterConfig& config, float cityRadius,
                                          std::mt19937& rng) {
    // Coast is represented as a large "river" that defines the sea edge
    River coast;
    coast.name = "Coast";

    float angle = config.coastDirection;
    Vec2 coastDir{std::cos(angle), std::sin(angle)};
    Vec2 coastPerp = coastDir.perpendicular();

    // Coast line passes through edge of city
    Vec2 coastCenter = coastDir * cityRadius * 0.9f;

    // Create coast line
    float coastLength = cityRadius * 3.0f;
    Vec2 start = coastCenter - coastPerp * coastLength;
    Vec2 end = coastCenter + coastPerp * coastLength;

    int numPoints = 12;
    std::uniform_real_distribution<float> waveDist(-5.0f, 5.0f);

    for (int i = 0; i < numPoints; i++) {
        float t = static_cast<float>(i) / (numPoints - 1);
        Vec2 base = Vec2::lerp(start, end, t);

        // Add wave variation
        float wave = std::sin(t * 3.14159265f * 4) * 3.0f + waveDist(rng);
        coast.path.push_back(base + coastDir * wave);
        coast.widths.push_back(cityRadius * 2.0f);  // Very wide for sea
    }

    rivers.push_back(coast);
}

inline void WaterFeatures::generatePonds(const WaterConfig& config,
                                          const std::vector<Patch*>& patches,
                                          std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // Small chance for each patch to have a pond
    for (auto* patch : patches) {
        if (!patch->withinCity) continue;

        if (dist(rng) < 0.05f) {  // 5% chance
            Pond pond;
            pond.name = "Pond";
            pond.isNatural = dist(rng) < 0.5f;

            // Create small irregular polygon in patch center
            Vec2 center = patch->shape.centroid();
            float size = std::sqrt(patch->area()) * 0.2f;

            int sides = pond.isNatural ? 8 : 6;
            std::vector<Vec2> verts;
            for (int i = 0; i < sides; i++) {
                float angle = 2.0f * 3.14159265f * i / sides;
                float r = size * (0.7f + 0.3f * dist(rng));
                verts.push_back(center + Vec2{std::cos(angle), std::sin(angle)} * r);
            }
            pond.shape = Polygon(verts);

            ponds.push_back(pond);
        }
    }
}

inline void WaterFeatures::generatePiers(const WaterConfig& config, float cityRadius,
                                          std::mt19937& rng) {
    if (!config.hasCoast || rivers.empty()) return;

    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // Find coast line
    const River* coast = nullptr;
    for (const auto& r : rivers) {
        if (r.name == "Coast") {
            coast = &r;
            break;
        }
    }

    if (!coast || coast->path.size() < 2) return;

    // Generate piers along the coast
    for (int i = 0; i < config.numPiers; i++) {
        float t = (i + 0.5f) / config.numPiers;
        size_t idx = static_cast<size_t>(t * (coast->path.size() - 1));
        idx = std::min(idx, coast->path.size() - 2);

        Vec2 coastPoint = Vec2::lerp(coast->path[idx], coast->path[idx + 1], t);

        // Direction into water
        float angle = config.coastDirection;
        Vec2 waterDir{std::cos(angle), std::sin(angle)};

        Pier pier;
        pier.start = coastPoint - waterDir * 2.0f;  // Start on land
        pier.end = coastPoint + waterDir * (8.0f + dist(rng) * 6.0f);  // Into water
        pier.width = 1.5f + dist(rng);

        piers.push_back(pier);
    }
}

inline void WaterFeatures::findBridgeLocations(const std::vector<Patch*>& patches) {
    // Find where patch edges cross rivers
    for (const auto& river : rivers) {
        if (river.name == "Coast") continue;  // Don't bridge the coast

        Polygon riverShape = river.getMergedShape();
        if (riverShape.empty()) continue;

        for (auto* patch : patches) {
            for (size_t i = 0; i < patch->shape.size(); i++) {
                size_t j = (i + 1) % patch->shape.size();
                Vec2 v1 = patch->shape[i];
                Vec2 v2 = patch->shape[j];

                // Check if edge crosses river
                bool v1InRiver = riverShape.contains(v1);
                bool v2InRiver = riverShape.contains(v2);

                if (v1InRiver != v2InRiver) {
                    // Edge crosses river - potential bridge location
                    Bridge bridge;
                    bridge.start = v1;
                    bridge.end = v2;
                    bridge.width = 2.5f;

                    // Check if bridge already exists nearby
                    bool exists = false;
                    for (const auto& existing : bridges) {
                        if (Vec2::distance(existing.start, bridge.start) < 5.0f ||
                            Vec2::distance(existing.end, bridge.start) < 5.0f) {
                            exists = true;
                            break;
                        }
                    }

                    if (!exists) {
                        bridges.push_back(bridge);
                    }
                }
            }
        }
    }
}

inline bool WaterFeatures::isInWater(const Vec2& point) const {
    for (const auto& river : rivers) {
        Polygon shape = river.getMergedShape();
        if (shape.contains(point)) return true;
    }

    for (const auto& pond : ponds) {
        if (pond.shape.contains(point)) return true;
    }

    return false;
}

inline std::vector<Patch*> WaterFeatures::getWaterPatches(
    const std::vector<Patch*>& patches) const {

    std::vector<Patch*> waterPatches;

    for (auto* patch : patches) {
        Vec2 center = patch->shape.centroid();
        if (isInWater(center)) {
            waterPatches.push_back(patch);
        }
    }

    return waterPatches;
}

} // namespace city
