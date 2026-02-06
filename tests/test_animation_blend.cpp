#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "animation/AnimationBlend.h"

static bool approxEqual(const glm::vec3& a, const glm::vec3& b, float eps = 0.001f) {
    return glm::all(glm::epsilonEqual(a, b, eps));
}

static bool approxEqual(const glm::quat& a, const glm::quat& b, float eps = 0.001f) {
    // Quaternions q and -q represent the same rotation
    float dot = glm::dot(a, b);
    return std::abs(dot) > (1.0f - eps);
}

// ============================================================================
// BonePose tests
// ============================================================================

TEST_SUITE("BonePose") {
    TEST_CASE("identity pose produces identity matrix") {
        BonePose pose = BonePose::identity();
        glm::mat4 mat = pose.toMatrix();
        glm::mat4 identity(1.0f);

        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                CHECK(mat[c][r] == doctest::Approx(identity[c][r]).epsilon(0.0001));
    }

    TEST_CASE("toMatrix applies translation") {
        BonePose pose = BonePose::identity();
        pose.translation = glm::vec3(3.0f, 4.0f, 5.0f);

        glm::mat4 mat = pose.toMatrix();
        CHECK(mat[3][0] == doctest::Approx(3.0f));
        CHECK(mat[3][1] == doctest::Approx(4.0f));
        CHECK(mat[3][2] == doctest::Approx(5.0f));
    }

    TEST_CASE("toMatrix applies scale") {
        BonePose pose = BonePose::identity();
        pose.scale = glm::vec3(2.0f, 3.0f, 4.0f);

        glm::mat4 mat = pose.toMatrix();
        CHECK(glm::length(glm::vec3(mat[0])) == doctest::Approx(2.0f));
        CHECK(glm::length(glm::vec3(mat[1])) == doctest::Approx(3.0f));
        CHECK(glm::length(glm::vec3(mat[2])) == doctest::Approx(4.0f));
    }

    TEST_CASE("toMatrix with preRotation") {
        BonePose pose = BonePose::identity();
        pose.translation = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::quat preRot = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));

        glm::mat4 mat = pose.toMatrix(preRot);
        // Translation should still be at (1,0,0)
        CHECK(mat[3][0] == doctest::Approx(1.0f));
    }

    TEST_CASE("fromMatrix roundtrip preserves pose") {
        BonePose original;
        original.translation = glm::vec3(1.0f, 2.0f, 3.0f);
        original.rotation = glm::angleAxis(glm::radians(45.0f), glm::normalize(glm::vec3(1, 1, 0)));
        original.scale = glm::vec3(1.5f, 2.0f, 0.8f);

        glm::mat4 mat = original.toMatrix();
        BonePose recovered = BonePose::fromMatrix(mat);

        CHECK(approxEqual(original.translation, recovered.translation));
        CHECK(approxEqual(original.rotation, recovered.rotation));
        CHECK(approxEqual(original.scale, recovered.scale));
    }

    TEST_CASE("fromMatrix with preRotation extracts animated rotation") {
        glm::quat preRot = glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0));
        glm::quat animRot = glm::angleAxis(glm::radians(60.0f), glm::vec3(1, 0, 0));

        BonePose original;
        original.translation = glm::vec3(5.0f, 0.0f, 0.0f);
        original.rotation = animRot;
        original.scale = glm::vec3(1.0f);

        glm::mat4 mat = original.toMatrix(preRot);
        BonePose recovered = BonePose::fromMatrix(mat, preRot);

        CHECK(approxEqual(original.translation, recovered.translation));
        CHECK(approxEqual(original.rotation, recovered.rotation));
    }
}

// ============================================================================
// AnimationBlend namespace tests
// ============================================================================

