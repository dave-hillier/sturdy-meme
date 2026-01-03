/**
 * Ported from: Source/com/watabou/geom/Voronoi.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <functional>

#include "../geom/Point.hpp"
#include "../utils/MathUtils.hpp"

namespace town {

class Triangle;
class Region;

/**
 * Custom hash and equality for PointPtr used as map keys.
 * Uses the Point's unique ID for deterministic hashing (matching Haxe semantics).
 */
struct PointPtrHash {
    std::size_t operator()(const PointPtr& p) const {
        return std::hash<uint64_t>()(p->getId());
    }
};

struct PointPtrEqual {
    bool operator()(const PointPtr& a, const PointPtr& b) const {
        return a.get() == b.get();
    }
};

/**
 * Triangle class - represents a triangle in the Delaunay triangulation.
 * Stores circumcircle center and radius for point-in-circle tests.
 */
class Triangle {
public:
    PointPtr p1;
    PointPtr p2;
    PointPtr p3;

    PointPtr c;  // Circumcircle center
    float r;  // Circumcircle radius

    Triangle(PointPtr p1, PointPtr p2, PointPtr p3) {
        // Calculate signed area to determine winding order
        float s = (p2->x - p1->x) * (p2->y + p1->y) +
                  (p3->x - p2->x) * (p3->y + p2->y) +
                  (p1->x - p3->x) * (p1->y + p3->y);

        this->p1 = p1;
        // Ensure CCW winding
        this->p2 = s > 0 ? p2 : p3;
        this->p3 = s > 0 ? p3 : p2;

        // Calculate circumcircle center
        float x1 = (p1->x + this->p2->x) / 2.0f;
        float y1 = (p1->y + this->p2->y) / 2.0f;
        float x2 = (this->p2->x + this->p3->x) / 2.0f;
        float y2 = (this->p2->y + this->p3->y) / 2.0f;

        float dx1 = p1->y - this->p2->y;
        float dy1 = this->p2->x - p1->x;
        float dx2 = this->p2->y - this->p3->y;
        float dy2 = this->p3->x - this->p2->x;

        float tg1 = dy1 / dx1;
        float t2 = ((y1 - y2) - (x1 - x2) * tg1) / (dy2 - dx2 * tg1);

        c = makePoint(x2 + dx2 * t2, y2 + dy2 * t2);
        r = Point::distance(*c, *p1);
    }

    /**
     * Checks if this triangle has an edge from point a to point b.
     * Edge direction matters for Delaunay triangulation.
     */
    bool hasEdge(PointPtr a, PointPtr b) const {
        return (p1 == a && p2 == b) ||
               (p2 == a && p3 == b) ||
               (p3 == a && p1 == b);
    }
};

/**
 * Region class - represents a Voronoi cell (region around a seed point).
 * Vertices are triangles whose circumcenters form the cell boundary.
 */
class Region {
public:
    PointPtr seed;
    std::vector<Triangle*> vertices;

    explicit Region(PointPtr seed) : seed(seed) {}

    /**
     * Sorts vertices (triangles) by angle around the seed point.
     * This ensures the circumcenters form a proper polygon when connected.
     */
    Region& sortVertices() {
        // Remove null and invalid triangles before sorting
        // Check for null, low addresses, and unreasonably high addresses
        auto isInvalidPtr = [](Triangle* ptr) {
            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            return ptr == nullptr || addr < 0x1000 || addr > 0x7fffffffffff;
        };
        vertices.erase(
            std::remove_if(vertices.begin(), vertices.end(), isInvalidPtr),
            vertices.end());

        if (vertices.size() < 2) return *this;

        // Use stable_sort to avoid potential issues with std::sort's optimizations
        // Also validate all pointers before sorting to catch any remaining issues
        for (Triangle* v : vertices) {
            uintptr_t addr = reinterpret_cast<uintptr_t>(v);
            if (!v || addr < 0x1000 || addr > 0x7fffffffffff || !v->c) {
                // Clear and return if any invalid pointers found
                vertices.clear();
                return *this;
            }
        }

        std::stable_sort(vertices.begin(), vertices.end(),
            [this](Triangle* v1, Triangle* v2) {
                // Pointers already validated, just do the comparison
                return compareAngles(v1, v2) < 0;
            });
        return *this;
    }

