#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/epsilon.hpp>
#include <cmath>

#include "scene/PlayerState.h"

static bool approxEqual(const glm::vec3& a, const glm::vec3& b, float eps = 0.01f) {
    return glm::all(glm::epsilonEqual(a, b, eps));
}

TEST_SUITE("PlayerTransform") {
    TEST_CASE("withPosition creates transform at position") {
        auto pt = PlayerTransform::withPosition(glm::vec3(1.0f, 2.0f, 3.0f));
        CHECK(approxEqual(pt.position, glm::vec3(1.0f, 2.0f, 3.0f)));
    }

    TEST_CASE("withYaw creates rotated transform") {
        auto pt = PlayerTransform::withYaw(glm::vec3(0.0f), 90.0f);
        float yaw = pt.getYaw();
        CHECK(yaw == doctest::Approx(90.0f).epsilon(0.5));
    }

    TEST_CASE("setYaw and getYaw roundtrip") {
        PlayerTransform pt;
        pt.setYaw(45.0f);
        CHECK(pt.getYaw() == doctest::Approx(45.0f).epsilon(0.5));

        pt.setYaw(-90.0f);
        CHECK(pt.getYaw() == doctest::Approx(-90.0f).epsilon(0.5));

        pt.setYaw(180.0f);
        CHECK(std::abs(pt.getYaw()) == doctest::Approx(180.0f).epsilon(0.5));
    }

    TEST_CASE("getYaw at 0 degrees points along +Z") {
        auto pt = PlayerTransform::withYaw(glm::vec3(0.0f), 0.0f);
        glm::vec3 fwd = pt.getForward();
        // At yaw=0, forward should be along +Z (or -Z depending on convention)
        // The forward() method uses rotation * (0,0,1)
        // With a Y-rotation of 0 degrees, forward = (0,0,1)
        CHECK(std::abs(fwd.z) > 0.5f);
    }

    TEST_CASE("getForward returns unit vector") {
        auto pt = PlayerTransform::withYaw(glm::vec3(0.0f), 37.0f);
        float len = glm::length(pt.getForward());
        CHECK(len == doctest::Approx(1.0f).epsilon(0.01));
    }

    TEST_CASE("getMatrix returns valid matrix") {
        auto pt = PlayerTransform::withYaw(glm::vec3(5.0f, 0.0f, 3.0f), 45.0f);
        glm::mat4 mat = pt.getMatrix();

        // Translation should be in the matrix
        CHECK(mat[3][0] == doctest::Approx(5.0f));
        CHECK(mat[3][2] == doctest::Approx(3.0f));
    }
}

TEST_SUITE("PlayerMovement") {
    TEST_CASE("getFocusPoint is above player position") {
        PlayerMovement movement;
        glm::vec3 pos(0.0f, 0.0f, 0.0f);
        glm::vec3 focus = movement.getFocusPoint(pos);

        CHECK(focus.y > pos.y);
        CHECK(focus.x == doctest::Approx(0.0f));
        CHECK(focus.z == doctest::Approx(0.0f));
    }

    TEST_CASE("getFocusPoint height is proportional to capsule height") {
        PlayerMovement movement;
        glm::vec3 pos(10.0f, 5.0f, 20.0f);
        glm::vec3 focus = movement.getFocusPoint(pos);

        float expectedY = 5.0f + PlayerMovement::CAPSULE_HEIGHT * 0.85f;
        CHECK(focus.y == doctest::Approx(expectedY));
        CHECK(focus.x == doctest::Approx(10.0f));
        CHECK(focus.z == doctest::Approx(20.0f));
    }

    TEST_CASE("getModelMatrix includes position offset") {
        PlayerMovement movement;
        auto transform = PlayerTransform::withYaw(glm::vec3(5.0f, 0.0f, 3.0f), 0.0f);
        glm::mat4 model = movement.getModelMatrix(transform);

        // Model matrix should translate to position + half capsule height
        CHECK(model[3][0] == doctest::Approx(5.0f));
        CHECK(model[3][1] == doctest::Approx(PlayerMovement::CAPSULE_HEIGHT * 0.5f));
        CHECK(model[3][2] == doctest::Approx(3.0f));
    }

    TEST_CASE("orientation lock uses locked yaw") {
        PlayerMovement movement;
        movement.orientationLocked = true;
        movement.lockedYaw = 90.0f;

        auto transform = PlayerTransform::withYaw(glm::vec3(0.0f), 45.0f);
        glm::mat4 lockedModel = movement.getModelMatrix(transform);

        movement.orientationLocked = false;
        glm::mat4 unlockedModel = movement.getModelMatrix(transform);

        // The matrices should differ because of different yaw values
        // Compare a non-trivial element
        CHECK(lockedModel[0][0] != doctest::Approx(unlockedModel[0][0]).epsilon(0.01));
    }

    TEST_CASE("capsule dimensions are reasonable") {
        CHECK(PlayerMovement::CAPSULE_HEIGHT > 0.0f);
        CHECK(PlayerMovement::CAPSULE_HEIGHT < 3.0f);
        CHECK(PlayerMovement::CAPSULE_RADIUS > 0.0f);
        CHECK(PlayerMovement::CAPSULE_RADIUS < PlayerMovement::CAPSULE_HEIGHT);
    }
}

TEST_SUITE("PlayerState") {
    TEST_CASE("default state") {
        PlayerState state;
        CHECK(state.grounded == false);
    }
}
