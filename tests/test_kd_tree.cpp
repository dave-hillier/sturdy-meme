#include <doctest/doctest.h>
#include <cmath>
#include <algorithm>

#include "animation/MotionMatchingKDTree.h"

using namespace MotionMatching;

static KDPoint makePoint(size_t index, float baseValue) {
    KDPoint p;
    p.poseIndex = index;
    for (size_t i = 0; i < KD_FEATURE_DIM; ++i) {
        p.features[i] = baseValue + static_cast<float>(i) * 0.1f;
    }
    return p;
}

static KDPoint makeZeroPoint(size_t index) {
    KDPoint p;
    p.poseIndex = index;
    p.features.fill(0.0f);
    return p;
}

TEST_SUITE("MotionKDTree") {
    TEST_CASE("empty tree") {
        MotionKDTree tree;
        CHECK_FALSE(tree.isBuilt());
        CHECK(tree.size() == 0);

        auto results = tree.findKNearest(makeZeroPoint(0), 5);
        CHECK(results.empty());
    }

    TEST_CASE("build with empty points") {
        MotionKDTree tree;
        tree.build({});
        CHECK_FALSE(tree.isBuilt());
    }

    TEST_CASE("build with single point") {
        MotionKDTree tree;
        std::vector<KDPoint> points = {makePoint(0, 1.0f)};
        tree.build(std::move(points));

        CHECK(tree.isBuilt());
        CHECK(tree.size() == 1);

        auto results = tree.findKNearest(makePoint(0, 1.0f), 1);
        REQUIRE(results.size() == 1);
        CHECK(results[0].poseIndex == 0);
        CHECK(results[0].squaredDistance == doctest::Approx(0.0f));
    }

    TEST_CASE("findKNearest returns nearest point") {
        MotionKDTree tree;
        std::vector<KDPoint> points;

        // Create points at different "distances" from origin
        for (size_t i = 0; i < 10; ++i) {
            KDPoint p = makeZeroPoint(i);
            float val = static_cast<float>(i) * 2.0f;
            p.features[0] = val;
            points.push_back(p);
        }

        tree.build(std::move(points));

        // Query point closest to index 3 (features[0] = 6.0)
        KDPoint query = makeZeroPoint(99);
        query.features[0] = 5.9f;

        auto results = tree.findKNearest(query, 1);
        REQUIRE(results.size() == 1);
        CHECK(results[0].poseIndex == 3);
    }

    TEST_CASE("findKNearest returns k results sorted by distance") {
        MotionKDTree tree;
        std::vector<KDPoint> points;

        for (size_t i = 0; i < 20; ++i) {
            KDPoint p = makeZeroPoint(i);
            p.features[0] = static_cast<float>(i);
            points.push_back(p);
        }

        tree.build(std::move(points));

        KDPoint query = makeZeroPoint(99);
        query.features[0] = 5.0f;

        auto results = tree.findKNearest(query, 3);
        REQUIRE(results.size() == 3);

        // Results should be sorted by distance
        for (size_t i = 1; i < results.size(); ++i) {
            CHECK(results[i].squaredDistance >= results[i - 1].squaredDistance);
        }

        // Nearest should be the point at features[0]=5.0
        CHECK(results[0].poseIndex == 5);
    }

    TEST_CASE("findKNearest with k larger than tree size") {
        MotionKDTree tree;
        std::vector<KDPoint> points;

        for (size_t i = 0; i < 3; ++i) {
            points.push_back(makePoint(i, static_cast<float>(i)));
        }

        tree.build(std::move(points));

        auto results = tree.findKNearest(makeZeroPoint(99), 10);
        CHECK(results.size() == 3);
    }

    TEST_CASE("findWithinRadius") {
        MotionKDTree tree;
        std::vector<KDPoint> points;

        // Place points along the first dimension
        for (size_t i = 0; i < 10; ++i) {
            KDPoint p = makeZeroPoint(i);
            p.features[0] = static_cast<float>(i) * 10.0f;
            points.push_back(p);
        }

        tree.build(std::move(points));

        KDPoint query = makeZeroPoint(99);
        query.features[0] = 25.0f;

        // Radius of 6.0: should find points at 20 and 30
        auto results = tree.findWithinRadius(query, 6.0f);
        CHECK(results.size() == 2);

        // Verify they are the expected points
        bool found2 = false, found3 = false;
        for (const auto& r : results) {
            if (r.poseIndex == 2) found2 = true;
            if (r.poseIndex == 3) found3 = true;
        }
        CHECK(found2);
        CHECK(found3);
    }

    TEST_CASE("findWithinRadius with zero radius") {
        MotionKDTree tree;
        std::vector<KDPoint> points;

        KDPoint p = makeZeroPoint(0);
        p.features[0] = 5.0f;
        points.push_back(p);

        tree.build(std::move(points));

        KDPoint query = makeZeroPoint(99);
        query.features[0] = 5.0f;

        // Zero radius should find only exact match
        auto results = tree.findWithinRadius(query, 0.0f);
        REQUIRE(results.size() == 1);
        CHECK(results[0].squaredDistance == doctest::Approx(0.0f));
    }

    TEST_CASE("findWithinRadius returns sorted results") {
        MotionKDTree tree;
        std::vector<KDPoint> points;

        for (size_t i = 0; i < 20; ++i) {
            KDPoint p = makeZeroPoint(i);
            p.features[0] = static_cast<float>(i);
            points.push_back(p);
        }

        tree.build(std::move(points));

        KDPoint query = makeZeroPoint(99);
        query.features[0] = 10.0f;

        auto results = tree.findWithinRadius(query, 3.5f);
        for (size_t i = 1; i < results.size(); ++i) {
            CHECK(results[i].squaredDistance >= results[i - 1].squaredDistance);
        }
    }

    TEST_CASE("clear resets tree") {
        MotionKDTree tree;
        tree.build({makePoint(0, 1.0f)});
        CHECK(tree.isBuilt());

        tree.clear();
        CHECK_FALSE(tree.isBuilt());
        CHECK(tree.size() == 0);
    }

    TEST_CASE("KDPoint squaredDistance") {
        KDPoint a = makeZeroPoint(0);
        KDPoint b = makeZeroPoint(1);

        a.features[0] = 3.0f;
        b.features[0] = 0.0f;
        a.features[1] = 4.0f;
        b.features[1] = 0.0f;

        // 3^2 + 4^2 = 25
        CHECK(a.squaredDistance(b) == doctest::Approx(25.0f));
    }

    TEST_CASE("KDPoint squaredDistance is symmetric") {
        KDPoint a = makePoint(0, 1.0f);
        KDPoint b = makePoint(1, 2.0f);

        CHECK(a.squaredDistance(b) == doctest::Approx(b.squaredDistance(a)));
    }

    TEST_CASE("large dataset still finds correct nearest") {
        MotionKDTree tree;
        std::vector<KDPoint> points;

        // Create 500 points with varying features
        for (size_t i = 0; i < 500; ++i) {
            KDPoint p = makeZeroPoint(i);
            p.features[0] = static_cast<float>(i % 50);
            p.features[1] = static_cast<float>(i / 50);
            points.push_back(p);
        }

        tree.build(std::move(points));

        // Query for a known point
        KDPoint query = makeZeroPoint(99);
        query.features[0] = 25.0f;
        query.features[1] = 5.0f;

        auto results = tree.findKNearest(query, 1);
        REQUIRE(results.size() == 1);
        // Point at index 275 has features[0]=25, features[1]=5
        CHECK(results[0].poseIndex == 275);
        CHECK(results[0].squaredDistance == doctest::Approx(0.0f));
    }
}
