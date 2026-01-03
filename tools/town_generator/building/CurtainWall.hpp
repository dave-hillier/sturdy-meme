/**
 * Ported from: Source/com/watabou/towngenerator/building/CurtainWall.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>

#include "../geom/Point.hpp"
#include "../geom/Polygon.hpp"
#include "../utils/Random.hpp"
#include "../building/Patch.hpp"

namespace town {

// Forward declaration
class Model;

/**
 * CurtainWall class - represents a defensive wall around a set of patches.
 * Manages wall shape, gates, towers, and wall segments.
 */
class CurtainWall {
public:
    Polygon shape;
    std::vector<bool> segments;
    std::vector<Point> gates;
    std::vector<Point> towers;

    /**
     * Constructs a CurtainWall around a set of patches.
     * @param real Whether this is a real wall (affects smoothing)
     * @param model The town model
     * @param patches The patches enclosed by this wall
     * @param reserved Points that should not be modified
     */
    CurtainWall(bool real, std::shared_ptr<Model> model,
                const std::vector<std::shared_ptr<Patch>>& patches,
                const Polygon& reserved);

    /**
     * Builds towers at wall vertices (except gates).
     */
    void buildTowers() {
        towers.clear();
        if (real_) {
            size_t len = shape.size();
            for (size_t i = 0; i < len; ++i) {
                Point t = shape[i];
                bool isGate = std::find_if(gates.begin(), gates.end(),
                    [&t](const Point& g) { return g == t; }) != gates.end();
                if (!isGate && (segments[(i + len - 1) % len] || segments[i])) {
                    towers.push_back(t);
                }
            }
        }
    }

    /**
     * Returns the maximum distance from origin to any wall vertex.
     */
    float getRadius() const {
        float radius = 0.0f;
        for (size_t i = 0; i < shape.size(); ++i) {
            const Point& v = shape[i];
            radius = std::max(radius, v.length());
        }
        return radius;
    }

    /**
     * Checks if a specific edge of a patch borders this wall.
     * @param p The patch to check
     * @param v0 Start vertex of the edge
     * @param v1 End vertex of the edge
     * @return True if the edge borders the wall
     */
    bool bordersBy(std::shared_ptr<Patch> p, const Point& v0, const Point& v1) const {
        bool patchInside = containsPatch(p);
        int index;
        if (patchInside) {
            index = shape.findEdge(v0, v1);
        } else {
            index = shape.findEdge(v1, v0);
        }

        if (index != -1 && segments[static_cast<size_t>(index)]) {
            return true;
        }

        return false;
    }

    /**
     * Checks if any edge of a patch borders this wall.
     * @param p The patch to check
     * @return True if the patch borders the wall
     */
    bool borders(std::shared_ptr<Patch> p) const {
        bool withinWalls = containsPatch(p);
        size_t length = shape.size();

        for (size_t i = 0; i < length; ++i) {
            if (segments[i]) {
                const Point& v0 = shape[i];
                const Point& v1 = shape[(i + 1) % length];
                int index;
                if (withinWalls) {
                    index = p->shape.findEdge(v0, v1);
                } else {
                    index = p->shape.findEdge(v1, v0);
                }
                if (index != -1) {
                    return true;
                }
            }
        }

        return false;
    }

private:
    bool real_;
    std::vector<std::shared_ptr<Patch>> patches_;

    /**
     * Checks if a patch is contained within this wall's patches.
     */
    bool containsPatch(std::shared_ptr<Patch> p) const {
        for (const auto& patch : patches_) {
            if (patch == p) {
                return true;
            }
        }
        return false;
    }
};

} // namespace town
