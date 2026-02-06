#include <doctest/doctest.h>

#include "debug/Flamegraph.h"

// ============================================================================
// FlamegraphNode tests
// ============================================================================

TEST_SUITE("FlamegraphNode") {
    TEST_CASE("endMs computes correctly") {
        FlamegraphNode node;
        node.startMs = 10.0f;
        node.durationMs = 5.0f;
        CHECK(node.endMs() == doctest::Approx(15.0f));
    }

    TEST_CASE("maxDepth of leaf is 0") {
        FlamegraphNode node;
        node.name = "leaf";
        CHECK(node.maxDepth() == 0);
    }

    TEST_CASE("maxDepth with children") {
        FlamegraphNode root;
        root.name = "root";

        FlamegraphNode child;
        child.name = "child";

        FlamegraphNode grandchild;
        grandchild.name = "grandchild";

        child.children.push_back(grandchild);
        root.children.push_back(child);

        CHECK(root.maxDepth() == 2);
    }
}

// ============================================================================
// FlamegraphCapture tests
// ============================================================================

TEST_SUITE("FlamegraphCapture") {
    TEST_CASE("isEmpty on default") {
        FlamegraphCapture capture;
        CHECK(capture.isEmpty());
        CHECK(capture.maxDepth() == 0);
    }

    TEST_CASE("maxDepth with roots") {
        FlamegraphCapture capture;

        FlamegraphNode root;
        root.name = "root";

        FlamegraphNode child;
        child.name = "child";
        root.children.push_back(child);

        capture.roots.push_back(root);
        CHECK(capture.maxDepth() == 2);
    }
}

// ============================================================================
// FlamegraphBuilder tests
// ============================================================================

TEST_SUITE("FlamegraphBuilder") {
    TEST_CASE("basic zone recording") {
        FlamegraphBuilder builder;
        builder.beginFrame();
        builder.beginZone("TestZone", 0.0f);
        builder.endZone("TestZone", 5.0f);
        auto capture = builder.endFrame(10.0f, 1);

        CHECK_FALSE(capture.isEmpty());
        CHECK(capture.totalTimeMs == doctest::Approx(10.0f));
        CHECK(capture.frameNumber == 1);

        REQUIRE(capture.roots.size() == 1);
        CHECK(capture.roots[0].name == "TestZone");
        CHECK(capture.roots[0].durationMs == doctest::Approx(5.0f));
        CHECK(capture.roots[0].startMs == doctest::Approx(0.0f));
    }

    TEST_CASE("nested zones create hierarchy") {
        FlamegraphBuilder builder;
        builder.beginFrame();

        builder.beginZone("Parent", 0.0f);
        builder.beginZone("Child", 1.0f);
        builder.endZone("Child", 3.0f);
        builder.endZone("Parent", 5.0f);

        auto capture = builder.endFrame(10.0f, 2);

        REQUIRE(capture.roots.size() == 1);
        CHECK(capture.roots[0].name == "Parent");
        CHECK(capture.roots[0].durationMs == doctest::Approx(5.0f));

        REQUIRE(capture.roots[0].children.size() == 1);
        CHECK(capture.roots[0].children[0].name == "Child");
        CHECK(capture.roots[0].children[0].durationMs == doctest::Approx(2.0f));
    }

    TEST_CASE("multiple root zones") {
        FlamegraphBuilder builder;
        builder.beginFrame();

        builder.beginZone("Zone1", 0.0f);
        builder.endZone("Zone1", 3.0f);

        builder.beginZone("Zone2", 3.0f);
        builder.endZone("Zone2", 7.0f);

        auto capture = builder.endFrame(10.0f, 3);

        REQUIRE(capture.roots.size() == 2);
        CHECK(capture.roots[0].name == "Zone1");
        CHECK(capture.roots[0].durationMs == doctest::Approx(3.0f));
        CHECK(capture.roots[1].name == "Zone2");
        CHECK(capture.roots[1].durationMs == doctest::Approx(4.0f));
    }

    TEST_CASE("wait zone detection") {
        FlamegraphBuilder builder;
        builder.beginFrame();

        builder.beginZone("Wait:FenceSync", 0.0f, true);
        builder.endZone("Wait:FenceSync", 2.0f);

        auto capture = builder.endFrame(5.0f, 4);

        REQUIRE(capture.roots.size() == 1);
        CHECK(capture.roots[0].isWaitZone);
        CHECK(capture.roots[0].colorHint == FlamegraphColorHint::Wait);
    }

    TEST_CASE("color hints from zone names") {
        FlamegraphBuilder builder;
        builder.beginFrame();

        builder.beginZone("ShadowPass", 0.0f);
        builder.endZone("ShadowPass", 1.0f);

        builder.beginZone("WaterRender", 1.0f);
        builder.endZone("WaterRender", 2.0f);

        builder.beginZone("TerrainDraw", 2.0f);
        builder.endZone("TerrainDraw", 3.0f);

        builder.beginZone("PostFX", 3.0f);
        builder.endZone("PostFX", 4.0f);

        builder.beginZone("BloomPass", 4.0f);
        builder.endZone("BloomPass", 5.0f);

        auto capture = builder.endFrame(5.0f, 5);

        REQUIRE(capture.roots.size() == 5);
        CHECK(capture.roots[0].colorHint == FlamegraphColorHint::Shadow);
        CHECK(capture.roots[1].colorHint == FlamegraphColorHint::Water);
        CHECK(capture.roots[2].colorHint == FlamegraphColorHint::Terrain);
        CHECK(capture.roots[3].colorHint == FlamegraphColorHint::PostProcess);
        CHECK(capture.roots[4].colorHint == FlamegraphColorHint::PostProcess); // "Bloom" matches PostProcess
    }

    TEST_CASE("isActive reflects frame state") {
        FlamegraphBuilder builder;
        CHECK_FALSE(builder.isActive());

        builder.beginFrame();
        CHECK(builder.isActive());

        builder.endFrame(0.0f, 0);
        CHECK_FALSE(builder.isActive());
    }

    TEST_CASE("zones outside frame are ignored") {
        FlamegraphBuilder builder;
        // No beginFrame - zones should be ignored
        builder.beginZone("Orphan", 0.0f);
        builder.endZone("Orphan", 5.0f);

        builder.beginFrame();
        auto capture = builder.endFrame(0.0f, 0);
        CHECK(capture.isEmpty());
    }
}