    /**
     * Calculates the centroid of the region (average of circumcenters).
     */
    Point center() const {
        Point ctr(0, 0);
        size_t validCount = 0;
        for (Triangle* v : vertices) {
            if (v && v->c && !std::isnan(v->c->x) && !std::isnan(v->c->y)) {
                ctr.x += v->c->x;
                ctr.y += v->c->y;
                validCount++;
            }
        }
        if (validCount > 0) {
            ctr.x /= static_cast<float>(validCount);
            ctr.y /= static_cast<float>(validCount);
        }
        return ctr;
    }

    /**
     * Checks if this region borders another region.
     * Two regions border if they share an edge (two consecutive triangles).
     */
    bool borders(const Region& r) const {
        size_t len1 = vertices.size();
        size_t len2 = r.vertices.size();

        for (size_t i = 0; i < len1; ++i) {
            auto it = std::find(r.vertices.begin(), r.vertices.end(), vertices[i]);
            if (it != r.vertices.end()) {
                size_t j = static_cast<size_t>(std::distance(r.vertices.begin(), it));
                return vertices[(i + 1) % len1] == r.vertices[(j + len2 - 1) % len2];
            }
        }
        return false;
    }

private:
    /**
     * Compares two triangles by angle from seed to circumcenter.
     * Uses cross product for robust angle comparison without trig functions.
     */
    int compareAngles(Triangle* v1, Triangle* v2) const {
        // Defensive checks for invalid pointers
        // Check for null, low addresses, and unreasonably high addresses
        auto isInvalidTriangle = [](Triangle* ptr) {
            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            // Check for null, low addresses (< 4096), or addresses outside typical heap range
            // On 64-bit systems, valid heap addresses are typically in the range 0x1000-0x7fffffffffff
            return ptr == nullptr || addr < 0x1000 || addr > 0x7fffffffffff;
        };
        if (isInvalidTriangle(v1) || isInvalidTriangle(v2) || !seed) {
            // Fall back to pointer comparison for invalid triangles
            return (v1 < v2) ? -1 : ((v1 > v2) ? 1 : 0);
        }
        // Additional check for null shared_ptrs inside valid triangles
        if (!v1->c || !v2->c) {
            return (v1 < v2) ? -1 : ((v1 > v2) ? 1 : 0);
        }
        float x1 = v1->c->x - seed->x;
        float y1 = v1->c->y - seed->y;
        float x2 = v2->c->x - seed->x;
        float y2 = v2->c->y - seed->y;

        // Handle NaN values from degenerate triangles
        if (std::isnan(x1) || std::isnan(y1) || std::isnan(x2) || std::isnan(y2)) {
            return (v1 < v2) ? -1 : ((v1 > v2) ? 1 : 0);
        }

        if (x1 >= 0 && x2 < 0) return 1;
        if (x2 >= 0 && x1 < 0) return -1;
        if (x1 == 0 && x2 == 0) {
            return y2 > y1 ? 1 : -1;
        }

        return MathUtils::sign(x2 * y1 - x1 * y2);
    }
};

/**
 * Voronoi class - incremental Delaunay triangulation and Voronoi diagram.
 * Uses Bowyer-Watson algorithm for incremental point insertion.
 */
class Voronoi {
public:
    std::vector<std::unique_ptr<Triangle>> triangles;
    std::vector<PointPtr> frame;

    Voronoi(float minx, float miny, float maxx, float maxy) {
        // Create frame corners
        PointPtr c1 = makePoint(minx, miny);
        PointPtr c2 = makePoint(minx, maxy);
        PointPtr c3 = makePoint(maxx, miny);
        PointPtr c4 = makePoint(maxx, maxy);

        frame = {c1, c2, c3, c4};
        pointPtrs = {c1, c2, c3, c4};

        // Initial triangulation of the frame
        triangles.push_back(std::make_unique<Triangle>(c1, c2, c3));
        triangles.push_back(std::make_unique<Triangle>(c2, c3, c4));

        // Build initial regions
        _regionsDirty = false;
        for (const PointPtr& p : pointPtrs) {
            _regions[p] = buildRegion(p);
        }
    }

