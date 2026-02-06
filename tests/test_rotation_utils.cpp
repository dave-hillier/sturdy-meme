#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/epsilon.hpp>
#include <cmath>

#include "scene/RotationUtils.h"

static bool approxEqual(const glm::vec3& a, const glm::vec3& b, float eps = 0.01f) {
    return glm::all(glm::epsilonEqual(a, b, eps));
}

TEST_SUITE("RotationUtils") {
    TEST_CASE("rotationFromDirection: aligned direction returns identity") {
        glm::vec3 defaultDir(0.0f, -1.0f, 0.0f);
        glm::quat q = RotationUtils::rotationFromDirection(defaultDir, defaultDir);

        // Should be identity or very close
        CHECK(std::abs(q.w) == doctest::Approx(1.0f).epsilon(0.01));
    }

    TEST_CASE("rotationFromDirection: opposite direction returns 180 degree rotation") {
        glm::vec3 defaultDir(0.0f, -1.0f, 0.0f);
        glm::vec3 opposite(0.0f, 1.0f, 0.0f);
        glm::quat q = RotationUtils::rotationFromDirection(opposite, defaultDir);

        // Apply to defaultDir should give opposite
        glm::vec3 result = q * defaultDir;
        CHECK(approxEqual(result, opposite));
    }

    TEST_CASE("rotationFromDirection: 90 degree rotation") {
        glm::vec3 defaultDir(0.0f, -1.0f, 0.0f);
        glm::vec3 target(1.0f, 0.0f, 0.0f);
        glm::quat q = RotationUtils::rotationFromDirection(target, defaultDir);

        glm::vec3 result = q * defaultDir;
        CHECK(approxEqual(result, target));
    }

    TEST_CASE("rotationFromDirection: arbitrary direction") {
        glm::vec3 defaultDir(0.0f, -1.0f, 0.0f);
        glm::vec3 target = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
        glm::quat q = RotationUtils::rotationFromDirection(target, defaultDir);

        glm::vec3 result = q * defaultDir;
        CHECK(approxEqual(result, target));
    }

    TEST_CASE("directionFromRotation: identity returns default direction") {
        glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 dir = RotationUtils::directionFromRotation(identity);
        CHECK(approxEqual(dir, glm::vec3(0.0f, -1.0f, 0.0f)));
    }

    TEST_CASE("directionFromRotation: custom default direction") {
        glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 customDefault(0.0f, 0.0f, 1.0f);
        glm::vec3 dir = RotationUtils::directionFromRotation(identity, customDefault);
        CHECK(approxEqual(dir, customDefault));
    }

    TEST_CASE("roundtrip: rotationFromDirection -> directionFromRotation") {
        glm::vec3 defaultDir(0.0f, -1.0f, 0.0f);
        glm::vec3 target = glm::normalize(glm::vec3(0.3f, -0.7f, 0.5f));

        glm::quat q = RotationUtils::rotationFromDirection(target, defaultDir);
        glm::vec3 recovered = RotationUtils::directionFromRotation(q, defaultDir);

        CHECK(approxEqual(recovered, target));
    }

    TEST_CASE("rotationFromDirection produces unit quaternion") {
        glm::vec3 directions[] = {
            glm::vec3(1, 0, 0),
            glm::vec3(0, 1, 0),
            glm::vec3(0, 0, 1),
            glm::vec3(-1, 0, 0),
            glm::vec3(0, -1, 0),
            glm::normalize(glm::vec3(1, 1, 1)),
            glm::normalize(glm::vec3(-1, 0.5f, -0.3f)),
        };

        for (const auto& dir : directions) {
            glm::quat q = RotationUtils::rotationFromDirection(dir);
            float len = glm::length(q);
            CHECK(len == doctest::Approx(1.0f).epsilon(0.01));
        }
    }
}