// ============================================================================
// FlamegraphHistory tests
// ============================================================================

TEST_SUITE("FlamegraphHistory") {
    TEST_CASE("initially empty") {
        FlamegraphHistory<5> history;
        CHECK(history.count() == 0);
        CHECK(history.capacity() == 5);
        CHECK(history.latest() == nullptr);
        CHECK(history.get(0) == nullptr);
    }

    TEST_CASE("push and get") {
        FlamegraphHistory<5> history;

        FlamegraphCapture cap;
        cap.frameNumber = 42;
        cap.totalTimeMs = 16.0f;
        history.push(std::move(cap));

        CHECK(history.count() == 1);
        const auto* latest = history.latest();
        REQUIRE(latest != nullptr);
        CHECK(latest->frameNumber == 42);
        CHECK(latest->totalTimeMs == doctest::Approx(16.0f));
    }

    TEST_CASE("get(0) returns most recent") {
        FlamegraphHistory<5> history;

        for (uint64_t i = 0; i < 3; ++i) {
            FlamegraphCapture cap;
            cap.frameNumber = i;
            history.push(std::move(cap));
        }

        CHECK(history.get(0)->frameNumber == 2);
        CHECK(history.get(1)->frameNumber == 1);
        CHECK(history.get(2)->frameNumber == 0);
        CHECK(history.get(3) == nullptr);
    }

    TEST_CASE("ring buffer wraps around") {
        FlamegraphHistory<3> history;

        for (uint64_t i = 0; i < 5; ++i) {
            FlamegraphCapture cap;
            cap.frameNumber = i;
            history.push(std::move(cap));
        }

        CHECK(history.count() == 3);
        CHECK(history.get(0)->frameNumber == 4); // Most recent
        CHECK(history.get(1)->frameNumber == 3);
        CHECK(history.get(2)->frameNumber == 2);
    }

    TEST_CASE("clear resets state") {
        FlamegraphHistory<5> history;

        for (int i = 0; i < 3; ++i) {
            FlamegraphCapture cap;
            history.push(std::move(cap));
        }
        CHECK(history.count() == 3);

        history.clear();
        CHECK(history.count() == 0);
        CHECK(history.latest() == nullptr);
    }
}
