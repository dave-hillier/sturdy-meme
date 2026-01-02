// CurtainWall: City fortification walls with gates and towers
// Ported from watabou's Medieval Fantasy City Generator
//
// Semantic rules:
// - Wall shape is computed from patches that are "within walls"
// - Gates are placed at vertices that border multiple inner districts
// - Gates maintain minimum distance from each other
// - Towers are placed at wall vertices that aren't gates
// - Wall segments can be disabled (gaps in the wall)

#pragma once

#include "Geometry.h"
#include "Patch.h"
#include <vector>
#include <algorithm>
#include <random>

namespace city {

// Forward declaration
class Model;

class CurtainWall {
public:
    Polygon shape;                    // Wall perimeter
    std::vector<bool> segments;       // Which wall segments are active
    std::vector<Vec2> gates;          // Gate positions
    std::vector<Vec2> towers;         // Tower positions

    // Build wall around given patches
    // smooth: number of smoothing iterations
    void build(const std::vector<Patch*>& innerPatches,
               const std::vector<Patch*>& allPatches,
               int smooth = 2);

    // Place gates at suitable wall vertices
    // minGateDistance: minimum distance between gates
    void buildGates(const std::vector<Patch*>& innerPatches,
                    float minGateDistance,
                    std::mt19937& rng);

    // Place towers at wall vertices (excluding gates)
    void buildTowers();

    // Get wall radius (max distance from center)
    float getRadius() const;

    // Check if wall borders a patch
    bool borders(const Patch& patch) const;

    // Check if a point is inside the wall
    bool contains(const Vec2& p) const {
        return shape.contains(p);
    }

private:
    // Find wall vertices that could be gates
    // (vertices adjacent to multiple inner patches)
    std::vector<size_t> findPotentialGateIndices(
        const std::vector<Patch*>& innerPatches) const;
};

// Implementation
inline void CurtainWall::build(const std::vector<Patch*>& innerPatches,
                               const std::vector<Patch*>& allPatches,
                               int smooth) {
    if (innerPatches.empty()) return;

    // Collect all vertices from inner patches
    std::vector<Vec2> wallVertices;

    for (const auto* patch : innerPatches) {
        for (size_t i = 0; i < patch->shape.size(); i++) {
            const Vec2& v = patch->shape[i];

            // Check if this vertex is on the outer edge
            // (not shared by another inner patch on the opposite side)
            bool isOuter = false;

            // Count how many inner patches share this vertex
            int innerCount = 0;
            int outerCount = 0;

            for (const auto* other : allPatches) {
                for (const auto& ov : other->shape.vertices) {
                    if (v == ov) {
                        if (std::find(innerPatches.begin(), innerPatches.end(), other)
                            != innerPatches.end()) {
                            innerCount++;
                        } else {
                            outerCount++;
                        }
                        break;
                    }
                }
            }

            // It's an outer vertex if it touches at least one non-inner patch
            if (outerCount > 0) {
                // Check if already in list
                bool found = false;
                for (const auto& wv : wallVertices) {
                    if (wv == v) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    wallVertices.push_back(v);
                }
            }
        }
    }

    if (wallVertices.size() < 3) return;

    // Sort vertices by angle from center to form proper polygon
    Vec2 center{0, 0};
    for (const auto& v : wallVertices) center += v;
    center /= static_cast<float>(wallVertices.size());

    std::sort(wallVertices.begin(), wallVertices.end(),
        [&center](const Vec2& a, const Vec2& b) {
            float angleA = std::atan2(a.y - center.y, a.x - center.x);
            float angleB = std::atan2(b.y - center.y, b.x - center.x);
            return angleA < angleB;
        });

    shape = Polygon(wallVertices);

    // Smooth the wall shape
    for (int i = 0; i < smooth; i++) {
        shape.smoothVertices(0.3f);
    }

    // Initialize all segments as active
    segments.resize(shape.size(), true);
}

inline void CurtainWall::buildGates(const std::vector<Patch*>& innerPatches,
                                    float minGateDistance,
                                    std::mt19937& rng) {
    gates.clear();

    auto potentialGates = findPotentialGateIndices(innerPatches);

    if (potentialGates.empty()) {
        // No good gate positions, pick random vertices
        if (shape.size() >= 4) {
            std::uniform_int_distribution<size_t> dist(0, shape.size() - 1);
            for (int i = 0; i < 4; i++) {
                size_t idx = dist(rng);
                gates.push_back(shape[idx]);
            }
        }
        return;
    }

    // Shuffle potential gates
    std::shuffle(potentialGates.begin(), potentialGates.end(), rng);

    // Greedily select gates maintaining minimum distance
    for (size_t idx : potentialGates) {
        const Vec2& candidate = shape[idx];

        bool tooClose = false;
        for (const auto& existing : gates) {
            if (Vec2::distance(candidate, existing) < minGateDistance) {
                tooClose = true;
                break;
            }
        }

        if (!tooClose) {
            gates.push_back(candidate);
        }
    }
}

inline void CurtainWall::buildTowers() {
    towers.clear();

    for (size_t i = 0; i < shape.size(); i++) {
        const Vec2& v = shape[i];

        // Skip if this is a gate
        bool isGate = false;
        for (const auto& g : gates) {
            if (v == g) {
                isGate = true;
                break;
            }
        }

        if (!isGate && segments[i]) {
            towers.push_back(v);
        }
    }
}

inline float CurtainWall::getRadius() const {
    float maxDist = 0.0f;
    Vec2 center = shape.centroid();
    for (const auto& v : shape.vertices) {
        maxDist = std::max(maxDist, Vec2::distance(v, center));
    }
    return maxDist;
}

inline bool CurtainWall::borders(const Patch& patch) const {
    // Check if any wall vertex is also a patch vertex
    for (const auto& wv : shape.vertices) {
        for (const auto& pv : patch.shape.vertices) {
            if (wv == pv) return true;
        }
    }
    return false;
}

inline std::vector<size_t> CurtainWall::findPotentialGateIndices(
    const std::vector<Patch*>& innerPatches) const {

    std::vector<size_t> result;

    for (size_t i = 0; i < shape.size(); i++) {
        const Vec2& v = shape[i];

        // Count how many inner patches share this vertex
        int patchCount = 0;
        for (const auto* patch : innerPatches) {
            for (const auto& pv : patch->shape.vertices) {
                if (v == pv) {
                    patchCount++;
                    break;
                }
            }
        }

        // Good gate position if shared by 2+ inner patches
        // (indicates a junction point)
        if (patchCount >= 2) {
            result.push_back(i);
        }
    }

    return result;
}

} // namespace city
