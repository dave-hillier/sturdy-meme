#pragma once

#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/utils/MathUtils.hpp"
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <memory>
#include <iostream>

namespace town_generator2 {
namespace geom {

class Region;  // Forward declaration

/**
 * Triangle - Delaunay triangle with circumcircle
 */
class Triangle {
public:
    PointPtr p1, p2, p3;
    PointPtr c;   // Circumcircle center (shared between adjacent regions)
    double r;     // Circumcircle radius

    Triangle(PointPtr p1_, PointPtr p2_, PointPtr p3_) {
        // Ensure CCW orientation
        double s = (p2_->x - p1_->x) * (p2_->y + p1_->y) +
                   (p3_->x - p2_->x) * (p3_->y + p2_->y) +
                   (p1_->x - p3_->x) * (p1_->y + p3_->y);

        p1 = p1_;
        p2 = s > 0 ? p2_ : p3_;
        p3 = s > 0 ? p3_ : p2_;

        // Calculate circumcircle using ORIGINAL vertices (before swap)
        // This matches the Haxe behavior which uses parameter values
        double x1 = (p1_->x + p2_->x) / 2;
        double y1 = (p1_->y + p2_->y) / 2;
        double x2 = (p2_->x + p3_->x) / 2;
        double y2 = (p2_->y + p3_->y) / 2;

        double dx1 = p1_->y - p2_->y;
        double dy1 = p2_->x - p1_->x;
        double dx2 = p2_->y - p3_->y;
        double dy2 = p3_->x - p2_->x;

        double tg1 = dy1 / dx1;
        double t2 = ((y1 - y2) - (x1 - x2) * tg1) / (dy2 - dx2 * tg1);

        c = makePoint(x2 + dx2 * t2, y2 + dy2 * t2);
        r = Point::distance(*c, *p1_);
    }

    bool hasEdge(const PointPtr& a, const PointPtr& b) const {
        return (p1 == a && p2 == b) ||
               (p2 == a && p3 == b) ||
               (p3 == a && p1 == b);
    }
};

using TrianglePtr = std::shared_ptr<Triangle>;

/**
 * Region - Voronoi region around a seed point
 */
class Region {
public:
    PointPtr seed;
    std::vector<TrianglePtr> vertices;  // Triangles that share this seed

    explicit Region(PointPtr seed_) : seed(seed_) {}

    Region& sortVertices() {
        std::sort(vertices.begin(), vertices.end(),
            [this](const TrianglePtr& v1, const TrianglePtr& v2) {
                return compareAngles(v1, v2) < 0;
            });
        return *this;
    }

    Point center() const {
        Point c;
        for (const auto& v : vertices) {
            c.addEq(*v->c);
        }
        c.scaleEq(1.0 / vertices.size());
        return c;
    }

    // Check if this region shares an edge with another
    bool borders(const Region& r) const {
        size_t len1 = vertices.size();
        size_t len2 = r.vertices.size();
        for (size_t i = 0; i < len1; ++i) {
            auto it = std::find(r.vertices.begin(), r.vertices.end(), vertices[i]);
            if (it != r.vertices.end()) {
                size_t j = std::distance(r.vertices.begin(), it);
                return vertices[(i + 1) % len1] == r.vertices[(j + len2 - 1) % len2];
            }
        }
        return false;
    }

    // Get the Voronoi polygon for this region
    Polygon polygon() const {
        std::vector<PointPtr> pts;
        pts.reserve(vertices.size());
        for (const auto& tr : vertices) {
            pts.push_back(tr->c);
        }
        return Polygon(pts);
    }

private:
    int compareAngles(const TrianglePtr& v1, const TrianglePtr& v2) const {
        double x1 = v1->c->x - seed->x;
        double y1 = v1->c->y - seed->y;
        double x2 = v2->c->x - seed->x;
        double y2 = v2->c->y - seed->y;

        if (x1 >= 0 && x2 < 0) return 1;
        if (x2 >= 0 && x1 < 0) return -1;
        if (x1 == 0 && x2 == 0) {
            return y2 > y1 ? 1 : -1;
        }

        return utils::MathUtils::sign(x2 * y1 - x1 * y2);
    }
};

/**
 * Voronoi - Incremental Delaunay triangulation with Voronoi diagram extraction
 */
class Voronoi {
public:
    std::vector<TrianglePtr> triangles;
    PointList points;
    PointList frame;  // Corner points of bounding box

    Voronoi(double minx, double miny, double maxx, double maxy) {
        auto c1 = makePoint(minx, miny);
        auto c2 = makePoint(minx, maxy);
        auto c3 = makePoint(maxx, miny);
        auto c4 = makePoint(maxx, maxy);

        frame = {c1, c2, c3, c4};
        points = {c1, c2, c3, c4};

        triangles.push_back(std::make_shared<Triangle>(c1, c2, c3));
        triangles.push_back(std::make_shared<Triangle>(c2, c3, c4));

        regionsDirty_ = true;
    }

