#pragma once

#include "town_generator/geom/Point.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>

namespace town_generator {
namespace geom {

class Region;

/**
 * Triangle - Delaunay triangle with circumcircle
 */
class Triangle {
public:
    Point p1, p2, p3;
    Point c;  // Circumcircle center
    double r; // Circumcircle radius

    Triangle(const Point& p1, const Point& p2, const Point& p3);

    bool isInCircumcircle(const Point& p) const {
        return Point::distance(p, c) < r;
    }

    // Equality based on vertices
    bool operator==(const Triangle& other) const {
        return p1 == other.p1 && p2 == other.p2 && p3 == other.p3;
    }

    bool operator!=(const Triangle& other) const {
        return !(*this == other);
    }
};

/**
 * Region - Voronoi region around a seed point
 */
class Region {
public:
    Point seed;
    std::vector<Triangle*> vertices; // Triangles whose circumcenters form the region

    Region() = default;
    explicit Region(const Point& seed) : seed(seed) {}

    void sortVertices();
    Point center() const;

    // Get neighboring regions
    std::vector<Region*> neighbors(const std::vector<std::unique_ptr<Region>>& allRegions) const;

    // Equality
    bool operator==(const Region& other) const {
        return seed == other.seed;
    }

    bool operator!=(const Region& other) const {
        return !(*this == other);
    }
};

/**
 * Voronoi - Voronoi tessellation via Delaunay triangulation
 * Faithful port from Haxe TownGeneratorOS
 */
class Voronoi {
private:
    std::vector<Point> frame_; // Bounding frame points

public:
    std::vector<std::unique_ptr<Triangle>> triangles;
    std::vector<std::unique_ptr<Region>> regions;

    // Constructor takes bounds: minx, miny, maxx, maxy (faithful to Haxe)
    Voronoi(double minx, double miny, double maxx, double maxy);

    void addPoint(const Point& p);

    // Extract real regions (excluding frame boundary regions)
    std::vector<Region*> partitioning();

    // Lloyd's relaxation
    static std::vector<Point> relax(const std::vector<Point>& vertices, double width, double height);

    // Build Voronoi from point set
    static Voronoi build(const std::vector<Point>& vertices);

    // Get frame
    const std::vector<Point>& frame() const { return frame_; }

    // Check if a triangle doesn't touch any frame points
    bool isRealTriangle(const Triangle* tr) const {
        for (const auto& fp : frame_) {
            if (tr->p1 == fp || tr->p2 == fp || tr->p3 == fp) {
                return false;
            }
        }
        return true;
    }

    // Equality
    bool operator==(const Voronoi& other) const {
        // Compare by frame and number of regions
        return frame_ == other.frame_ && triangles.size() == other.triangles.size();
    }

    bool operator!=(const Voronoi& other) const {
        return !(*this == other);
    }

private:
    void updateRegions(Triangle* tr);
    Region* findRegion(const Point& p);
};

} // namespace geom
} // namespace town_generator
