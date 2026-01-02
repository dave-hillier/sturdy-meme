// Voronoi diagram generator using incremental Delaunay triangulation
// Ported from watabou's Medieval Fantasy City Generator
//
// Semantic rules:
// - Voronoi partitions city area into patches (regions)
// - Each region is associated with a seed point
// - Regions form the basis for ward assignment
// - Edges between regions become potential streets

#pragma once

#include "Geometry.h"
#include <vector>
#include <map>
#include <memory>
#include <algorithm>

namespace city {

// Forward declarations
class Voronoi;

// Triangle in the Delaunay triangulation
struct Triangle {
    Vec2 p1, p2, p3;
    Vec2 circumcenter;
    float circumradius;

    Triangle(const Vec2& a, const Vec2& b, const Vec2& c)
        : p1(a), p2(b), p3(c) {
        auto circle = Circle::circumcircle(a, b, c);
        circumcenter = circle.center;
        circumradius = circle.radius;
    }

    bool hasVertex(const Vec2& p) const {
        return p == p1 || p == p2 || p == p3;
    }

    bool hasEdge(const Vec2& a, const Vec2& b) const {
        return (a == p1 && b == p2) || (a == p2 && b == p1) ||
               (a == p2 && b == p3) || (a == p3 && b == p2) ||
               (a == p3 && b == p1) || (a == p1 && b == p3);
    }

    bool circumcircleContains(const Vec2& p) const {
        return Vec2::distance(p, circumcenter) < circumradius;
    }
};

// Voronoi region (dual of Delaunay triangulation)
// Each region corresponds to one seed point
struct Region {
    Vec2 seed;                           // The seed point for this region
    std::vector<Triangle*> triangles;    // Triangles containing this seed
    std::vector<Vec2> vertexPositions;   // Computed Voronoi vertices (circumcenters)

    explicit Region(const Vec2& s) : seed(s) {}

    // Sort vertices to form a proper polygon (CCW order around seed)
    void sortVertices() {
        if (triangles.size() < 3) return;

        // Collect circumcenters
        vertexPositions.clear();
        for (auto* tri : triangles) {
            vertexPositions.push_back(tri->circumcenter);
        }

        // Sort by angle from seed
        std::sort(vertexPositions.begin(), vertexPositions.end(),
            [this](const Vec2& a, const Vec2& b) {
                float angleA = std::atan2(a.y - seed.y, a.x - seed.x);
                float angleB = std::atan2(b.y - seed.y, b.x - seed.x);
                return angleA < angleB;
            });
    }

    // Get the polygon shape of this region
    Polygon shape() const {
        return Polygon(vertexPositions);
    }

    // Center of the region (average of vertices)
    Vec2 center() const {
        if (vertexPositions.empty()) return seed;
        Vec2 c{0, 0};
        for (const auto& v : vertexPositions) c += v;
        return c / static_cast<float>(vertexPositions.size());
    }

