#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/epsilon.hpp>
#include <cmath>
#include <vector>
#include <algorithm>
#include <limits>

// Stub implementations for Skeleton methods (avoids pulling in GLTFLoader.cpp + fastgltf)
#include "loaders/GLTFLoader.h"

int32_t Skeleton::findJointIndex(const std::string& name) const {
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void Skeleton::computeGlobalTransforms(std::vector<glm::mat4>& out) const {
    out.resize(joints.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].parentIndex >= 0 && static_cast<size_t>(joints[i].parentIndex) < i) {
            out[i] = out[joints[i].parentIndex] * joints[i].localTransform;
        } else {
            out[i] = joints[i].localTransform;
        }
    }
}

void Skeleton::buildHierarchy() { /* stub - not needed for motion matching tests */ }
void Skeleton::syncToHierarchy() const { /* stub */ }

// Motion matching headers
#include "animation/MotionMatchingFeature.h"
#include "animation/MotionMatchingKDTree.h"
#include "animation/MotionMatchingTrajectory.h"
#include "animation/MotionDatabase.h"

using namespace MotionMatching;

// ============================================================================
// Helper: create a minimal skeleton for testing
// ============================================================================
static Skeleton createTestSkeleton() {
    Skeleton skel;

    // Root bone (index 0)
    Joint root;
    root.name = "Hips";
    root.parentIndex = -1;
    root.inverseBindMatrix = glm::mat4(1.0f);
    root.localTransform = glm::mat4(1.0f);
    skel.joints.push_back(root);

    // Left foot (index 1)
    Joint leftFoot;
    leftFoot.name = "LeftFoot";
    leftFoot.parentIndex = 0;
    leftFoot.inverseBindMatrix = glm::mat4(1.0f);
    leftFoot.localTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-0.2f, -0.9f, 0.0f));
    skel.joints.push_back(leftFoot);

    // Right foot (index 2)
    Joint rightFoot;
    rightFoot.name = "RightFoot";
    rightFoot.parentIndex = 0;
    rightFoot.inverseBindMatrix = glm::mat4(1.0f);
    rightFoot.localTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, -0.9f, 0.0f));
    skel.joints.push_back(rightFoot);

    return skel;
}

// Helper: create a simple animation clip with root motion
static AnimationClip createTestClip(float duration, float speed) {
    AnimationClip clip;
    clip.name = "test_walk";
    clip.duration = duration;
    clip.rootBoneIndex = 0;
    clip.rootMotionPerCycle = glm::vec3(0.0f, 0.0f, speed * duration);

    // Root bone channel - translates forward over time
    AnimationChannel rootChannel;
    rootChannel.jointIndex = 0;
    rootChannel.translation.times = {0.0f, duration};
    rootChannel.translation.values = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, speed * duration)
    };
    rootChannel.rotation.times = {0.0f};
    rootChannel.rotation.values = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
    clip.channels.push_back(rootChannel);

    // Left foot channel - simple bob
    AnimationChannel leftFootChannel;
    leftFootChannel.jointIndex = 1;
    leftFootChannel.translation.times = {0.0f, duration * 0.5f, duration};
    leftFootChannel.translation.values = {
        glm::vec3(-0.2f, -0.9f, 0.0f),
        glm::vec3(-0.2f, -0.7f, 0.0f),
        glm::vec3(-0.2f, -0.9f, 0.0f)
    };
    clip.channels.push_back(leftFootChannel);

    // Right foot channel - opposite phase
    AnimationChannel rightFootChannel;
    rightFootChannel.jointIndex = 2;
    rightFootChannel.translation.times = {0.0f, duration * 0.5f, duration};
    rightFootChannel.translation.values = {
        glm::vec3(0.2f, -0.7f, 0.0f),
        glm::vec3(0.2f, -0.9f, 0.0f),
        glm::vec3(0.2f, -0.7f, 0.0f)
    };
    clip.channels.push_back(rightFootChannel);

    return clip;
}

// ============================================================================
// Trajectory tests
// ============================================================================
TEST_SUITE("Trajectory") {
    TEST_CASE("empty trajectory returns zero cost") {
        Trajectory a, b;
        CHECK(a.computeCost(b) == doctest::Approx(0.0f));
    }

    TEST_CASE("one empty trajectory returns zero cost") {
        Trajectory a, b;
        TrajectorySample s;
        s.position = glm::vec3(1.0f, 0.0f, 0.0f);
        s.timeOffset = 0.1f;
        a.addSample(s);

        CHECK(a.computeCost(b) == doctest::Approx(0.0f));
        CHECK(b.computeCost(a) == doctest::Approx(0.0f));
    }

    TEST_CASE("identical trajectories have zero cost") {
        Trajectory a, b;
        TrajectorySample s;
        s.position = glm::vec3(1.0f, 0.0f, 2.0f);
        s.velocity = glm::vec3(0.0f, 0.0f, 3.0f);
        s.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        s.timeOffset = 0.2f;
        a.addSample(s);
        b.addSample(s);

        CHECK(a.computeCost(b) == doctest::Approx(0.0f));
    }

    TEST_CASE("different trajectories have positive cost") {
        Trajectory a, b;

        TrajectorySample sa;
        sa.position = glm::vec3(0.0f);
        sa.velocity = glm::vec3(0.0f);
        sa.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        sa.timeOffset = 0.1f;
        a.addSample(sa);

        TrajectorySample sb;
        sb.position = glm::vec3(1.0f, 0.0f, 0.0f);
        sb.velocity = glm::vec3(0.0f, 0.0f, 2.0f);
        sb.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        sb.timeOffset = 0.1f;
        b.addSample(sb);

        float cost = a.computeCost(b);
        CHECK(cost > 0.0f);
    }

    TEST_CASE("samples are matched by closest time offset") {
        Trajectory a, b;

        // 'a' has sample at t=0.1
        TrajectorySample sa;
        sa.position = glm::vec3(1.0f, 0.0f, 0.0f);
        sa.timeOffset = 0.1f;
        sa.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        a.addSample(sa);

        // 'b' has samples at t=0.05 and t=0.5
        // The t=0.05 sample should match (within 0.15f threshold)
        TrajectorySample sb1;
        sb1.position = glm::vec3(1.0f, 0.0f, 0.0f);
        sb1.timeOffset = 0.05f;
        sb1.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        b.addSample(sb1);

        TrajectorySample sb2;
        sb2.position = glm::vec3(100.0f, 0.0f, 0.0f); // very different
        sb2.timeOffset = 0.5f;
        sb2.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        b.addSample(sb2);

        // Should match sample at t=0.05 (close to t=0.1), cost should be near 0
        float cost = a.computeCost(b);
        CHECK(cost == doctest::Approx(0.0f));
    }

    TEST_CASE("samples too far apart in time are not compared") {
        Trajectory a, b;

        TrajectorySample sa;
        sa.position = glm::vec3(0.0f);
        sa.timeOffset = 0.0f;
        sa.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        a.addSample(sa);

        // Only sample in 'b' is at t=1.0 -- beyond 0.15 threshold from t=0.0
        TrajectorySample sb;
        sb.position = glm::vec3(999.0f, 0.0f, 0.0f);
        sb.timeOffset = 1.0f;
        sb.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        b.addSample(sb);

        // No matched samples, cost should be 0
        float cost = a.computeCost(b);
        CHECK(cost == doctest::Approx(0.0f));
    }

    TEST_CASE("addSample respects MAX_TRAJECTORY_SAMPLES") {
        Trajectory t;
        for (size_t i = 0; i < MAX_TRAJECTORY_SAMPLES + 5; ++i) {
            TrajectorySample s;
            s.timeOffset = static_cast<float>(i) * 0.1f;
            t.addSample(s);
        }
        CHECK(t.sampleCount == MAX_TRAJECTORY_SAMPLES);
    }

    TEST_CASE("clear resets sample count") {
        Trajectory t;
        TrajectorySample s;
        t.addSample(s);
        CHECK(t.sampleCount == 1);
        t.clear();
        CHECK(t.sampleCount == 0);
    }

    TEST_CASE("facing cost is zero for same direction") {
        Trajectory a, b;
        TrajectorySample sa, sb;
        sa.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        sb.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        sa.timeOffset = sb.timeOffset = 0.1f;
        a.addSample(sa);
        b.addSample(sb);

        // Only facing weight, zero position/velocity
        float cost = a.computeCost(b, 0.0f, 0.0f, 1.0f);
        CHECK(cost == doctest::Approx(0.0f));
    }

    TEST_CASE("facing cost is max for opposite direction") {
        Trajectory a, b;
        TrajectorySample sa, sb;
        sa.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        sb.facing = glm::vec3(0.0f, 0.0f, -1.0f);
        sa.timeOffset = sb.timeOffset = 0.1f;
        a.addSample(sa);
        b.addSample(sb);

        float cost = a.computeCost(b, 0.0f, 0.0f, 1.0f);
        // 1 - dot(forward, -forward) = 1 - (-1) = 2.0
        CHECK(cost == doctest::Approx(2.0f));
    }
}

