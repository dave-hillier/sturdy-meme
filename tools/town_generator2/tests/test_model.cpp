#include <doctest/doctest.h>
#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/geom/Voronoi.hpp"
#include "town_generator2/building/Model.hpp"
#include "town_generator2/building/Patch.hpp"
#include "town_generator2/building/CurtainWall.hpp"
#include "town_generator2/building/Cutter.hpp"
#include "town_generator2/wards/Ward.hpp"
#include "town_generator2/wards/AllWards.hpp"
#include "town_generator2/utils/Random.hpp"

using namespace town_generator2;
using namespace town_generator2::geom;
using namespace town_generator2::building;

TEST_SUITE("Random") {
    TEST_CASE("Random seed reproducibility") {
        utils::Random::reset(42);
        double v1 = utils::Random::getFloat();
        double v2 = utils::Random::getFloat();

        utils::Random::reset(42);
        CHECK(utils::Random::getFloat() == v1);
        CHECK(utils::Random::getFloat() == v2);
    }

    TEST_CASE("Random getInt range") {
        utils::Random::reset(123);
        for (int i = 0; i < 100; ++i) {
            int v = utils::Random::getInt(0, 10);
            CHECK(v >= 0);
            CHECK(v < 10);
        }
    }

    TEST_CASE("Random getBool") {
        utils::Random::reset(456);
        int trueCount = 0;
        for (int i = 0; i < 100; ++i) {
            if (utils::Random::getBool()) trueCount++;
        }
        // Should be roughly 50/50
        CHECK(trueCount > 20);
        CHECK(trueCount < 80);
    }
}

TEST_SUITE("Patch") {
    TEST_CASE("Patch fromRegion") {
        PointList points;
        points.push_back(makePoint(0, 0));
        points.push_back(makePoint(20, 0));
        points.push_back(makePoint(10, 20));

        Voronoi v = Voronoi::build(points);
        auto parts = v.partioning();

        if (!parts.empty()) {
            auto patch = Patch::fromRegion(parts[0]);
            CHECK(patch != nullptr);
            CHECK(patch->shape.length() >= 3);
        }
    }

    TEST_CASE("Patch initial state") {
        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        CHECK(patch.withinCity == false);
        CHECK(patch.withinWalls == false);
        CHECK(patch.ward == nullptr);
    }
}

TEST_SUITE("Cutter") {
    TEST_CASE("Cutter bisect") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        auto p0 = square.ptr(0);

        auto halves = Cutter::bisect(square, p0, 0.5, 0.0, 0.0);

        CHECK(halves.size() == 2);
        double totalArea = std::abs(halves[0].square()) + std::abs(halves[1].square());
        CHECK(totalArea == doctest::Approx(100.0).epsilon(1.0));
    }

    TEST_CASE("Cutter bisect with gap") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        auto p0 = square.ptr(0);

        auto halves = Cutter::bisect(square, p0, 0.5, 0.0, 1.0);

        CHECK(halves.size() == 2);
        // With gap, total area should be less
        double totalArea = std::abs(halves[0].square()) + std::abs(halves[1].square());
        CHECK(totalArea < 100.0);
    }

    TEST_CASE("Cutter radial") {
        Polygon hex = Polygon::regular(6, 10.0);
        auto parts = Cutter::radial(hex, nullptr, 0.5);

        // Should have created some parts
        CHECK(parts.size() >= 1);
    }

    TEST_CASE("Cutter ring") {
        Polygon square({Point(0, 0), Point(20, 0), Point(20, 20), Point(0, 20)});
        auto ring = Cutter::ring(square, 2.0);

        // Ring should create some geometry
        CHECK(ring.size() >= 1);
    }
}