    // Check if this region shares an edge with another
    bool borders(const Region& other) const {
        for (const auto& v1 : vertexPositions) {
            for (size_t i = 0; i < other.vertexPositions.size(); i++) {
                size_t j = (i + 1) % other.vertexPositions.size();
                if (v1 == other.vertexPositions[i] || v1 == other.vertexPositions[j]) {
                    // Check if we share two consecutive vertices
                    for (size_t k = 0; k < vertexPositions.size(); k++) {
                        size_t l = (k + 1) % vertexPositions.size();
                        if ((vertexPositions[k] == other.vertexPositions[i] &&
                             vertexPositions[l] == other.vertexPositions[j]) ||
                            (vertexPositions[l] == other.vertexPositions[i] &&
                             vertexPositions[k] == other.vertexPositions[j])) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }
};

// Voronoi diagram via incremental Delaunay triangulation
class Voronoi {
public:
    std::vector<std::unique_ptr<Triangle>> triangles;
    std::vector<Vec2> points;         // All seed points
    std::vector<Vec2> frame;          // Bounding frame points (super-triangle)

    // Create Voronoi diagram with bounding frame
    Voronoi(float minX, float minY, float maxX, float maxY) {
        // Create bounding frame (4 corners)
        Vec2 c1{minX, minY};
        Vec2 c2{minX, maxY};
        Vec2 c3{maxX, minY};
        Vec2 c4{maxX, maxY};

        frame = {c1, c2, c3, c4};
        points = {c1, c2, c3, c4};

        // Initial triangulation of frame
        triangles.push_back(std::make_unique<Triangle>(c1, c2, c3));
        triangles.push_back(std::make_unique<Triangle>(c2, c3, c4));

        rebuildRegions();
    }

    // Add a point and update triangulation (Bowyer-Watson algorithm)
    void addPoint(const Vec2& p) {
        // Find triangles whose circumcircle contains the new point
        std::vector<Triangle*> badTriangles;
        for (auto& tri : triangles) {
            if (tri->circumcircleContains(p)) {
                badTriangles.push_back(tri.get());
            }
        }

        if (badTriangles.empty()) return;

        points.push_back(p);

        // Find the boundary of the polygonal hole
        std::vector<std::pair<Vec2, Vec2>> boundary;

        for (auto* tri : badTriangles) {
            // Check each edge
            std::pair<Vec2, Vec2> edges[3] = {
                {tri->p1, tri->p2},
                {tri->p2, tri->p3},
                {tri->p3, tri->p1}
            };

            for (const auto& edge : edges) {
                bool shared = false;
                for (auto* other : badTriangles) {
                    if (other != tri && other->hasEdge(edge.second, edge.first)) {
                        shared = true;
                        break;
                    }
                }
                if (!shared) {
                    boundary.push_back(edge);
                }
            }
        }

        // Remove bad triangles
        triangles.erase(
            std::remove_if(triangles.begin(), triangles.end(),
                [&badTriangles](const std::unique_ptr<Triangle>& tri) {
                    return std::find(badTriangles.begin(), badTriangles.end(),
                                    tri.get()) != badTriangles.end();
                }),
            triangles.end());

        // Create new triangles from boundary edges to new point
        for (const auto& edge : boundary) {
            triangles.push_back(std::make_unique<Triangle>(p, edge.first, edge.second));
        }

        regionsDirty = true;
    }

    // Get all regions (rebuild if necessary)
    const std::map<const Vec2*, Region>& getRegions() {
        if (regionsDirty) {
            rebuildRegions();
        }
        return regions;
    }

    // Get regions that don't touch the frame (interior regions)
    std::vector<Region*> getInteriorRegions() {
        if (regionsDirty) {
            rebuildRegions();
        }

        std::vector<Region*> result;
        for (auto& [seed, region] : regions) {
            bool touchesFrame = false;
            for (auto* tri : region.triangles) {
                for (const auto& fp : frame) {
                    if (tri->hasVertex(fp)) {
                        touchesFrame = true;
                        break;
                    }
                }
                if (touchesFrame) break;
            }
            if (!touchesFrame) {
                result.push_back(&region);
            }
        }
        return result;
    }

    // Get triangles that don't use frame points
    std::vector<Triangle*> getInteriorTriangles() const {
        std::vector<Triangle*> result;
        for (const auto& tri : triangles) {
            bool usesFrame = false;
            for (const auto& fp : frame) {
                if (tri->hasVertex(fp)) {
                    usesFrame = true;
                    break;
                }
            }
            if (!usesFrame) {
                result.push_back(tri.get());
            }
        }
        return result;
    }

    // Lloyd relaxation - move seeds to region centers
    static Voronoi relax(const Voronoi& voronoi, int iterations = 1) {
        auto interiorRegions = const_cast<Voronoi&>(voronoi).getInteriorRegions();

        // Collect interior seed points and their new positions
        std::vector<Vec2> newPoints;
        for (const auto& p : voronoi.points) {
            bool isFrame = false;
            for (const auto& fp : voronoi.frame) {
                if (p == fp) {
                    isFrame = true;
                    break;
                }
            }
            if (!isFrame) {
                // Find the region for this point
                bool found = false;
                for (auto* region : interiorRegions) {
                    if (region->seed == p) {
                        newPoints.push_back(region->center());
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    newPoints.push_back(p);
                }
            }
        }

        // Rebuild Voronoi with new points
        AABB bounds;
        for (const auto& p : voronoi.points) bounds.expand(p);
        float margin = std::max(bounds.size().x, bounds.size().y) * 0.25f;

        Voronoi result(bounds.min.x - margin, bounds.min.y - margin,
                       bounds.max.x + margin, bounds.max.y + margin);

        for (const auto& p : newPoints) {
            result.addPoint(p);
        }

        // Recurse for more iterations
        if (iterations > 1) {
            return relax(result, iterations - 1);
        }

        return result;
    }

    // Build from a set of points
    static Voronoi build(const std::vector<Vec2>& vertices) {
        if (vertices.empty()) {
            return Voronoi(-1, -1, 1, 1);
        }

        AABB bounds;
        for (const auto& v : vertices) bounds.expand(v);

        float dx = bounds.size().x * 0.5f;
        float dy = bounds.size().y * 0.5f;

        Voronoi voronoi(bounds.min.x - dx/2, bounds.min.y - dy/2,
                        bounds.max.x + dx/2, bounds.max.y + dy/2);

        for (const auto& v : vertices) {
            voronoi.addPoint(v);
        }

        return voronoi;
    }

private:
    std::map<const Vec2*, Region> regions;
    bool regionsDirty = true;

    void rebuildRegions() {
        regions.clear();

        // Create regions for each point
        for (const auto& p : points) {
            regions.emplace(&p, Region(p));
        }

        // Associate triangles with their vertices
        for (auto& tri : triangles) {
            for (auto& [seed, region] : regions) {
                if (tri->hasVertex(*seed)) {
                    region.triangles.push_back(tri.get());
                }
            }
        }

        // Sort vertices for each region
        for (auto& [seed, region] : regions) {
            region.sortVertices();
        }

        regionsDirty = false;
    }
};

} // namespace city