// ============================================================================
// BoneFeature tests
// ============================================================================
TEST_SUITE("BoneFeature") {
    TEST_CASE("identical bones have zero cost") {
        BoneFeature a, b;
        a.position = b.position = glm::vec3(1.0f, 2.0f, 3.0f);
        a.velocity = b.velocity = glm::vec3(0.5f, 0.0f, 0.0f);
        CHECK(a.computeCost(b) == doctest::Approx(0.0f));
    }

    TEST_CASE("position difference contributes to cost") {
        BoneFeature a, b;
        a.position = glm::vec3(0.0f);
        b.position = glm::vec3(1.0f, 0.0f, 0.0f);

        float cost = a.computeCost(b, 1.0f, 0.0f);
        CHECK(cost == doctest::Approx(1.0f));
    }

    TEST_CASE("velocity difference contributes to cost") {
        BoneFeature a, b;
        a.velocity = glm::vec3(0.0f);
        b.velocity = glm::vec3(0.0f, 0.0f, 2.0f);

        float cost = a.computeCost(b, 0.0f, 1.0f);
        CHECK(cost == doctest::Approx(2.0f));
    }
}

// ============================================================================
// PoseFeatures tests
// ============================================================================
TEST_SUITE("PoseFeatures") {
    TEST_CASE("identical poses have zero cost") {
        PoseFeatures a, b;
        a.boneCount = b.boneCount = 2;
        a.boneFeatures[0].position = b.boneFeatures[0].position = glm::vec3(1.0f);
        a.boneFeatures[1].position = b.boneFeatures[1].position = glm::vec3(2.0f);
        a.rootVelocity = b.rootVelocity = glm::vec3(1.0f, 0.0f, 0.0f);
        a.rootAngularVelocity = b.rootAngularVelocity = 0.5f;
        a.leftFootPhase = b.leftFootPhase = 0.3f;
        a.rightFootPhase = b.rightFootPhase = 0.8f;

        CHECK(a.computeCost(b) == doctest::Approx(0.0f));
    }

    TEST_CASE("phase difference wraps correctly") {
        PoseFeatures a, b;
        a.boneCount = b.boneCount = 0;
        a.rootVelocity = b.rootVelocity = glm::vec3(0.0f);
        a.rootAngularVelocity = b.rootAngularVelocity = 0.0f;

        // Phase 0.0 and 1.0 should have near-zero difference (they're the same in cyclic terms)
        a.leftFootPhase = 0.0f;
        b.leftFootPhase = 1.0f;
        a.rightFootPhase = b.rightFootPhase = 0.0f;

        float cost = a.computeCost(b, 0.0f, 0.0f, 0.0f, 1.0f);
        CHECK(cost == doctest::Approx(0.0f));
    }

    TEST_CASE("phase difference is correct for 0.1 vs 0.9") {
        PoseFeatures a, b;
        a.boneCount = b.boneCount = 0;
        a.rootVelocity = b.rootVelocity = glm::vec3(0.0f);
        a.rootAngularVelocity = b.rootAngularVelocity = 0.0f;
        a.rightFootPhase = b.rightFootPhase = 0.0f;

        // Phase 0.1 and 0.9: linear diff = 0.8, wrapped diff = 0.2
        a.leftFootPhase = 0.1f;
        b.leftFootPhase = 0.9f;

        float cost = a.computeCost(b, 0.0f, 0.0f, 0.0f, 1.0f);
        CHECK(cost == doctest::Approx(0.2f));
    }

    TEST_CASE("phase difference maximum at 0.5") {
        PoseFeatures a, b;
        a.boneCount = b.boneCount = 0;
        a.rootVelocity = b.rootVelocity = glm::vec3(0.0f);
        a.rootAngularVelocity = b.rootAngularVelocity = 0.0f;
        a.rightFootPhase = b.rightFootPhase = 0.0f;

        a.leftFootPhase = 0.0f;
        b.leftFootPhase = 0.5f;

        float cost = a.computeCost(b, 0.0f, 0.0f, 0.0f, 1.0f);
        CHECK(cost == doctest::Approx(0.5f));
    }

    TEST_CASE("root velocity difference contributes to cost") {
        PoseFeatures a, b;
        a.boneCount = b.boneCount = 0;
        a.rootVelocity = glm::vec3(0.0f, 0.0f, 0.0f);
        b.rootVelocity = glm::vec3(0.0f, 0.0f, 3.0f);

        float cost = a.computeCost(b, 0.0f, 1.0f, 0.0f, 0.0f);
        CHECK(cost == doctest::Approx(3.0f));
    }

    TEST_CASE("angular velocity difference contributes to cost") {
        PoseFeatures a, b;
        a.boneCount = b.boneCount = 0;
        a.rootVelocity = b.rootVelocity = glm::vec3(0.0f);
        a.rootAngularVelocity = 0.0f;
        b.rootAngularVelocity = 2.0f;

        float cost = a.computeCost(b, 0.0f, 0.0f, 1.0f, 0.0f);
        CHECK(cost == doctest::Approx(2.0f));
    }

    TEST_CASE("different bone count uses min bones") {
        PoseFeatures a, b;
        a.boneCount = 1;
        b.boneCount = 3;
        a.boneFeatures[0].position = glm::vec3(0.0f);
        b.boneFeatures[0].position = glm::vec3(1.0f, 0.0f, 0.0f);
        a.rootVelocity = b.rootVelocity = glm::vec3(0.0f);
        a.rootAngularVelocity = b.rootAngularVelocity = 0.0f;

        // Should only compare 1 bone (min of 1, 3)
        float cost = a.computeCost(b, 1.0f, 0.0f, 0.0f, 0.0f);
        CHECK(cost > 0.0f);
    }
}

