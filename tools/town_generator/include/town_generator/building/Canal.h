#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/geom/Graph.h"
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace town_generator {
namespace building {

class Model;
class Patch;

/**
 * CanalTopology - Separate topology for river pathfinding
 * Faithful to mfcg.js yb.buildTopology / gh class
 */
class CanalTopology {
public:
    geom::Graph graph;
    std::unordered_map<geom::PointPtr, geom::Node*> pt2node;
    std::unordered_map<geom::Node*, geom::PointPtr> node2pt;

    // Build topology from non-water patches
    void build(Model* model);

    // Exclude polygon edges from the graph (like mfcg.js excludePolygon)
    void excludePolygon(const std::vector<geom::PointPtr>& polygon);

    // Exclude specific points (like mfcg.js excludePoints)
    void excludePoints(const std::vector<geom::PointPtr>& points);

    // Build path using A* (like mfcg.js buildPath)
    std::vector<geom::PointPtr> buildPath(const geom::PointPtr& from, const geom::PointPtr& to);

private:
    std::unordered_set<geom::PointPtr> excludedPoints_;
    geom::Node* getOrCreateNode(const geom::PointPtr& pt);
};

/**
 * Canal - Water feature running through the city
 * Faithful port from mfcg.js Canal class
 *
 * Canals are river-like features that can run through cities,
 * with bridges at street crossings.
 */
class Canal {
public:
    // The path of the canal (sequence of PointPtrs for reference semantics)
    std::vector<geom::PointPtr> coursePtr;

    // The path as points for compatibility
    std::vector<geom::Point> course;

    // Width of the canal (faithful to mfcg.js: 3-6 based on city size)
    double width = 4.0;

    // Whether the canal is rural (outside inner city)
    bool rural = false;

    // Bridge locations (point -> street direction)
    std::map<geom::Point, geom::Point, std::function<bool(const geom::Point&, const geom::Point&)>> bridges;

    // Gate locations on walls where canal passes
    std::vector<geom::PointPtr> gates;

    // Reference to model
    Model* model = nullptr;

    Canal() : bridges([](const geom::Point& a, const geom::Point& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    }) {}

    // Create a river canal (faithful to mfcg.js yb.createRiver)
    static std::unique_ptr<Canal> createRiver(Model* model);

    // Find bridge locations where streets cross the canal
    void findBridges();

    // Update state after course is built (bridges, gates, width)
    void updateState();

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

private:
    // Delta river for coastal cities (mfcg.js yb.deltaRiver)
    static std::vector<geom::PointPtr> deltaRiver(Model* model, CanalTopology& topology);

    // Regular river for non-coastal cities (mfcg.js yb.regularRiver)
    static std::vector<geom::PointPtr> regularRiver(Model* model, CanalTopology& topology);

    // Validate the course (mfcg.js yb.validateCourse)
    static bool validateCourse(Model* model, const std::vector<geom::PointPtr>& course);
};

} // namespace building
} // namespace town_generator