    /**
     * Add a point to the triangulation
     */
    void addPoint(PointPtr p) {
        std::cerr << "(s" << triangles.size() << ")" << std::flush;
        std::vector<TrianglePtr> toSplit;
        for (const auto& tr : triangles) {
            if (Point::distance(*p, *tr->c) < tr->r) {
                toSplit.push_back(tr);
            }
        }
        std::cerr << "(t" << toSplit.size() << ")" << std::flush;

        if (!toSplit.empty()) {
            points.push_back(p);

            // Find boundary edges
            PointList a, b;
            for (const auto& t1 : toSplit) {
                bool e1 = true, e2 = true, e3 = true;
                for (const auto& t2 : toSplit) {
                    if (t2 != t1) {
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
            std::cerr << "(e" << a.size() << ")" << std::flush;

            // Create new triangles by walking around the boundary
            // The edges in a/b form a closed loop - walk around it
            if (!a.empty()) {
                size_t index = 0;
                size_t startIndex = 0;
                size_t created = 0;
                do {
                    triangles.push_back(std::make_shared<Triangle>(p, a[index], b[index]));
                    created++;
                    // Find where b[index] appears in a
                    auto it = std::find(a.begin(), a.end(), b[index]);
                    if (it == a.end()) {
                        // Boundary not closed - shouldn't happen, but fall back
                        std::cerr << "[WARN:boundary_break]" << std::flush;
                        break;
                    }
                    index = std::distance(a.begin(), it);
                    if (created > a.size()) {
                        std::cerr << "[WARN:too_many]" << std::flush;
                        break;
                    }
                } while (index != startIndex);
            }
            std::cerr << "(c)" << std::flush;

            // Remove split triangles
            for (const auto& tr : toSplit) {
                auto it = std::find(triangles.begin(), triangles.end(), tr);
                if (it != triangles.end()) {
                    triangles.erase(it);
                }
            }
            std::cerr << "(r)" << std::flush;

            regionsDirty_ = true;
        }
    }

    /**
     * Get Voronoi regions, rebuilding if dirty
     */
    const std::map<PointPtr, Region>& regions() {
        if (regionsDirty_) {
            regions_.clear();
            for (const auto& p : points) {
                regions_.emplace(p, buildRegion(p));
            }
            regionsDirty_ = false;
        }
        return regions_;
    }

    /**
     * Check if a triangle is "real" (none of its vertices are frame points)
     */
    bool isReal(const TrianglePtr& tr) const {
        return std::find(frame.begin(), frame.end(), tr->p1) == frame.end() &&
               std::find(frame.begin(), frame.end(), tr->p2) == frame.end() &&
               std::find(frame.begin(), frame.end(), tr->p3) == frame.end();
    }

    /**
     * Get triangles that don't contain frame points
     */
    std::vector<TrianglePtr> triangulation() const {
        std::vector<TrianglePtr> result;
        for (const auto& tr : triangles) {
            if (isReal(tr)) {
                result.push_back(tr);
            }
        }
        return result;
    }

    /**
     * Get real Voronoi regions (not touching frame)
     */
    std::vector<Region> partioning() {
        std::vector<Region> result;
        auto& regs = regions();

        for (const auto& p : points) {
            auto it = regs.find(p);
            if (it != regs.end()) {
                const Region& r = it->second;
                bool isRealRegion = true;
                for (const auto& v : r.vertices) {
                    if (!isReal(v)) {
                        isRealRegion = false;
                        break;
                    }
                }
                if (isRealRegion) {
                    result.push_back(r);
                }
            }
        }
        return result;
    }

    /**
     * Get neighboring regions
     */
    std::vector<Region> getNeighbours(const Region& r1) {
        std::vector<Region> result;
        auto& regs = regions();
        for (const auto& [p, r2] : regs) {
            if (r1.borders(r2)) {
                result.push_back(r2);
            }
        }
        return result;
    }

    /**
     * Lloyd relaxation - move seed points to region centroids
     */
    static Voronoi relax(Voronoi& voronoi, const PointList* toRelax = nullptr) {
        auto regions = voronoi.partioning();

        PointList newPoints;
        for (const auto& p : voronoi.points) {
            if (std::find(voronoi.frame.begin(), voronoi.frame.end(), p) == voronoi.frame.end()) {
                newPoints.push_back(p);
            }
        }

        const PointList& relaxPoints = toRelax ? *toRelax : voronoi.points;
        for (const auto& r : regions) {
            if (std::find(relaxPoints.begin(), relaxPoints.end(), r.seed) != relaxPoints.end()) {
                // Remove old seed, add centroid
                auto it = std::find(newPoints.begin(), newPoints.end(), r.seed);
                if (it != newPoints.end()) {
                    newPoints.erase(it);
                }
                newPoints.push_back(makePoint(r.center()));
            }
        }

        return build(newPoints);
    }

    /**
     * Build Voronoi diagram from points
     */
    static Voronoi build(const PointList& vertices) {
        double minx = 1e+10, miny = 1e+10;
        double maxx = -1e+9, maxy = -1e+9;

        for (const auto& v : vertices) {
            if (v->x < minx) minx = v->x;
            if (v->y < miny) miny = v->y;
            if (v->x > maxx) maxx = v->x;
            if (v->y > maxy) maxy = v->y;
        }

        double dx = (maxx - minx) * 0.5;
        double dy = (maxy - miny) * 0.5;

        Voronoi voronoi(minx - dx / 2, miny - dy / 2, maxx + dx / 2, maxy + dy / 2);
        size_t count = 0;
        for (const auto& v : vertices) {
            std::cerr << count << std::flush;
            voronoi.addPoint(v);
            count++;
        }
        std::cerr << "!" << std::flush;

        return voronoi;
    }

    /**
     * Build from value points (creates new PointPtrs)
     */
    static Voronoi build(const std::vector<Point>& vertices) {
        PointList pts;
        pts.reserve(vertices.size());
        for (const auto& v : vertices) {
            pts.push_back(makePoint(v));
        }
        return build(pts);
    }

private:
    std::map<PointPtr, Region> regions_;
    bool regionsDirty_ = true;

    Region buildRegion(const PointPtr& p) {
        Region r(p);
        for (const auto& tr : triangles) {
            if (tr->p1 == p || tr->p2 == p || tr->p3 == p) {
                r.vertices.push_back(tr);
            }
        }
        return r.sortVertices();
    }
};

} // namespace geom
} // namespace town_generator2