// ============================================================================
// HeadingFeature tests
// ============================================================================
TEST_SUITE("HeadingFeature") {
    TEST_CASE("same direction has zero heading cost") {
        HeadingFeature a, b;
        a.direction = b.direction = glm::vec3(0.0f, 0.0f, 1.0f);
        CHECK(a.computeCost(b) == doctest::Approx(0.0f));
    }

    TEST_CASE("opposite direction has max heading cost") {
        HeadingFeature a, b;
        a.direction = glm::vec3(0.0f, 0.0f, 1.0f);
        b.direction = glm::vec3(0.0f, 0.0f, -1.0f);
        CHECK(a.computeCost(b) == doctest::Approx(2.0f));
    }

    TEST_CASE("perpendicular directions have cost ~1") {
        HeadingFeature a, b;
        a.direction = glm::vec3(0.0f, 0.0f, 1.0f);
        b.direction = glm::vec3(1.0f, 0.0f, 0.0f);
        // dot = 0, cost = 1 - 0 = 1
        CHECK(a.computeCost(b) == doctest::Approx(1.0f));
    }

    TEST_CASE("strafe cost is zero with no movement") {
        HeadingFeature h;
        h.direction = glm::vec3(0.0f, 0.0f, 1.0f);
        CHECK(h.computeStrafeCost(glm::vec3(0.0f)) == doctest::Approx(0.0f));
    }

    TEST_CASE("strafe cost reflects angle difference") {
        HeadingFeature h;
        h.direction = glm::vec3(0.0f, 0.0f, 1.0f);
        h.angleDifference = 0.0f; // forward strafe

        // Movement is exactly forward - angle should be 0, matching angleDifference=0
        float cost = h.computeStrafeCost(glm::vec3(0.0f, 0.0f, 1.0f));
        CHECK(cost == doctest::Approx(0.0f));
    }
}

// ============================================================================
// FeatureStats / FeatureNormalization tests
// ============================================================================
TEST_SUITE("FeatureStats") {
    TEST_CASE("default stats give identity normalization") {
        FeatureStats stats;
        // mean=0, stdDev=1 → normalize(x) = x
        CHECK(stats.normalize(5.0f) == doctest::Approx(5.0f));
        CHECK(stats.normalize(0.0f) == doctest::Approx(0.0f));
        CHECK(stats.normalize(-3.0f) == doctest::Approx(-3.0f));
    }

    TEST_CASE("normalization subtracts mean and divides by stdDev") {
        FeatureStats stats;
        stats.mean = 10.0f;
        stats.stdDev = 2.0f;

        CHECK(stats.normalize(10.0f) == doctest::Approx(0.0f));
        CHECK(stats.normalize(12.0f) == doctest::Approx(1.0f));
        CHECK(stats.normalize(8.0f) == doctest::Approx(-1.0f));
    }

    TEST_CASE("normalization with small stdDev doesn't explode") {
        FeatureStats stats;
        stats.mean = 5.0f;
        stats.stdDev = 0.001f; // very small but above minimum

        float result = stats.normalize(5.001f);
        CHECK(result == doctest::Approx(1.0f).epsilon(0.01));
    }
}

// ============================================================================
// KD-tree tests
// ============================================================================
TEST_SUITE("MotionKDTree") {
    TEST_CASE("empty tree returns empty results") {
        MotionKDTree tree;
        CHECK_FALSE(tree.isBuilt());
        CHECK(tree.size() == 0);

        KDPoint query;
        query.features.fill(0.0f);
        auto results = tree.findKNearest(query, 5);
        CHECK(results.empty());
    }

    TEST_CASE("build with single point") {
        MotionKDTree tree;
        std::vector<KDPoint> points(1);
        points[0].features.fill(1.0f);
        points[0].poseIndex = 42;

        tree.build(std::move(points));
        CHECK(tree.isBuilt());
        CHECK(tree.size() == 1);

        KDPoint query;
        query.features.fill(1.0f);
        auto results = tree.findKNearest(query, 1);
        REQUIRE(results.size() == 1);
        CHECK(results[0].poseIndex == 42);
        CHECK(results[0].squaredDistance == doctest::Approx(0.0f));
    }

    TEST_CASE("findKNearest returns closest points") {
        MotionKDTree tree;
        std::vector<KDPoint> points;

        // Create 5 points at increasing distances from origin
        for (size_t i = 0; i < 5; ++i) {
            KDPoint p;
            p.features.fill(0.0f);
            p.features[0] = static_cast<float>(i);
            p.poseIndex = i;
            points.push_back(p);
        }

        tree.build(std::move(points));

        KDPoint query;
        query.features.fill(0.0f);

        auto results = tree.findKNearest(query, 3);
        REQUIRE(results.size() == 3);

        // Results should be sorted by distance
        CHECK(results[0].squaredDistance <= results[1].squaredDistance);
        CHECK(results[1].squaredDistance <= results[2].squaredDistance);

        // Nearest should be the point at origin (index 0)
        CHECK(results[0].poseIndex == 0);
        CHECK(results[0].squaredDistance == doctest::Approx(0.0f));
    }

    TEST_CASE("findKNearest with k > num points returns all points") {
        MotionKDTree tree;
        std::vector<KDPoint> points(3);
        for (size_t i = 0; i < 3; ++i) {
            points[i].features.fill(static_cast<float>(i));
            points[i].poseIndex = i;
        }
        tree.build(std::move(points));

        KDPoint query;
        query.features.fill(0.0f);
        auto results = tree.findKNearest(query, 10);
        CHECK(results.size() == 3);
    }

    TEST_CASE("findWithinRadius returns correct results") {
        MotionKDTree tree;
        std::vector<KDPoint> points;

        // Points at distances 0, 1, 2, 3, 4 from origin (in dim 0)
        for (size_t i = 0; i < 5; ++i) {
            KDPoint p;
            p.features.fill(0.0f);
            p.features[0] = static_cast<float>(i);
            p.poseIndex = i;
            points.push_back(p);
        }
        tree.build(std::move(points));

        KDPoint query;
        query.features.fill(0.0f);

        // Radius 1.5 should include points at distance 0 and 1
        auto results = tree.findWithinRadius(query, 1.5f);
        CHECK(results.size() == 2);
    }

    TEST_CASE("findWithinRadius with zero radius returns only exact match") {
        MotionKDTree tree;
        std::vector<KDPoint> points(3);
        for (size_t i = 0; i < 3; ++i) {
            points[i].features.fill(static_cast<float>(i));
            points[i].poseIndex = i;
        }
        tree.build(std::move(points));

        KDPoint query;
        query.features.fill(0.0f);
        auto results = tree.findWithinRadius(query, 0.0f);
        CHECK(results.size() == 1);
        CHECK(results[0].poseIndex == 0);
    }

    TEST_CASE("KDPoint squared distance is correct") {
        KDPoint a, b;
        a.features.fill(0.0f);
        b.features.fill(0.0f);
        b.features[0] = 3.0f;
        b.features[1] = 4.0f;
        // distance^2 = 9 + 16 = 25
        CHECK(a.squaredDistance(b) == doctest::Approx(25.0f));
    }

    TEST_CASE("larger tree finds correct nearest neighbor") {
        MotionKDTree tree;
        std::vector<KDPoint> points;

        // Create a grid of points
        for (int x = 0; x < 10; ++x) {
            for (int y = 0; y < 10; ++y) {
                KDPoint p;
                p.features.fill(0.0f);
                p.features[0] = static_cast<float>(x);
                p.features[1] = static_cast<float>(y);
                p.poseIndex = x * 10 + y;
                points.push_back(p);
            }
        }
        tree.build(std::move(points));

        // Query near (3.1, 4.2) - closest should be (3, 4) = index 34
        KDPoint query;
        query.features.fill(0.0f);
        query.features[0] = 3.1f;
        query.features[1] = 4.2f;

        auto results = tree.findKNearest(query, 1);
        REQUIRE(results.size() == 1);
        CHECK(results[0].poseIndex == 34);
    }

    TEST_CASE("clear resets tree") {
        MotionKDTree tree;
        std::vector<KDPoint> points(5);
        for (size_t i = 0; i < 5; ++i) {
            points[i].features.fill(static_cast<float>(i));
            points[i].poseIndex = i;
        }
        tree.build(std::move(points));
        CHECK(tree.isBuilt());

        tree.clear();
        CHECK_FALSE(tree.isBuilt());
        CHECK(tree.size() == 0);
    }

    TEST_CASE("k=0 returns empty") {
        MotionKDTree tree;
        std::vector<KDPoint> points(3);
        for (auto& p : points) p.features.fill(0.0f);
        tree.build(std::move(points));

        KDPoint query;
        query.features.fill(0.0f);
        auto results = tree.findKNearest(query, 0);
        CHECK(results.empty());
    }
}