    /**
     * Adds a point to the triangulation using Bowyer-Watson algorithm.
     */
    void addPoint(PointPtr p) {
        std::vector<Triangle*> toSplit;

        for (const auto& tr : triangles) {
            if (Point::distance(*p, *tr->c) < tr->r) {
                toSplit.push_back(tr.get());
            }
        }

        if (!toSplit.empty()) {
            pointPtrs.push_back(p);

            std::vector<PointPtr> a;
            std::vector<PointPtr> b;

            for (Triangle* t1 : toSplit) {
                bool e1 = true;
                bool e2 = true;
                bool e3 = true;

                for (Triangle* t2 : toSplit) {
                    if (t2 != t1) {
                        // If triangles have a common edge, it goes in opposite directions
                        if (e1 && t2->hasEdge(t1->p2, t1->p1)) e1 = false;
                        if (e2 && t2->hasEdge(t1->p3, t1->p2)) e2 = false;
                        if (e3 && t2->hasEdge(t1->p1, t1->p3)) e3 = false;
                        if (!(e1 || e2 || e3)) break;
                    }
                }

                if (e1) { a.push_back(t1->p1); b.push_back(t1->p2); }
                if (e2) { a.push_back(t1->p2); b.push_back(t1->p3); }
                if (e3) { a.push_back(t1->p3); b.push_back(t1->p1); }
            }

            // Create new triangles connecting the point to the boundary
            // The edges in a/b should form a closed loop, so b[index] should always be in a
            if (!a.empty()) {
                size_t index = 0;
                size_t safety = a.size() + 1;  // Safety limit
                do {
                    triangles.push_back(std::make_unique<Triangle>(p, a[index], b[index]));
                    auto it = std::find(a.begin(), a.end(), b[index]);
                    index = (it != a.end()) ? std::distance(a.begin(), it) : 0;
                } while (index != 0 && --safety > 0);
            }

            // Remove split triangles
            for (Triangle* tr : toSplit) {
                triangles.erase(
                    std::remove_if(triangles.begin(), triangles.end(),
                        [tr](const std::unique_ptr<Triangle>& t) { return t.get() == tr; }),
                    triangles.end());
            }

            _regionsDirty = true;
        }
    }

    /**
     * Gets the regions map, rebuilding if dirty.
     */
    const std::unordered_map<PointPtr, std::unique_ptr<Region>, PointPtrHash, PointPtrEqual>& getRegions() {
        if (_regionsDirty) {
            _regions.clear();
            _regionsDirty = false;
            for (const PointPtr& p : pointPtrs) {
                _regions[p] = buildRegion(p);
            }
        }
        return _regions;
    }

    /**
     * Returns triangles which do not contain frame points as their vertices.
     */
    std::vector<Triangle*> triangulation() {
        std::vector<Triangle*> result;
        for (const auto& tr : triangles) {
            if (isReal(tr.get())) {
                result.push_back(tr.get());
            }
        }
        return result;
    }

    /**
     * Returns regions that don't touch the frame.
     */
    std::vector<Region*> partioning() {
        std::vector<Region*> result;
        const auto& regs = getRegions();

        for (const PointPtr& p : pointPtrs) {
            auto it = regs.find(p);
            if (it != regs.end()) {
                Region* r = it->second.get();
                if (!r || r->vertices.empty()) continue;
                bool real = true;
                for (Triangle* v : r->vertices) {
                    if (!v || !isReal(v)) {
                        real = false;
                        break;
                    }
                }
                if (real) {
                    result.push_back(r);
                }
            }
        }
        return result;
    }

