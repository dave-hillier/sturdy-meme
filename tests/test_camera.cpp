#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/epsilon.hpp>
#include <cmath>

#include "scene/Camera.h"

static bool approxEqual(const glm::vec3& a, const glm::vec3& b, float eps = 0.01f) {
    return glm::all(glm::epsilonEqual(a, b, eps));
}

TEST_SUITE("Camera") {
    TEST_CASE("default construction") {
        Camera cam;
        CHECK(cam.getPosition().y == doctest::Approx(1.5f));
        CHECK(cam.getNearPlane() == doctest::Approx(0.1f));
        CHECK(cam.getFarPlane() == doctest::Approx(50000.0f));
        CHECK(cam.getFov() == doctest::Approx(45.0f));
        CHECK(cam.getYaw() == doctest::Approx(-90.0f));
        CHECK(cam.getPitch() == doctest::Approx(0.0f));
    }

    TEST_CASE("setPosition") {
        Camera cam;
        cam.setPosition(glm::vec3(10.0f, 20.0f, 30.0f));
        CHECK(approxEqual(cam.getPosition(), glm::vec3(10.0f, 20.0f, 30.0f)));
    }

    TEST_CASE("setRotation") {
        Camera cam;
        cam.setRotation(45.0f, 30.0f);
        CHECK(cam.getYaw() == doctest::Approx(45.0f));
        CHECK(cam.getPitch() == doctest::Approx(30.0f));
    }

    TEST_CASE("pitch is clamped") {
        Camera cam;
        cam.setPitch(100.0f);
        CHECK(cam.getPitch() == doctest::Approx(89.0f));

        cam.setPitch(-100.0f);
        CHECK(cam.getPitch() == doctest::Approx(-89.0f));
    }

    TEST_CASE("rotatePitch clamps") {
        Camera cam;
        cam.setPitch(85.0f);
        cam.rotatePitch(10.0f);
        CHECK(cam.getPitch() == doctest::Approx(89.0f));
    }

    TEST_CASE("forward vector at default yaw=-90 is along -Z") {
        Camera cam;
        // Default yaw=-90, pitch=0 should give forward ~(0, 0, -1)
        glm::vec3 fwd = cam.getForward();
        CHECK(fwd.x == doctest::Approx(0.0f).epsilon(0.01));
        CHECK(fwd.y == doctest::Approx(0.0f).epsilon(0.01));
        CHECK(fwd.z == doctest::Approx(-1.0f).epsilon(0.01));
    }

    TEST_CASE("moveForward translates along forward vector") {
        Camera cam;
        glm::vec3 startPos = cam.getPosition();
        glm::vec3 fwd = cam.getForward();

        cam.moveForward(5.0f);
        glm::vec3 expected = startPos + fwd * 5.0f;
        CHECK(approxEqual(cam.getPosition(), expected));
    }

    TEST_CASE("moveRight translates along right vector") {
        Camera cam;
        glm::vec3 startPos = cam.getPosition();
        glm::vec3 right = cam.getRight();

        cam.moveRight(3.0f);
        glm::vec3 expected = startPos + right * 3.0f;
        CHECK(approxEqual(cam.getPosition(), expected));
    }

    TEST_CASE("moveUp translates along world up") {
        Camera cam;
        cam.setPosition(glm::vec3(0.0f));

        cam.moveUp(7.0f);
        CHECK(cam.getPosition().y == doctest::Approx(7.0f));
        CHECK(cam.getPosition().x == doctest::Approx(0.0f));
        CHECK(cam.getPosition().z == doctest::Approx(0.0f));
    }

    TEST_CASE("view matrix looks at correct direction") {
        Camera cam;
        cam.setPosition(glm::vec3(0.0f));
        cam.setRotation(-90.0f, 0.0f);  // Looking along -Z

        glm::mat4 view = cam.getViewMatrix();
        // View matrix transforms world to camera space
        // Camera at origin looking at -Z => a point at (0,0,-5) should
        // be at (0,0,-5) in view space
        glm::vec4 worldPoint(0.0f, 0.0f, -5.0f, 1.0f);
        glm::vec4 viewPoint = view * worldPoint;
        CHECK(viewPoint.z < 0.0f); // Should be in front of camera (negative Z in view space)
    }

    TEST_CASE("projection matrix is valid") {
        Camera cam;
        cam.setAspectRatio(16.0f / 9.0f);

        glm::mat4 proj = cam.getProjectionMatrix();
        // Vulkan-style: proj[1][1] is flipped (negative)
        CHECK(proj[1][1] < 0.0f);
        // Near/far planes should produce reasonable depth range
        CHECK(proj[0][0] != 0.0f);
    }

    TEST_CASE("getRotation returns a valid unit quaternion") {
        Camera cam;
        cam.setRotation(45.0f, 30.0f);

        glm::quat q = cam.getRotation();
        float len = glm::length(q);
        CHECK(len == doctest::Approx(1.0f).epsilon(0.001));
    }

    TEST_CASE("getTransform returns position and rotation") {
        Camera cam;
        cam.setPosition(glm::vec3(1.0f, 2.0f, 3.0f));
        cam.setRotation(90.0f, 0.0f);

        Transform t = cam.getTransform();
        CHECK(approxEqual(t.position, glm::vec3(1.0f, 2.0f, 3.0f)));
        float len = glm::length(t.rotation);
        CHECK(len == doctest::Approx(1.0f).epsilon(0.001));
    }

    TEST_CASE("setAspectRatio changes projection") {
        Camera cam;

        cam.setAspectRatio(1.0f);
        glm::mat4 proj1 = cam.getProjectionMatrix();

        cam.setAspectRatio(2.0f);
        glm::mat4 proj2 = cam.getProjectionMatrix();

        // Different aspect ratios should produce different projections
        CHECK(proj1[0][0] != doctest::Approx(proj2[0][0]));
    }

    TEST_CASE("third-person: orbit pitch clamps") {
        Camera cam;
        cam.orbitPitch(1000.0f);  // Should clamp internally on updateThirdPerson
        // Just verify it doesn't crash
        cam.updateThirdPerson(0.016f);
    }

    TEST_CASE("third-person: adjustDistance clamps to range") {
        Camera cam;
        cam.setDistance(5.0f);

        // Try to go below minimum
        cam.adjustDistance(-100.0f);
        cam.updateThirdPerson(0.016f);
        CHECK(cam.getSmoothedDistance() > 0.0f);
    }

    TEST_CASE("resetSmoothing snaps values") {
        Camera cam;
        cam.setThirdPersonTarget(glm::vec3(10.0f, 0.0f, 0.0f));
        cam.resetSmoothing();

        glm::vec3 target = cam.getThirdPersonTarget();
        CHECK(target.x == doctest::Approx(10.0f));
    }

    TEST_CASE("forward, right, up are orthonormal") {
        Camera cam;
        cam.setRotation(37.0f, 15.0f);

        glm::vec3 fwd = cam.getForward();
        glm::vec3 right = cam.getRight();
        glm::vec3 up = cam.getUp();

        // All should be unit length
        CHECK(glm::length(fwd) == doctest::Approx(1.0f).epsilon(0.001));
        CHECK(glm::length(right) == doctest::Approx(1.0f).epsilon(0.001));
        CHECK(glm::length(up) == doctest::Approx(1.0f).epsilon(0.001));

        // Should be orthogonal
        CHECK(std::abs(glm::dot(fwd, right)) < 0.001f);
        CHECK(std::abs(glm::dot(fwd, up)) < 0.001f);
        CHECK(std::abs(glm::dot(right, up)) < 0.001f);
    }
}
