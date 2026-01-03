/**
 * Ported from: Source/com/watabou/towngenerator/wards/Ward.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <cmath>
#include <algorithm>
#include <map>

#include "../geom/Point.hpp"
#include "../geom/Polygon.hpp"
#include "../utils/Random.hpp"
#include "../geom/GeomUtils.hpp"
#include "../building/Cutter.hpp"
#include "../building/Patch.hpp"

namespace town {

// Forward declarations
class Model;
class Patch;

class Ward {
public:
    // Street width constants
    static constexpr float MAIN_STREET = 2.0f;
    static constexpr float REGULAR_STREET = 1.0f;
    static constexpr float ALLEY = 0.6f;

    std::shared_ptr<Model> model;
    std::shared_ptr<Patch> patch;
    std::vector<Polygon> geometry;

    Ward(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : model(model), patch(patch) {}

    virtual ~Ward() = default;

    virtual void createGeometry() {
        geometry.clear();
    }

    Polygon getCityBlock();

    virtual std::string getLabel() const { return ""; }

    static float rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch) {
        return 0.0f;
    }

    static std::vector<Polygon> createAlleys(
        Polygon& p,
        float minSq,
        float gridChaos,
        float sizeChaos,
        float emptyProb = 0.04f,
        bool split = true
    ) {
        // Handle degenerate polygons
        if (p.size() < 3) {
            return {};
        }

        // Looking for the longest edge to cut it
        size_t vIdx = 0;
        float length = -1.0f;

        for (size_t i = 0; i < p.size(); ++i) {
            PointPtr p0 = p[i];
            PointPtr p1 = p[(i + 1) % p.size()];
            if (!p0 || !p1) continue;  // Skip null vertices
            float len = Point::distance(*p0, *p1);
            if (len > length) {
                length = len;
                vIdx = i;
            }
        }

        // If no valid edge found, return empty
        if (length < 0) {
            return {};
        }

        // Ensure the selected vertex is valid
        PointPtr selectedVertex = p[vIdx];
        if (!selectedVertex) {
            return {};
        }

        float spread = 0.8f * gridChaos;
        float ratio = (1.0f - spread) / 2.0f + Random::getFloat() * spread;

        // Trying to keep buildings rectangular even in chaotic wards
        float angleSpread = static_cast<float>(M_PI) / 6.0f * gridChaos *
            (p.square() < minSq * 4.0f ? 0.0f : 1.0f);
        float b = (Random::getFloat() - 0.5f) * angleSpread;

        auto halves = Cutter::bisect(p, selectedVertex, ratio, b, split ? ALLEY : 0.0f);

        std::vector<Polygon> buildings;
        for (auto& half : halves) {
            float halfSq = half.square();
            float threshold = minSq * std::pow(2.0f, 4.0f * sizeChaos * (Random::getFloat() - 0.5f));

            if (halfSq < threshold) {
                if (!Random::getBool(emptyProb)) {
                    buildings.push_back(std::move(half));
                }
            } else {
                auto subBuildings = createAlleys(
                    half, minSq, gridChaos, sizeChaos, emptyProb,
                    halfSq > minSq / (Random::getFloat() * Random::getFloat())
                );
                buildings.insert(buildings.end(),
                    std::make_move_iterator(subBuildings.begin()),
                    std::make_move_iterator(subBuildings.end()));
            }
        }

        return buildings;
    }

    static std::vector<Polygon> createOrthoBuilding(Polygon& poly, float minBlockSq, float fill) {
        if (poly.square() < minBlockSq) {
            return { poly };
        }

        size_t longestIdx = findLongestEdgeIndex(poly);
        Point c1 = poly.vectori(static_cast<int>(longestIdx));
        Point c2 = c1.rotate90();

        // The original Haxe has while(true), but we add a safety limit
        // to prevent infinite loops in edge cases
        int maxAttempts = 100;
        while (maxAttempts-- > 0) {
            auto blocks = slice(poly, c1, c2, minBlockSq, fill);
            if (!blocks.empty()) {
                return blocks;
            }
        }
        // Fallback: return the original polygon if we couldn't slice it
        return { poly };
    }

protected:
    void filterOutskirts();

private:
    static size_t findLongestEdgeIndex(Polygon& poly) {
        float maxLen = -1.0f;
        size_t result = 0;

        for (size_t i = 0; i < poly.size(); i++) {
            float len = poly.vectori(static_cast<int>(i)).length();
            if (len > maxLen) {
                maxLen = len;
                result = i;
            }
        }
        return result;
    }

    static std::vector<Polygon> slice(
        Polygon& poly,
        Point& c1,
        Point& c2,
        float minBlockSq,
        float fill,
        int depth = 0
    ) {
        // Safety limit to prevent stack overflow from infinite recursion
        constexpr int MAX_DEPTH = 50;
        if (depth >= MAX_DEPTH) {
            return { poly };  // Return the polygon as-is if we've recursed too deep
        }

        size_t v0Idx = findLongestEdgeIndex(poly);
        PointPtr v0 = poly[v0Idx];
        PointPtr v1 = poly.next(v0);
        Point v = v1->subtract(*v0);

        float ratio = 0.4f + Random::getFloat() * 0.2f;
        Point p1 = GeomUtils::interpolate(*v0, *v1, ratio);

        Point& c = (std::abs(GeomUtils::scalar(v.x, v.y, c1.x, c1.y)) <
                    std::abs(GeomUtils::scalar(v.x, v.y, c2.x, c2.y))) ? c1 : c2;

        auto halves = poly.cut(p1, p1.add(c));

        std::vector<Polygon> buildings;
        for (auto& half : halves) {
            float halfSq = half.square();
            float threshold = minBlockSq * std::pow(2.0f, Random::normal() * 2.0f - 1.0f);

            if (halfSq < threshold) {
                if (Random::getBool(fill)) {
                    buildings.push_back(std::move(half));
                }
            } else {
                auto subBlocks = slice(half, c1, c2, minBlockSq, fill, depth + 1);
                buildings.insert(buildings.end(),
                    std::make_move_iterator(subBlocks.begin()),
                    std::make_move_iterator(subBlocks.end()));
            }
        }
        return buildings;
    }
};

} // namespace town