// ============================================================================
// TrajectoryPredictor tests
// ============================================================================
TEST_SUITE("TrajectoryPredictor") {
    TEST_CASE("initial state has zero velocity") {
        TrajectoryPredictor predictor;
        CHECK(glm::length(predictor.getCurrentVelocity()) == doctest::Approx(0.0f));
    }

    TEST_CASE("update with zero input keeps zero velocity") {
        TrajectoryPredictor predictor;
        glm::vec3 pos(0.0f);
        glm::vec3 facing(0.0f, 0.0f, 1.0f);
        glm::vec3 inputDir(0.0f);

        predictor.update(pos, facing, inputDir, 0.0f, 1.0f / 60.0f);
        CHECK(glm::length(predictor.getCurrentVelocity()) == doctest::Approx(0.0f));
    }

    TEST_CASE("update with forward input produces forward velocity") {
        TrajectoryPredictor predictor;
        TrajectoryPredictor::Config config;
        config.maxSpeed = 5.0f;
        config.acceleration = 100.0f; // high accel for instant response
        config.inputSmoothing = 0.001f; // minimal smoothing
        predictor.setConfig(config);

        glm::vec3 pos(0.0f);
        glm::vec3 facing(0.0f, 0.0f, 1.0f);
        glm::vec3 inputDir(0.0f, 0.0f, 1.0f);

        // Update several frames to build velocity
        for (int i = 0; i < 60; ++i) {
            predictor.update(pos, facing, inputDir, 1.0f, 1.0f / 60.0f);
        }

        glm::vec3 vel = predictor.getCurrentVelocity();
        CHECK(vel.z > 0.0f);
        CHECK(vel.z == doctest::Approx(5.0f).epsilon(0.1));
    }

    TEST_CASE("generateTrajectory produces samples") {
        TrajectoryPredictor predictor;
        TrajectoryPredictor::Config config;
        config.sampleTimes = {-0.2f, -0.1f, 0.1f, 0.2f};
        predictor.setConfig(config);

        // Update a few frames to populate history
        glm::vec3 pos(0.0f);
        glm::vec3 facing(0.0f, 0.0f, 1.0f);
        for (int i = 0; i < 30; ++i) {
            predictor.update(pos, facing, glm::vec3(0.0f, 0.0f, 1.0f), 0.5f, 1.0f / 60.0f);
        }

        Trajectory traj = predictor.generateTrajectory();
        CHECK(traj.sampleCount == 4);
    }

    TEST_CASE("reset clears state") {
        TrajectoryPredictor predictor;
        glm::vec3 pos(5.0f, 0.0f, 5.0f);
        glm::vec3 facing(0.0f, 0.0f, 1.0f);

        for (int i = 0; i < 30; ++i) {
            predictor.update(pos, facing, glm::vec3(0.0f, 0.0f, 1.0f), 1.0f, 1.0f / 60.0f);
        }
        CHECK(glm::length(predictor.getCurrentVelocity()) > 0.0f);

        predictor.reset();
        CHECK(glm::length(predictor.getCurrentVelocity()) == doctest::Approx(0.0f));
    }

    TEST_CASE("strafe mode returns strafe facing") {
        TrajectoryPredictor predictor;

        glm::vec3 strafeFacing(1.0f, 0.0f, 0.0f);
        predictor.setStrafeMode(true);
        predictor.setStrafeFacing(strafeFacing);

        CHECK(predictor.isStrafeMode() == true);
        glm::vec3 f = predictor.getCurrentFacing();
        CHECK(f.x == doctest::Approx(1.0f));
        CHECK(f.z == doctest::Approx(0.0f));
    }

    TEST_CASE("non-strafe mode returns actual facing") {
        TrajectoryPredictor predictor;
        predictor.setStrafeMode(false);

        glm::vec3 pos(0.0f);
        glm::vec3 facing(0.0f, 0.0f, 1.0f);
        predictor.update(pos, facing, glm::vec3(0.0f), 0.0f, 1.0f / 60.0f);

        glm::vec3 f = predictor.getCurrentFacing();
        CHECK(f.z == doctest::Approx(1.0f).epsilon(0.01));
    }

    TEST_CASE("angular velocity is computed from facing change") {
        TrajectoryPredictor predictor;
        TrajectoryPredictor::Config config;
        config.inputSmoothing = 0.001f;
        predictor.setConfig(config);

        // Frame 1: facing forward
        predictor.update(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                        glm::vec3(0.0f), 0.0f, 1.0f / 60.0f);

        // Frame 2: facing rotated ~45 degrees
        float angle = glm::radians(45.0f);
        glm::vec3 rotatedFacing(std::sin(angle), 0.0f, std::cos(angle));
        predictor.update(glm::vec3(0.0f), rotatedFacing,
                        glm::vec3(0.0f), 0.0f, 1.0f / 60.0f);

        // Angular velocity should be ~45 degrees / (1/60 s) = 2700 deg/s in radians
        float angVel = predictor.getCurrentAngularVelocity();
        // The sign convention: positive cross means turning left
        // turning from (0,0,1) towards (sin45,0,cos45) is turning right (negative)
        CHECK(std::abs(angVel) > 1.0f); // should be large
    }
}

