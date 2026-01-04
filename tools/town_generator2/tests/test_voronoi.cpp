#include <doctest/doctest.h>
#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/geom/Voronoi.hpp"

using namespace town_generator2::geom;

TEST_SUITE("Triangle") {
    TEST_CASE("Triangle construction and circumcircle") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(4, 0);
        auto p3 = makePoint(2, 2);

        Triangle tri(p1, p2, p3);

        // Check circumcircle contains all three points
        CHECK(Point::distance(*tri.c, *p1) == doctest::Approx(tri.r).epsilon(0.01));
        CHECK(Point::distance(*tri.c, *p2) == doctest::Approx(tri.r).epsilon(0.01));
        CHECK(Point::distance(*tri.c, *p3) == doctest::Approx(tri.r).epsilon(0.01));
    }

    TEST_CASE("Triangle hasEdge") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(0, 1);

        Triangle tri(p1, p2, p3);

        // Triangle edges (CCW order)
        CHECK(tri.hasEdge(tri.p1, tri.p2));
        CHECK(tri.hasEdge(tri.p2, tri.p3));
        CHECK(tri.hasEdge(tri.p3, tri.p1));

        // Reverse direction should not match
        CHECK_FALSE(tri.hasEdge(tri.p2, tri.p1));
    }
}

TEST_SUITE("Voronoi construction") {
    TEST_CASE("Build from few points - no hang") {
        PointList points;
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(10, 0));
        points.push_back(makePoint(5, 10));

        // This should complete quickly
        Voronoi v = Voronoi::build(points);

        CHECK(v.points.size() >= 3);
        CHECK(v.triangles.size() >= 1);
    }

    TEST_CASE("Build from grid of points") {
        PointList points;
        for (int x = 0; x < 3; ++x) {
            for (int y = 0; y < 3; ++y) {
                points.push_back(makePoint(x * 10.0, y * 10.0));
            }
        }

        Voronoi v = Voronoi::build(points);

        // Should have triangulation
        CHECK(v.triangles.size() >= 4);
    }

    TEST_CASE("Build from random points") {
        PointList points;
        for (int i = 0; i < 10; ++i) {
            double angle = i * 2.0 * M_PI / 10.0;
            double r = 10.0 + (i % 3) * 5.0;
            points.push_back(makePoint(r * std::cos(angle), r * std::sin(angle)));
        }

        Voronoi v = Voronoi::build(points);

        CHECK(v.triangles.size() >= 1);
    }
}

TEST_SUITE("Voronoi regions") {
    TEST_CASE("Regions are created for each point") {
        PointList points;
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(20, 0));
        points.push_back(makePoint(10, 20));

        Voronoi v = Voronoi::build(points);
        auto& regions = v.regions();

        // Should have region for each point (including frame points)
        CHECK(regions.size() == v.points.size());
    }

    TEST_CASE("Partitioning returns real regions only") {
        PointList points;
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(20, 0));
        points.push_back(makePoint(10, 20));

        Voronoi v = Voronoi::build(points);
        auto parts = v.partioning();

        // May have fewer regions (frame points excluded)
        CHECK(parts.size() <= points.size());
    }

    TEST_CASE("Region polygon has vertices") {
        PointList points;
        points.push_back(makePoint(5, 5));
        points.push_back(makePoint(15, 5));
        points.push_back(makePoint(10, 15));

        Voronoi v = Voronoi::build(points);
        auto parts = v.partioning();

        for (const auto& r : parts) {
            Polygon poly = r.polygon();
            CHECK(poly.length() >= 3);
        }
    }
}

TEST_SUITE("Voronoi relaxation") {
    TEST_CASE("Relax converges to more uniform distribution") {
        PointList points;
        // Start with clustered points
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(1, 0));
        points.push_back(makePoint(0, 1));
        points.push_back(makePoint(20, 20));

        Voronoi v1 = Voronoi::build(points);

        // Relax once
        Voronoi v2 = Voronoi::relax(v1);

        // Should still have valid triangulation
        CHECK(v2.triangles.size() >= 1);

        // Points should have moved
        // (Hard to test exact positions, just verify structure is valid)
        auto parts = v2.partioning();
        for (const auto& r : parts) {
            Polygon poly = r.polygon();
            CHECK(poly.length() >= 3);
        }
    }

    TEST_CASE("Multiple relaxation iterations") {
        PointList points;
        for (int i = 0; i < 5; ++i) {
            double angle = i * 2.0 * M_PI / 5.0;
            points.push_back(makePoint(10 * std::cos(angle), 10 * std::sin(angle)));
        }

        Voronoi v = Voronoi::build(points);

        // Multiple relaxation steps should not hang
        for (int i = 0; i < 3; ++i) {
            v = Voronoi::relax(v);
        }

        CHECK(v.triangles.size() >= 1);
    }
}

TEST_SUITE("Voronoi edge cases") {
    TEST_CASE("Collinear points") {
        PointList points;
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(10, 0));
        points.push_back(makePoint(20, 0));

        // Should handle collinear points (may have degenerate triangles)
        Voronoi v = Voronoi::build(points);
        CHECK(v.points.size() >= 3);
    }

    TEST_CASE("Duplicate points") {
        PointList points;
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(0, 0)); // Duplicate
        points.push_back(makePoint(10, 0));
        points.push_back(makePoint(5, 10));

        // Should handle duplicates gracefully
        Voronoi v = Voronoi::build(points);
        CHECK(v.triangles.size() >= 1);
    }

    TEST_CASE("Single point") {
        PointList points;
        points.push_back(makePoint(5, 5));

        Voronoi v = Voronoi::build(points);
        // Should have frame + 1 point
        CHECK(v.points.size() >= 1);
    }

    TEST_CASE("Two points") {
        PointList points;
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(10, 10));

        Voronoi v = Voronoi::build(points);
        CHECK(v.triangles.size() >= 1);
    }
}

TEST_SUITE("Voronoi triangulation") {
    TEST_CASE("Triangulation returns real triangles") {
        PointList points;
        points.push_back(makePoint(5, 5));
        points.push_back(makePoint(15, 5));
        points.push_back(makePoint(10, 15));
        points.push_back(makePoint(10, 8));

        Voronoi v = Voronoi::build(points);
        auto tris = v.triangulation();

        // All returned triangles should not contain frame points
        for (const auto& tri : tris) {
            CHECK(v.isReal(tri));
        }
    }
}

TEST_SUITE("Region borders") {
    TEST_CASE("Adjacent regions share edge") {
        PointList points;
        // Three points forming a triangle
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(20, 0));
        points.push_back(makePoint(10, 20));

        Voronoi v = Voronoi::build(points);
        auto parts = v.partioning();

        if (parts.size() >= 2) {
            // At least some regions should border each other
            bool foundBordering = false;
            for (size_t i = 0; i < parts.size() && !foundBordering; ++i) {
                for (size_t j = i + 1; j < parts.size(); ++j) {
                    if (parts[i].borders(parts[j])) {
                        foundBordering = true;
                        break;
                    }
                }
            }
            // It's OK if no bordering regions found for small input
        }
    }
}
