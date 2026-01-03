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
 * Custom hash and equality for Point pointers used as map keys.
 * Haxe uses object identity for Map<Point, T>, so we use raw pointer comparison.
 */
struct PointPtrHash {
    std::size_t operator()(const Point* p) const {
        return std::hash<const void*>()(p);
    }
};

struct PointPtrEqual {
    bool operator()(const Point* a, const Point* b) const {
        return a == b;
    }
};

/**
 * Triangle class - represents a triangle in the Delaunay triangulation.
 * Stores circumcircle center and radius for point-in-circle tests.
 */
class Triangle {
public:
    Point* p1;
    Point* p2;
    Point* p3;

    Point c;  // Circumcircle center
    float r;  // Circumcircle radius

    Triangle(Point* p1, Point* p2, Point* p3) {
        // Calculate signed area to determine winding order
        float s = (p2->x - p1->x) * (p2->y + p1->y) +
                  (p3->x - p2->x) * (p3->y + p2->y) +
                  (p1->x - p3->x) * (p1->y + p3->y);

        this->p1 = p1;
        // Ensure CCW winding
        this->p2 = s > 0 ? p2 : p3;
        this->p3 = s > 0 ? p3 : p2;

        // Calculate circumcircle center
        float x1 = (p1->x + p2->x) / 2.0f;
        float y1 = (p1->y + p2->y) / 2.0f;
        float x2 = (p2->x + p3->x) / 2.0f;
        float y2 = (p2->y + p3->y) / 2.0f;

        float dx1 = p1->y - p2->y;
        float dy1 = p2->x - p1->x;
        float dx2 = p2->y - p3->y;
        float dy2 = p3->x - p2->x;

        float tg1 = dy1 / dx1;
        float t2 = ((y1 - y2) - (x1 - x2) * tg1) / (dy2 - dx2 * tg1);

        c.x = x2 + dx2 * t2;
        c.y = y2 + dy2 * t2;
        r = Point::distance(c, *p1);
    }