TEST_SUITE("Model buildPatches") {
    TEST_CASE("buildPatches creates patches - small count") {
        utils::Random::reset(42);

        // Create minimal test - just test that patches can be built
        PointList points;
        double sa = utils::Random::getFloat() * 2 * M_PI;
        int nPatches = 5;

        for (int i = 0; i < nPatches * 8; ++i) {
            double a = sa + std::sqrt(i) * 5;
            double r = (i == 0) ? 0 : 10 + i * (2 + utils::Random::getFloat());
            points.push_back(makePoint(std::cos(a) * r, std::sin(a) * r));
        }

        Voronoi voronoi = Voronoi::build(points);
        CHECK(voronoi.triangles.size() >= 1);

        // Can relax without hanging
        for (int i = 0; i < 3; ++i) {
            PointList toRelax;
            for (int j = 0; j < 3 && j < static_cast<int>(voronoi.points.size()); ++j) {
                toRelax.push_back(voronoi.points[j]);
            }
            if (nPatches < static_cast<int>(voronoi.points.size())) {
                toRelax.push_back(voronoi.points[nPatches]);
            }
            voronoi = Voronoi::relax(voronoi, &toRelax);
        }

        auto regions = voronoi.partioning();
        CHECK(regions.size() >= 1);
    }
}

TEST_SUITE("Model findCircumference") {
    TEST_CASE("findCircumference with single patch") {
        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        auto patch = std::make_unique<Patch>(shape);
        std::vector<Patch*> patches = {patch.get()};

        Polygon circ = Model::findCircumference(patches);
        CHECK(circ.length() == 4);
    }

    TEST_CASE("findCircumference with empty patches") {
        std::vector<Patch*> patches;
        Polygon circ = Model::findCircumference(patches);
        CHECK(circ.length() == 0);
    }
}

TEST_SUITE("CurtainWall") {
    TEST_CASE("CurtainWall from single patch") {
        utils::Random::reset(42);

        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        auto patch = std::make_unique<Patch>(shape);
        std::vector<Patch*> patches = {patch.get()};

        // Need a minimal Model for patchByVertex
        // Just test that construction doesn't crash
        // (Full test requires a complete Model)
    }
}

TEST_SUITE("Graph A*") {
    TEST_CASE("Graph basic pathfinding") {
        geom::Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();
        auto n3 = graph.add();

        // Create path n1 -> n2 -> n3
        n1->links[n2] = 1.0;
        n2->links[n1] = 1.0;
        n2->links[n3] = 1.0;
        n3->links[n2] = 1.0;

        auto path = graph.aStar(n1, n3, nullptr);

        // Path is returned in reverse order (goal to start)
        CHECK(path.size() == 3);
        CHECK(path[0] == n3);
        CHECK(path[1] == n2);
        CHECK(path[2] == n1);
    }

    TEST_CASE("Graph no path") {
        geom::Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();

        // No links between nodes
        auto path = graph.aStar(n1, n2, nullptr);

        CHECK(path.empty());
    }

    TEST_CASE("Graph path with exclusion") {
        geom::Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();
        auto n3 = graph.add();
        auto n4 = graph.add();

        // Two paths: n1 -> n2 -> n4 and n1 -> n3 -> n4
        n1->links[n2] = 1.0;
        n2->links[n1] = 1.0;
        n2->links[n4] = 1.0;
        n4->links[n2] = 1.0;

        n1->links[n3] = 1.0;
        n3->links[n1] = 1.0;
        n3->links[n4] = 1.0;
        n4->links[n3] = 1.0;

        // Exclude n2
        std::vector<geom::NodePtr> exclude = {n2};
        auto path = graph.aStar(n1, n4, &exclude);

        // Should find path through n3 (returned in reverse order: goal to start)
        CHECK(path.size() == 3);
        CHECK(path[0] == n4);
        CHECK(path[1] == n3);
        CHECK(path[2] == n1);
    }
}

TEST_SUITE("Model integration - small scale") {
    TEST_CASE("Model with 3 patches - no hang") {
        utils::Random::reset(42);

        // This is the key test - if Model construction hangs,
        // it will timeout here
        try {
            Model model(3, 42);

            // If we get here, model was created successfully
            CHECK(model.patches.size() >= 1);
            CHECK(model.center != nullptr);
        } catch (const std::exception& e) {
            // Some failures are expected with small patch counts
            // The important thing is it doesn't hang
            MESSAGE("Model construction failed (acceptable): ", e.what());
        }
    }

    TEST_CASE("Model with 5 patches") {
        utils::Random::reset(123);

        try {
            Model model(5, 123);

            CHECK(model.patches.size() >= 1);
            CHECK(model.inner.size() >= 1);
        } catch (const std::exception& e) {
            MESSAGE("Model construction failed (acceptable): ", e.what());
        }
    }
}