TEST_SUITE("AnimationBlend") {
    TEST_CASE("blend BonePose at t=0 returns first pose") {
        BonePose a;
        a.translation = glm::vec3(1.0f, 0.0f, 0.0f);
        a.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        a.scale = glm::vec3(1.0f);

        BonePose b;
        b.translation = glm::vec3(5.0f, 0.0f, 0.0f);
        b.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
        b.scale = glm::vec3(2.0f);

        BonePose result = AnimationBlend::blend(a, b, 0.0f);
        CHECK(approxEqual(result.translation, a.translation));
        CHECK(approxEqual(result.scale, a.scale));
    }

    TEST_CASE("blend BonePose at t=1 returns second pose") {
        BonePose a;
        a.translation = glm::vec3(1.0f, 0.0f, 0.0f);
        a.scale = glm::vec3(1.0f);

        BonePose b;
        b.translation = glm::vec3(5.0f, 0.0f, 0.0f);
        b.scale = glm::vec3(2.0f);

        BonePose result = AnimationBlend::blend(a, b, 1.0f);
        CHECK(approxEqual(result.translation, b.translation));
        CHECK(approxEqual(result.scale, b.scale));
    }

    TEST_CASE("blend BonePose at t=0.5 interpolates") {
        BonePose a;
        a.translation = glm::vec3(0.0f, 0.0f, 0.0f);
        a.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        a.scale = glm::vec3(1.0f);

        BonePose b;
        b.translation = glm::vec3(10.0f, 20.0f, 30.0f);
        b.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        b.scale = glm::vec3(3.0f);

        BonePose result = AnimationBlend::blend(a, b, 0.5f);
        CHECK(approxEqual(result.translation, glm::vec3(5.0f, 10.0f, 15.0f)));
        CHECK(approxEqual(result.scale, glm::vec3(2.0f)));
    }

    TEST_CASE("blend SkeletonPose") {
        SkeletonPose poseA, poseB, result;
        poseA.resize(3);
        poseB.resize(3);

        for (size_t i = 0; i < 3; ++i) {
            poseA[i] = BonePose::identity();
            poseA[i].translation = glm::vec3(0.0f);

            poseB[i] = BonePose::identity();
            poseB[i].translation = glm::vec3(static_cast<float>(i) * 2.0f, 0.0f, 0.0f);
        }

        AnimationBlend::blend(poseA, poseB, 0.5f, result);
        REQUIRE(result.size() == 3);
        CHECK(result[0].translation.x == doctest::Approx(0.0f));
        CHECK(result[1].translation.x == doctest::Approx(1.0f));
        CHECK(result[2].translation.x == doctest::Approx(2.0f));
    }

    TEST_CASE("blendMasked uses per-bone weights") {
        SkeletonPose poseA, poseB, result;
        poseA.resize(3);
        poseB.resize(3);

        for (size_t i = 0; i < 3; ++i) {
            poseA[i] = BonePose::identity();
            poseA[i].translation = glm::vec3(0.0f);

            poseB[i] = BonePose::identity();
            poseB[i].translation = glm::vec3(10.0f, 0.0f, 0.0f);
        }

        std::vector<float> weights = {0.0f, 0.5f, 1.0f};
        AnimationBlend::blendMasked(poseA, poseB, weights, result);
        REQUIRE(result.size() == 3);
        CHECK(result[0].translation.x == doctest::Approx(0.0f));
        CHECK(result[1].translation.x == doctest::Approx(5.0f));
        CHECK(result[2].translation.x == doctest::Approx(10.0f));
    }

    TEST_CASE("additive with zero weight returns base") {
        BonePose base;
        base.translation = glm::vec3(5.0f, 3.0f, 1.0f);
        base.rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));
        base.scale = glm::vec3(2.0f);

        BonePose delta;
        delta.translation = glm::vec3(10.0f, 20.0f, 30.0f);
        delta.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));
        delta.scale = glm::vec3(3.0f);

        BonePose result = AnimationBlend::additive(base, delta, 0.0f);
        CHECK(approxEqual(result.translation, base.translation));
        CHECK(approxEqual(result.scale, base.scale));
    }

    TEST_CASE("additive applies translation offset") {
        BonePose base;
        base.translation = glm::vec3(1.0f, 0.0f, 0.0f);
        base.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        base.scale = glm::vec3(1.0f);

        BonePose delta;
        delta.translation = glm::vec3(2.0f, 3.0f, 0.0f);
        delta.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        delta.scale = glm::vec3(1.0f);

        BonePose result = AnimationBlend::additive(base, delta, 1.0f);
        CHECK(result.translation.x == doctest::Approx(3.0f));
        CHECK(result.translation.y == doctest::Approx(3.0f));
    }

    TEST_CASE("computeAdditiveDelta and additive are inverses") {
        BonePose reference = BonePose::identity();
        reference.translation = glm::vec3(1.0f, 0.0f, 0.0f);

        BonePose animation;
        animation.translation = glm::vec3(4.0f, 3.0f, 0.0f);
        animation.rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0));
        animation.scale = glm::vec3(1.5f);

        BonePose delta = AnimationBlend::computeAdditiveDelta(reference, animation);
        BonePose result = AnimationBlend::additive(reference, delta, 1.0f);

        CHECK(approxEqual(result.translation, animation.translation));
        CHECK(approxEqual(result.scale, animation.scale));
    }

    TEST_CASE("blend handles different sized skeleton poses") {
        SkeletonPose poseA, poseB, result;
        poseA.resize(5);
        poseB.resize(3);

        for (size_t i = 0; i < 5; ++i) poseA[i] = BonePose::identity();
        for (size_t i = 0; i < 3; ++i) poseB[i] = BonePose::identity();

        AnimationBlend::blend(poseA, poseB, 0.5f, result);
        CHECK(result.size() == 3); // min of both sizes
    }
}