    /**
     * Checks if this triangle has an edge from point a to point b.
     * Edge direction matters for Delaunay triangulation.
     */
    bool hasEdge(Point* a, Point* b) const {
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
    Point* seed;
    std::vector<Triangle*> vertices;

    explicit Region(Point* seed) : seed(seed) {}

    /**
     * Sorts vertices (triangles) by angle around the seed point.
     * This ensures the circumcenters form a proper polygon when connected.
     */
    Region& sortVertices() {
        std::sort(vertices.begin(), vertices.end(),
            [this](Triangle* v1, Triangle* v2) {
                return compareAngles(v1, v2) < 0;
            });
        return *this;
    }

    /**
     * Calculates the centroid of the region (average of circumcenters).
     */
    Point center() const {
        Point c(0, 0);
        for (Triangle* v : vertices) {
            c.x += v->c.x;
            c.y += v->c.y;
        }
        c.x /= static_cast<float>(vertices.size());
        c.y /= static_cast<float>(vertices.size());
        return c;
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
        float x1 = v1->c.x - seed->x;
        float y1 = v1->c.y - seed->y;
        float x2 = v2->c.x - seed->x;
        float y2 = v2->c.y - seed->y;

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
    std::vector<std::unique_ptr<Point>> points;
    std::vector<Point*> frame;

    // Owned storage for points (for proper memory management)
    std::vector<std::unique_ptr<Point>> ownedPoints;

    Voronoi(float minx, float miny, float maxx, float maxy) {
        // Create frame corners
        auto c1 = std::make_unique<Point>(minx, miny);
        auto c2 = std::make_unique<Point>(minx, maxy);
        auto c3 = std::make_unique<Point>(maxx, miny);
        auto c4 = std::make_unique<Point>(maxx, maxy);

        Point* p1 = c1.get();
        Point* p2 = c2.get();
        Point* p3 = c3.get();
        Point* p4 = c4.get();

        frame = {p1, p2, p3, p4};

        ownedPoints.push_back(std::move(c1));
        ownedPoints.push_back(std::move(c2));
        ownedPoints.push_back(std::move(c3));
        ownedPoints.push_back(std::move(c4));

        // Store raw pointers for access
        pointPtrs = {p1, p2, p3, p4};

        // Initial triangulation of the frame
        triangles.push_back(std::make_unique<Triangle>(p1, p2, p3));
        triangles.push_back(std::make_unique<Triangle>(p2, p3, p4));

        // Build initial regions
        _regionsDirty = false;
        for (Point* p : pointPtrs) {
            _regions[p] = buildRegion(p);
        }
    }

    /**
     * Adds a point to the triangulation using Bowyer-Watson algorithm.
     */
    void addPoint(Point* p) {
        std::vector<Triangle*> toSplit;

        for (const auto& tr : triangles) {
            if (Point::distance(*p, tr->c) < tr->r) {
                toSplit.push_back(tr.get());
            }
        }

        if (!toSplit.empty()) {
            pointPtrs.push_back(p);

            std::vector<Point*> a;
            std::vector<Point*> b;

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
            size_t index = 0;
            do {
                triangles.push_back(std::make_unique<Triangle>(p, a[index], b[index]));
                auto it = std::find(a.begin(), a.end(), b[index]);
                index = static_cast<size_t>(std::distance(a.begin(), it));
            } while (index != 0);

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
    const std::unordered_map<Point*, std::unique_ptr<Region>, PointPtrHash, PointPtrEqual>& getRegions() {
        if (_regionsDirty) {
            _regions.clear();
            _regionsDirty = false;
            for (Point* p : pointPtrs) {
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

        for (Point* p : pointPtrs) {
            auto it = regs.find(p);
            if (it != regs.end()) {
                Region* r = it->second.get();
                bool real = true;
                for (Triangle* v : r->vertices) {
                    if (!isReal(v)) {
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
    const std::vector<Point*>& getPoints() const {
        return pointPtrs;
    }

    /**
     * Gets a mutable reference to the list of point pointers.
     * Needed for sorting operations that match the original Haxe API.
     */
    std::vector<Point*>& getPointsMutable() const {
        return const_cast<std::vector<Point*>&>(pointPtrs);
    }

    /**
     * Performs Lloyd relaxation on the Voronoi diagram.
     */
    static std::unique_ptr<Voronoi> relax(Voronoi& voronoi,
                                          const std::vector<Point*>* toRelax = nullptr) {
        auto regions = voronoi.partioning();

        std::vector<Point*> points;
        for (Point* p : voronoi.pointPtrs) {
            if (std::find(voronoi.frame.begin(), voronoi.frame.end(), p) == voronoi.frame.end()) {
                points.push_back(p);
            }
        }

        const std::vector<Point*>* relaxPoints = toRelax ? toRelax : &voronoi.pointPtrs;

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
        for (Point* p : points) {
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
            auto p = std::make_unique<Point>(v.x, v.y);
            Point* ptr = p.get();
            voronoi->ownedPoints.push_back(std::move(p));
            voronoi->addPoint(ptr);
        }

        return voronoi;
    }

private:
    std::vector<Point*> pointPtrs;
    bool _regionsDirty;
    std::unordered_map<Point*, std::unique_ptr<Region>, PointPtrHash, PointPtrEqual> _regions;

    /**
     * Builds a region for a given seed point.
     */
    std::unique_ptr<Region> buildRegion(Point* p) {
        auto r = std::make_unique<Region>(p);
        for (const auto& tr : triangles) {
            if (tr->p1 == p || tr->p2 == p || tr->p3 == p) {
                r->vertices.push_back(tr.get());
            }
        }
        r->sortVertices();
        return r;
    }

    /**
     * Checks if a triangle contains only non-frame vertices.
     */
    bool isReal(Triangle* tr) const {
        return std::find(frame.begin(), frame.end(), tr->p1) == frame.end() &&
               std::find(frame.begin(), frame.end(), tr->p2) == frame.end() &&
               std::find(frame.begin(), frame.end(), tr->p3) == frame.end();
    }
};

} // namespace town