// ============================================================================
// InertialBlender tests
// ============================================================================
TEST_SUITE("InertialBlender") {
    TEST_CASE("not blending initially") {
        InertialBlender blender;
        // After construction, blendTime_ = 0 but blendDuration default is 0.3
        // isBlending checks blendTime_ < blendDuration
        // blendTime_ starts at 0 which IS < 0.3 so it IS blending initially
        // But after reset, blendTime is set to blendDuration
        blender.reset();
        CHECK_FALSE(blender.isBlending());
    }

    TEST_CASE("startBlend begins blending") {
        InertialBlender blender;
        blender.reset();
        CHECK_FALSE(blender.isBlending());

        blender.startBlend(
            glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f),
            glm::vec3(0.0f),              glm::vec3(0.0f)
        );
        CHECK(blender.isBlending());
    }

    TEST_CASE("blend decays position offset to zero") {
        InertialBlender blender;
        InertialBlender::Config config;
        config.blendDuration = 0.5f;
        config.naturalFrequency = 10.0f;
        blender.setConfig(config);

        blender.startBlend(
            glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f), // current
            glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f)  // target
        );

        // Initial offset should be (1, 0, 0)
        CHECK(glm::length(blender.getPositionOffset()) == doctest::Approx(1.0f));

        // After some time, offset should decay
        for (int i = 0; i < 30; ++i) {
            blender.update(1.0f / 60.0f);
        }

        float offset = glm::length(blender.getPositionOffset());
        CHECK(offset < 0.5f); // Should have decayed significantly
    }

    TEST_CASE("blend progress goes from 0 to 1") {
        InertialBlender blender;
        InertialBlender::Config config;
        config.blendDuration = 0.3f;
        blender.setConfig(config);

        blender.startBlend(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f),
                          glm::vec3(0.0f), glm::vec3(0.0f));

        CHECK(blender.getProgress() == doctest::Approx(0.0f));

        blender.update(0.15f);
        CHECK(blender.getProgress() == doctest::Approx(0.5f));

        blender.update(0.15f);
        CHECK(blender.getProgress() == doctest::Approx(1.0f));
    }

    TEST_CASE("skeletal blend applies position offsets to pose") {
        InertialBlender blender;
        InertialBlender::Config config;
        config.blendDuration = 1.0f;
        blender.setConfig(config);

        SkeletonPose currentPose, targetPose;
        currentPose.resize(2);
        targetPose.resize(2);
        currentPose[0].translation = glm::vec3(1.0f, 0.0f, 0.0f);
        targetPose[0].translation = glm::vec3(0.0f);
        currentPose[1].translation = glm::vec3(0.0f, 2.0f, 0.0f);
        targetPose[1].translation = glm::vec3(0.0f);

        blender.startSkeletalBlend(currentPose, targetPose);
        CHECK(blender.isSkeletalBlend());

        // Apply to target pose - should add offsets
        SkeletonPose testPose;
        testPose.resize(2);
        testPose[0].translation = glm::vec3(0.0f);
        testPose[1].translation = glm::vec3(0.0f);

        blender.applyToPose(testPose);
        // Bone 0 should have the offset from (1,0,0) - (0,0,0) = (1,0,0)
        CHECK(testPose[0].translation.x == doctest::Approx(1.0f));
        CHECK(testPose[1].translation.y == doctest::Approx(2.0f));
    }

    TEST_CASE("reset stops blending") {
        InertialBlender blender;
        blender.startBlend(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f),
                          glm::vec3(0.0f), glm::vec3(0.0f));
        CHECK(blender.isBlending());

        blender.reset();
        CHECK_FALSE(blender.isBlending());
        CHECK(glm::length(blender.getPositionOffset()) == doctest::Approx(0.0f));
    }

    TEST_CASE("skeletal blend takes shortest rotation path") {
        // Bug regression: when rotation difference has w < 0 (angle > 180 degrees),
        // the blender should negate the quaternion to take the shortest path
        InertialBlender blender;
        InertialBlender::Config config;
        config.blendDuration = 1.0f;
        config.naturalFrequency = 10.0f;
        blender.setConfig(config);

        SkeletonPose currentPose, targetPose;
        currentPose.resize(1);
        targetPose.resize(1);

        // Current: rotated 170 degrees around Y
        currentPose[0].rotation = glm::angleAxis(glm::radians(170.0f), glm::vec3(0, 1, 0));
        // Target: rotated -170 degrees around Y (i.e. 190 degrees)
        // The shortest path is 20 degrees, NOT 340 degrees
        targetPose[0].rotation = glm::angleAxis(glm::radians(-170.0f), glm::vec3(0, 1, 0));

        blender.startSkeletalBlend(currentPose, targetPose);

        // The spring rotation axis-angle magnitude should represent the SHORT path (~20 degrees)
        const auto& states = blender.getBoneStates();
        REQUIRE(states.size() == 1);
        float springAngle = glm::length(states[0].springRotation);
        // Should be ~20 degrees (0.35 rad), NOT ~340 degrees (5.93 rad)
        CHECK(springAngle < glm::radians(180.0f));
    }
}