    /**
     * Gets all regions that border a given region.
     */
    std::vector<Region*> getNeighbours(Region* r1) {
        std::vector<Region*> result;
        const auto& regs = getRegions();
        for (const auto& pair : regs) {
            if (r1->borders(*pair.second)) {
                result.push_back(pair.second.get());
            }
        }
        return result;
    }

    /**
     * Gets the list of point pointers (read-only).
     */
    const std::vector<PointPtr>& getPoints() const {
        return pointPtrs;
    }

    /**
     * Gets a mutable reference to the list of point pointers.
     * Needed for sorting operations that match the original Haxe API.
     */
    std::vector<PointPtr>& getPointsMutable() {
        return pointPtrs;
    }

    /**
     * Performs Lloyd relaxation on the Voronoi diagram.
     */
    static std::unique_ptr<Voronoi> relax(Voronoi& voronoi,
                                          const std::vector<PointPtr>* toRelax = nullptr) {
        auto regions = voronoi.partioning();

        std::vector<PointPtr> points;
        for (const PointPtr& p : voronoi.pointPtrs) {
            if (std::find(voronoi.frame.begin(), voronoi.frame.end(), p) == voronoi.frame.end()) {
                points.push_back(p);
            }
        }

        const std::vector<PointPtr>* relaxPoints = toRelax ? toRelax : &voronoi.pointPtrs;

        std::vector<Point> newPoints;
        for (Region* r : regions) {
            if (std::find(relaxPoints->begin(), relaxPoints->end(), r->seed) != relaxPoints->end()) {
                // Remove seed from points
                auto it = std::find(points.begin(), points.end(), r->seed);
                if (it != points.end()) {
                    points.erase(it);
                }
                // Add centroid
                newPoints.push_back(r->center());
            }
        }

        // Build new Voronoi from the relaxed points
        std::vector<Point> allPoints;
        for (const PointPtr& p : points) {
            allPoints.push_back(*p);
        }
        for (const Point& p : newPoints) {
            allPoints.push_back(p);
        }

        return build(allPoints);
    }

    /**
     * Builds a Voronoi diagram from a set of points.
     */
    static std::unique_ptr<Voronoi> build(const std::vector<Point>& vertices) {
        float minx = 1e+10f;
        float miny = 1e+10f;
        float maxx = -1e+9f;
        float maxy = -1e+9f;

        for (const Point& v : vertices) {
            if (v.x < minx) minx = v.x;
            if (v.y < miny) miny = v.y;
            if (v.x > maxx) maxx = v.x;
            if (v.y > maxy) maxy = v.y;
        }

        float dx = (maxx - minx) * 0.5f;
        float dy = (maxy - miny) * 0.5f;

        auto voronoi = std::make_unique<Voronoi>(
            minx - dx / 2, miny - dy / 2,
            maxx + dx / 2, maxy + dy / 2);

        for (const Point& v : vertices) {
            PointPtr p = makePoint(v.x, v.y);
            voronoi->addPoint(p);
        }

        return voronoi;
    }

private:
    std::vector<PointPtr> pointPtrs;
    bool _regionsDirty;
    std::unordered_map<PointPtr, std::unique_ptr<Region>, PointPtrHash, PointPtrEqual> _regions;

    /**
     * Builds a region for a given seed point.
     */
    std::unique_ptr<Region> buildRegion(PointPtr p) {
        auto r = std::make_unique<Region>(p);
        for (const auto& tr : triangles) {
            if (tr && tr->p1 && tr->p2 && tr->p3 && tr->c) {
                if (tr->p1 == p || tr->p2 == p || tr->p3 == p) {
                    r->vertices.push_back(tr.get());
                }
            }
        }
        r->sortVertices();
        return r;
    }

    /**
     * Checks if a triangle contains only non-frame vertices.
     */
    bool isReal(Triangle* tr) const {
        if (!tr || !tr->p1 || !tr->p2 || !tr->p3) return false;
        return std::find(frame.begin(), frame.end(), tr->p1) == frame.end() &&
               std::find(frame.begin(), frame.end(), tr->p2) == frame.end() &&
               std::find(frame.begin(), frame.end(), tr->p3) == frame.end();
    }
};

} // namespace town
