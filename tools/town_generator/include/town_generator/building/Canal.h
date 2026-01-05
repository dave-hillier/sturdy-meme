#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include <vector>
#include <map>
#include <memory>

namespace town_generator {
namespace building {

class Model;
class Patch;

/**
 * Canal - Water feature running through the city
 * Faithful port from mfcg.js Canal class
 *
 * Canals are river-like features that can run through cities,
 * with bridges at street crossings.
 */
class Canal {
public:
    // The path of the canal (sequence of edge origins)
    std::vector<geom::Point> course;

    // Width of the canal
    double width = 4.0;

    // Bridge locations (point -> street crossing)
    std::map<geom::Point, geom::Point, std::function<bool(const geom::Point&, const geom::Point&)>> bridges;

    // Reference to model
    Model* model = nullptr;

    Canal() : bridges([](const geom::Point& a, const geom::Point& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    }) {}

    // Create a river canal from shore to interior
    static std::unique_ptr<Canal> createRiver(Model* model);

    // Build the canal course (simple straight with waviness)
    void buildCourse(const geom::Point& start, const geom::Point& end);

    // Build the canal course following Voronoi edges
    void buildCourseAlongEdges(const geom::PointPtr& start, const geom::PointPtr& end);

    // Find bridge locations where streets cross the canal
    void findBridges();

    // Smooth the canal course (like mfcg.js smoothOpen)
    void smoothCourse(int iterations = 1);

    // Get points along the canal for rendering
    std::vector<geom::Point> getCenterline() const { return course; }

    // Get polygon representing the canal water area
    geom::Polygon getWaterPolygon() const;

    // Check if a vertex (by coordinate) is on the canal course
    bool containsVertex(const geom::Point& v, double tolerance = 0.5) const;

    // Check if an edge (v0 -> v1) is part of the canal course
    bool containsEdge(const geom::Point& v0, const geom::Point& v1, double tolerance = 0.5) const;

    // Get the canal width at a specific vertex (for building inset)
    double getWidthAtVertex(const geom::Point& v, double tolerance = 0.5) const;
};

} // namespace building
} // namespace town_generator