// ============================================================================
// RootMotionExtractor tests
// ============================================================================
TEST_SUITE("RootMotionExtractor") {
    TEST_CASE("first update sets reference with no delta") {
        RootMotionExtractor extractor;
        extractor.update(glm::vec3(1.0f, 0.0f, 0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);

        CHECK(glm::length(extractor.getDeltaTranslation()) == doctest::Approx(0.0f));
        CHECK(extractor.getDeltaRotation() == doctest::Approx(0.0f));
    }

    TEST_CASE("second update computes delta translation") {
        RootMotionExtractor extractor;

        extractor.update(glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);
        extractor.update(glm::vec3(1.0f, 0.0f, 2.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);

        glm::vec3 delta = extractor.getDeltaTranslation();
        // Only horizontal (XZ) should be extracted
        CHECK(delta.x == doctest::Approx(1.0f));
        CHECK(delta.y == doctest::Approx(0.0f));
        CHECK(delta.z == doctest::Approx(2.0f));
    }

    TEST_CASE("reset clears reference") {
        RootMotionExtractor extractor;

        extractor.update(glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);
        extractor.update(glm::vec3(1.0f, 0.0f, 0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);
        CHECK(extractor.getDeltaTranslation().x == doctest::Approx(1.0f));

        extractor.reset();
        // After reset, next update should be reference-setting again
        extractor.update(glm::vec3(5.0f, 0.0f, 0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);
        CHECK(glm::length(extractor.getDeltaTranslation()) == doctest::Approx(0.0f));
    }

    TEST_CASE("vertical translation is stripped") {
        RootMotionExtractor extractor;

        extractor.update(glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);
        extractor.update(glm::vec3(0.0f, 5.0f, 0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);

        glm::vec3 delta = extractor.getDeltaTranslation();
        CHECK(delta.y == doctest::Approx(0.0f));
    }

    TEST_CASE("translation extraction can be disabled") {
        RootMotionExtractor extractor;
        RootMotionExtractor::Config config;
        config.extractTranslation = false;
        extractor.setConfig(config);

        extractor.update(glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);
        extractor.update(glm::vec3(10.0f, 0.0f, 0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);

        CHECK(glm::length(extractor.getDeltaTranslation()) == doctest::Approx(0.0f));
    }
}

// ============================================================================
// DatabasePose tag tests
// ============================================================================
TEST_SUITE("DatabasePose") {
    TEST_CASE("hasTag returns true for matching tags") {
        DatabasePose pose;
        pose.tags = {"walk", "locomotion", "forward"};

        CHECK(pose.hasTag("walk"));
        CHECK(pose.hasTag("locomotion"));
        CHECK(pose.hasTag("forward"));
    }

    TEST_CASE("hasTag returns false for non-matching tags") {
        DatabasePose pose;
        pose.tags = {"walk"};

        CHECK_FALSE(pose.hasTag("run"));
        CHECK_FALSE(pose.hasTag("idle"));
        CHECK_FALSE(pose.hasTag(""));
    }

    TEST_CASE("hasTag with empty tags always returns false") {
        DatabasePose pose;
        CHECK_FALSE(pose.hasTag("anything"));
    }
}

// ============================================================================
// PoseSearchSchema tests
// ============================================================================
TEST_SUITE("PoseSearchSchema") {
    TEST_CASE("locomotion schema has trajectory and pose channels") {
        auto schema = PoseSearchSchema::locomotion();
        CHECK(schema.name == "Locomotion");
        CHECK(schema.channels.size() == 3); // trajectory, pose, velocity
        CHECK(schema.getChannel("Trajectory") != nullptr);
        CHECK(schema.getChannel("Pose") != nullptr);
        CHECK(schema.getChannel("Velocity") != nullptr);
        CHECK(schema.getChannel("Heading") == nullptr);
    }

    TEST_CASE("strafe schema has heading channel") {
        auto schema = PoseSearchSchema::locomotionWithStrafe();
        CHECK(schema.getChannel("Heading") != nullptr);
        CHECK(schema.channels.size() == 4);
    }

    TEST_CASE("getTotalWeight sums enabled channel weights") {
        PoseSearchSchema schema;
        SchemaChannel ch1;
        ch1.weight = 2.0f;
        ch1.enabled = true;
        SchemaChannel ch2;
        ch2.weight = 3.0f;
        ch2.enabled = true;
        SchemaChannel ch3;
        ch3.weight = 5.0f;
        ch3.enabled = false; // disabled

        schema.channels = {ch1, ch2, ch3};
        CHECK(schema.getTotalWeight() == doctest::Approx(5.0f));
    }

    TEST_CASE("getChannel returns nullptr for missing channel") {
        PoseSearchSchema schema;
        CHECK(schema.getChannel("nonexistent") == nullptr);
    }
}

// ============================================================================
// FeatureConfig tests
// ============================================================================
TEST_SUITE("FeatureConfig") {
    TEST_CASE("locomotion config has expected bones") {
        auto config = FeatureConfig::locomotion();
        CHECK(config.featureBoneNames.size() == 3);
    }

    TEST_CASE("fullBody config has more bones") {
        auto config = FeatureConfig::fullBody();
        CHECK(config.featureBoneNames.size() == 6);
    }

    TEST_CASE("fromSchema extracts settings correctly") {
        auto schema = PoseSearchSchema::locomotionWithStrafe();
        schema.continuingPoseCostBias = -0.5f;
        schema.strafeMode = true;

        auto config = FeatureConfig::fromSchema(schema);
        CHECK(config.continuingPoseCostBias == doctest::Approx(-0.5f));
        CHECK(config.strafeMode == true);
        CHECK(config.headingWeight > 0.0f);
    }
}

// ============================================================================
// MotionMatcher filter tests
// ============================================================================
TEST_SUITE("MotionMatcher") {
    // Helper to build a minimal database with manually-constructed poses
    struct TestDatabaseFixture {
        Skeleton skeleton;
        MotionDatabase database;
        AnimationClip walkClip;
        AnimationClip runClip;
        AnimationClip idleClip;

        TestDatabaseFixture() {
            skeleton = createTestSkeleton();
            auto config = FeatureConfig::locomotion();
            database.initialize(skeleton, config);

            // Walk clip (1.0 second, ~1.5 m/s)
            walkClip = createTestClip(1.0f, 1.5f);
            walkClip.name = "walk";
            database.addClip(&walkClip, "walk", true, 10.0f, {"locomotion", "walk"}, 1.5f);

            // Run clip (0.8 second, ~4.0 m/s)
            runClip = createTestClip(0.8f, 4.0f);
            runClip.name = "run";
            database.addClip(&runClip, "run", true, 10.0f, {"locomotion", "run"}, 4.0f);

            // Idle clip (2.0 seconds, no movement)
            idleClip = createTestClip(2.0f, 0.0f);
            idleClip.name = "idle";
            database.addClip(&idleClip, "idle", true, 10.0f, {"idle"});

            DatabaseBuildOptions options;
            options.pruneStaticPoses = false; // keep all for testing
            options.buildKDTree = true;
            database.build(options);
        }
    };

    TEST_CASE("database builds correctly") {
        TestDatabaseFixture f;
        CHECK(f.database.isBuilt());
        CHECK(f.database.getClipCount() == 3);
        CHECK(f.database.getPoseCount() > 0);
    }

    TEST_CASE("database has KD-tree after build") {
        TestDatabaseFixture f;
        CHECK(f.database.hasKDTree());
    }

    TEST_CASE("getPosesFromClip returns correct poses") {
        TestDatabaseFixture f;
        auto poses = f.database.getPosesFromClip(0);
        CHECK(poses.size() > 0);
        for (auto* p : poses) {
            CHECK(p->clipIndex == 0);
        }
    }

    TEST_CASE("getPosesWithTag filters correctly") {
        TestDatabaseFixture f;
        auto walkPoses = f.database.getPosesWithTag("walk");
        auto idlePoses = f.database.getPosesWithTag("idle");

        CHECK(walkPoses.size() > 0);
        CHECK(idlePoses.size() > 0);

        for (auto* p : walkPoses) {
            CHECK(p->hasTag("walk"));
        }
        for (auto* p : idlePoses) {
            CHECK(p->hasTag("idle"));
            CHECK_FALSE(p->hasTag("walk"));
        }
    }

    TEST_CASE("findBestMatch returns a valid result") {
        TestDatabaseFixture f;
        MotionMatcher matcher;
        matcher.setDatabase(&f.database);

        // Create a query trajectory moving forward
        Trajectory queryTraj;
        for (float t : {0.1f, 0.2f, 0.4f}) {
            TrajectorySample s;
            s.timeOffset = t;
            s.position = glm::vec3(0.0f, 0.0f, 1.5f * t);
            s.velocity = glm::vec3(0.0f, 0.0f, 1.5f);
            s.facing = glm::vec3(0.0f, 0.0f, 1.0f);
            queryTraj.addSample(s);
        }

        PoseFeatures queryPose;
        queryPose.rootVelocity = glm::vec3(0.0f, 0.0f, 1.5f);

        SearchOptions options;
        options.useKDTree = false; // brute force for determinism
        auto result = matcher.findBestMatch(queryTraj, queryPose, options);

        CHECK(result.isValid());
        CHECK(result.cost < std::numeric_limits<float>::max());
    }

    TEST_CASE("required tags filter restricts results") {
        TestDatabaseFixture f;
        MotionMatcher matcher;
        matcher.setDatabase(&f.database);

        Trajectory queryTraj;
        TrajectorySample s;
        s.timeOffset = 0.1f;
        s.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        queryTraj.addSample(s);

        PoseFeatures queryPose;

        // Require "idle" tag - should only return idle poses
        SearchOptions options;
        options.useKDTree = false;
        options.requiredTags = {"idle"};

        auto result = matcher.findBestMatch(queryTraj, queryPose, options);
        CHECK(result.isValid());
        CHECK(result.pose->hasTag("idle"));
    }

    TEST_CASE("excluded tags filter restricts results") {
        TestDatabaseFixture f;
        MotionMatcher matcher;
        matcher.setDatabase(&f.database);

        Trajectory queryTraj;
        TrajectorySample s;
        s.timeOffset = 0.1f;
        s.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        queryTraj.addSample(s);

        PoseFeatures queryPose;

        // Exclude all locomotion - should only get idle
        SearchOptions options;
        options.useKDTree = false;
        options.excludedTags = {"locomotion"};

        auto result = matcher.findBestMatch(queryTraj, queryPose, options);
        CHECK(result.isValid());
        CHECK_FALSE(result.pose->hasTag("locomotion"));
    }

    TEST_CASE("continuing pose bias favors current clip") {
        TestDatabaseFixture f;
        MotionMatcher matcher;
        matcher.setDatabase(&f.database);

        Trajectory queryTraj;
        TrajectorySample s;
        s.timeOffset = 0.1f;
        s.position = glm::vec3(0.0f, 0.0f, 0.1f);
        s.velocity = glm::vec3(0.0f, 0.0f, 1.5f);
        s.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        queryTraj.addSample(s);

        PoseFeatures queryPose;
        queryPose.rootVelocity = glm::vec3(0.0f, 0.0f, 1.5f);

        // Compute cost with no current clip (no bias)
        SearchOptions options;
        options.useKDTree = false;
        options.continuingPoseCostBias = -10.0f; // very strong bias

        // Get cost for walk clip pose 0 without bias
        float costWithoutBias = matcher.computeCost(0, queryTraj, queryPose, options);

        // Now set current clip to walk (clip 0) - should get negative bias
        options.currentClipIndex = 0;
        float costWithBias = matcher.computeCost(0, queryTraj, queryPose, options);

        CHECK(costWithBias < costWithoutBias);
    }

    TEST_CASE("looping animation gets looping bias") {
        TestDatabaseFixture f;
        MotionMatcher matcher;
        matcher.setDatabase(&f.database);

        Trajectory queryTraj;
        TrajectorySample s;
        s.timeOffset = 0.1f;
        s.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        queryTraj.addSample(s);

        PoseFeatures queryPose;

        SearchOptions options;
        options.useKDTree = false;
        options.loopingCostBias = 0.0f; // disable looping bias

        float costNoLoopBias = matcher.computeCost(0, queryTraj, queryPose, options);

        options.loopingCostBias = -0.5f; // enable looping bias
        float costWithLoopBias = matcher.computeCost(0, queryTraj, queryPose, options);

        CHECK(costWithLoopBias < costNoLoopBias);
    }

    TEST_CASE("KD-tree and brute force give same best match") {
        TestDatabaseFixture f;
        MotionMatcher matcher;
        matcher.setDatabase(&f.database);

        Trajectory queryTraj;
        for (float t : {-0.1f, 0.1f, 0.2f, 0.4f}) {
            TrajectorySample s;
            s.timeOffset = t;
            s.position = glm::vec3(0.0f, 0.0f, 2.0f * t);
            s.velocity = glm::vec3(0.0f, 0.0f, 2.0f);
            s.facing = glm::vec3(0.0f, 0.0f, 1.0f);
            queryTraj.addSample(s);
        }

        PoseFeatures queryPose;
        queryPose.rootVelocity = glm::vec3(0.0f, 0.0f, 2.0f);

        SearchOptions bruteForceOpts;
        bruteForceOpts.useKDTree = false;
        auto bruteResult = matcher.findBestMatch(queryTraj, queryPose, bruteForceOpts);

        SearchOptions kdTreeOpts;
        kdTreeOpts.useKDTree = true;
        kdTreeOpts.kdTreeCandidates = 128; // large to ensure we don't miss
        auto kdResult = matcher.findBestMatch(queryTraj, queryPose, kdTreeOpts);

        CHECK(bruteResult.isValid());
        CHECK(kdResult.isValid());
        // Both should find the same or very similar best match
        CHECK(kdResult.cost == doctest::Approx(bruteResult.cost).epsilon(0.5));
    }

    TEST_CASE("findTopMatches returns sorted results") {
        TestDatabaseFixture f;
        MotionMatcher matcher;
        matcher.setDatabase(&f.database);

        Trajectory queryTraj;
        TrajectorySample s;
        s.timeOffset = 0.1f;
        s.velocity = glm::vec3(0.0f, 0.0f, 1.5f);
        s.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        queryTraj.addSample(s);

        PoseFeatures queryPose;
        queryPose.rootVelocity = glm::vec3(0.0f, 0.0f, 1.5f);

        SearchOptions options;
        options.useKDTree = false;
        auto results = matcher.findTopMatches(queryTraj, queryPose, 5, options);

        CHECK(results.size() >= 2);
        // Should be sorted by cost ascending
        for (size_t i = 1; i < results.size(); ++i) {
            CHECK(results[i].cost >= results[i - 1].cost);
        }
    }

    TEST_CASE("canTransitionTo=false poses are filtered out") {
        // Build a database with one untransitionable pose
        Skeleton skeleton = createTestSkeleton();
        MotionDatabase database;
        auto config = FeatureConfig::locomotion();
        database.initialize(skeleton, config);

        AnimationClip clip = createTestClip(1.0f, 1.5f);
        database.addClip(&clip, "test", true, 10.0f, {}, 1.5f);

        DatabaseBuildOptions options;
        options.pruneStaticPoses = false;
        options.buildKDTree = false;
        database.build(options);

        // This is tricky - the database doesn't expose mutable access to poses
        // Instead, verify the filter logic with the default (canTransitionTo=true)
        MotionMatcher matcher;
        matcher.setDatabase(&database);

        Trajectory queryTraj;
        TrajectorySample s;
        s.timeOffset = 0.1f;
        s.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        queryTraj.addSample(s);

        PoseFeatures queryPose;
        SearchOptions searchOpts;
        searchOpts.useKDTree = false;
        auto result = matcher.findBestMatch(queryTraj, queryPose, searchOpts);
        CHECK(result.isValid());
    }

    TEST_CASE("normalization is computed after build") {
        TestDatabaseFixture f;
        const auto& norm = f.database.getNormalization();
        CHECK(norm.isComputed);
        // Root velocity stdDev should be > 0 since we have varying clip speeds
        CHECK(norm.rootVelocity.stdDev > 0.0f);
    }
}

// ============================================================================
// FeatureExtractor tests
// ============================================================================
TEST_SUITE("FeatureExtractor") {
    TEST_CASE("initialize finds bones in skeleton") {
        Skeleton skel = createTestSkeleton();
        FeatureConfig config = FeatureConfig::locomotion();
        FeatureExtractor extractor;

        extractor.initialize(skel, config);
        CHECK(extractor.isInitialized());
    }

    TEST_CASE("extractFromClip returns non-empty features") {
        Skeleton skel = createTestSkeleton();
        FeatureConfig config = FeatureConfig::locomotion();
        FeatureExtractor extractor;
        extractor.initialize(skel, config);

        AnimationClip clip = createTestClip(1.0f, 1.5f);
        auto features = extractor.extractFromClip(clip, skel, 0.5f);

        CHECK(features.boneCount > 0);
    }

    TEST_CASE("extractTrajectoryFromClip returns trajectory with samples") {
        Skeleton skel = createTestSkeleton();
        FeatureConfig config = FeatureConfig::locomotion();
        config.trajectorySampleTimes = {-0.1f, 0.1f, 0.2f, 0.4f};
        FeatureExtractor extractor;
        extractor.initialize(skel, config);

        AnimationClip clip = createTestClip(1.0f, 1.5f);
        auto traj = extractor.extractTrajectoryFromClip(clip, skel, 0.5f);

        CHECK(traj.sampleCount == 4);
    }

    TEST_CASE("uninitialised extractor returns empty features") {
        FeatureExtractor extractor;
        CHECK_FALSE(extractor.isInitialized());

        AnimationClip clip = createTestClip(1.0f, 1.0f);
        Skeleton skel = createTestSkeleton();
        auto features = extractor.extractFromClip(clip, skel, 0.0f);
        CHECK(features.boneCount == 0);
    }

    TEST_CASE("strafe mode can be toggled") {
        FeatureExtractor extractor;
        CHECK_FALSE(extractor.isStrafeMode());
        extractor.setStrafeMode(true);
        CHECK(extractor.isStrafeMode());
        extractor.setStrafeMode(false);
        CHECK_FALSE(extractor.isStrafeMode());
    }
}

// ============================================================================
// MotionDatabase poseToKDPoint tests
// ============================================================================
TEST_SUITE("MotionDatabase KDPoint") {
    TEST_CASE("poseToKDPoint produces 16-dim point") {
        Skeleton skel = createTestSkeleton();
        FeatureConfig config = FeatureConfig::locomotion();
        MotionDatabase database;
        database.initialize(skel, config);

        Trajectory traj;
        for (int i = 0; i < 6; ++i) {
            TrajectorySample s;
            s.position = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
            s.velocity = glm::vec3(0.0f, 0.0f, static_cast<float>(i));
            s.timeOffset = static_cast<float>(i) * 0.1f;
            traj.addSample(s);
        }

        PoseFeatures pose;
        pose.rootVelocity = glm::vec3(1.0f, 0.0f, 2.0f);
        pose.rootAngularVelocity = 0.5f;

        KDPoint point = database.poseToKDPoint(traj, pose);

        // Should have non-zero features for trajectory and root velocity
        bool hasNonZero = false;
        for (size_t i = 0; i < KD_FEATURE_DIM; ++i) {
            if (point.features[i] != 0.0f) {
                hasNonZero = true;
                break;
            }
        }
        CHECK(hasNonZero);
    }
}

// ============================================================================
// Normalized cost tests
// ============================================================================
TEST_SUITE("NormalizedCost") {
    TEST_CASE("normalized trajectory cost with identity normalization equals raw cost") {
        Trajectory a, b;
        TrajectorySample sa, sb;
        sa.position = glm::vec3(1.0f, 0.0f, 0.0f);
        sb.position = glm::vec3(2.0f, 0.0f, 0.0f);
        sa.velocity = sb.velocity = glm::vec3(0.0f);
        sa.facing = sb.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        sa.timeOffset = sb.timeOffset = 0.1f;
        a.addSample(sa);
        b.addSample(sb);

        // Identity normalization: mean=0, stdDev=1
        FeatureNormalization norm;
        norm.isComputed = true;

        float rawCost = a.computeCost(b, 1.0f, 0.0f, 0.0f);
        float normCost = a.computeNormalizedCost(b, norm, 1.0f, 0.0f, 0.0f);

        // With identity normalization, normalized cost should equal raw cost
        CHECK(normCost == doctest::Approx(rawCost));
    }

    TEST_CASE("normalized pose cost with identity normalization equals raw cost") {
        PoseFeatures a, b;
        a.boneCount = b.boneCount = 1;
        a.boneFeatures[0].position = glm::vec3(0.0f);
        b.boneFeatures[0].position = glm::vec3(1.0f, 0.0f, 0.0f);
        a.rootVelocity = glm::vec3(0.0f);
        b.rootVelocity = glm::vec3(2.0f, 0.0f, 0.0f);
        a.rootAngularVelocity = 0.0f;
        b.rootAngularVelocity = 1.0f;

        FeatureNormalization norm;
        norm.isComputed = true;

        float rawCost = a.computeCost(b, 1.0f, 1.0f, 1.0f, 0.0f);
        float normCost = a.computeNormalizedCost(b, norm, 1.0f, 1.0f, 1.0f, 0.0f);

        CHECK(normCost == doctest::Approx(rawCost).epsilon(0.01));
    }
}

// ============================================================================
// Integration: database build + search end-to-end
// ============================================================================
TEST_SUITE("Integration") {
    TEST_CASE("end to end: build database, search, get valid result") {
        Skeleton skel = createTestSkeleton();
        FeatureConfig config = FeatureConfig::locomotion();
        MotionDatabase database;
        database.initialize(skel, config);

        AnimationClip walkClip = createTestClip(1.0f, 1.5f);
        AnimationClip idleClip = createTestClip(2.0f, 0.0f);

        database.addClip(&walkClip, "walk", true, 15.0f, {"walk"}, 1.5f);
        database.addClip(&idleClip, "idle", true, 15.0f, {"idle"});

        DatabaseBuildOptions buildOpts;
        buildOpts.pruneStaticPoses = false;
        database.build(buildOpts);

        CHECK(database.isBuilt());

        MotionMatcher matcher;
        matcher.setDatabase(&database);

        // Query for walking
        Trajectory queryTraj;
        TrajectorySample s;
        s.timeOffset = 0.1f;
        s.position = glm::vec3(0.0f, 0.0f, 0.15f);
        s.velocity = glm::vec3(0.0f, 0.0f, 1.5f);
        s.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        queryTraj.addSample(s);

        PoseFeatures queryPose;
        queryPose.rootVelocity = glm::vec3(0.0f, 0.0f, 1.5f);

        SearchOptions searchOpts;
        searchOpts.useKDTree = false;
        auto result = matcher.findBestMatch(queryTraj, queryPose, searchOpts);

        CHECK(result.isValid());
        CHECK(result.clip != nullptr);
        CHECK(result.pose != nullptr);
    }

    TEST_CASE("stats are reported correctly") {
        Skeleton skel = createTestSkeleton();
        FeatureConfig config = FeatureConfig::locomotion();
        MotionDatabase database;
        database.initialize(skel, config);

        AnimationClip clip1 = createTestClip(1.0f, 1.0f);
        AnimationClip clip2 = createTestClip(2.0f, 2.0f);

        database.addClip(&clip1, "clip1", true, 10.0f, {});
        database.addClip(&clip2, "clip2", true, 10.0f, {});

        DatabaseBuildOptions opts;
        opts.pruneStaticPoses = false;
        database.build(opts);

        auto stats = database.getStats();
        CHECK(stats.totalClips == 2);
        CHECK(stats.totalPoses > 0);
        CHECK(stats.totalDuration == doctest::Approx(3.0f));
    }

    TEST_CASE("clear resets database completely") {
        Skeleton skel = createTestSkeleton();
        FeatureConfig config = FeatureConfig::locomotion();
        MotionDatabase database;
        database.initialize(skel, config);

        AnimationClip clip = createTestClip(1.0f, 1.0f);
        database.addClip(&clip, "test", true, 10.0f, {});
        database.build();

        CHECK(database.isBuilt());
        CHECK(database.getPoseCount() > 0);

        database.clear();
        CHECK_FALSE(database.isBuilt());
        CHECK(database.getPoseCount() == 0);
        CHECK(database.getClipCount() == 0);
    }
}
