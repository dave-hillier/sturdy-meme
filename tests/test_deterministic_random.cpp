#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <cmath>
#include <set>

#include "scene/DeterministicRandom.h"

TEST_SUITE("DeterministicRandom") {
    TEST_CASE("hashPosition returns values in [0, 1]") {
        for (float x = -100.0f; x <= 100.0f; x += 13.7f) {
            for (float z = -100.0f; z <= 100.0f; z += 17.3f) {
                float val = DeterministicRandom::hashPosition(x, z, 42);
                CHECK(val >= 0.0f);
                CHECK(val <= 1.0f);
            }
        }
    }

    TEST_CASE("hashPosition is deterministic") {
        float a = DeterministicRandom::hashPosition(3.14f, 2.71f, 100);
        float b = DeterministicRandom::hashPosition(3.14f, 2.71f, 100);
        CHECK(a == b);
    }

    TEST_CASE("hashPosition varies with seed") {
        float a = DeterministicRandom::hashPosition(1.0f, 1.0f, 0);
        float b = DeterministicRandom::hashPosition(1.0f, 1.0f, 1);
        CHECK(a != b);
    }

    TEST_CASE("hashPosition varies with position") {
        float a = DeterministicRandom::hashPosition(0.0f, 0.0f, 42);
        float b = DeterministicRandom::hashPosition(1.0f, 0.0f, 42);
        float c = DeterministicRandom::hashPosition(0.0f, 1.0f, 42);
        CHECK(a != b);
        CHECK(a != c);
    }

    TEST_CASE("hashRange returns values in [minVal, maxVal]") {
        for (int i = 0; i < 50; ++i) {
            float x = static_cast<float>(i) * 7.13f;
            float z = static_cast<float>(i) * 3.29f;
            float val = DeterministicRandom::hashRange(x, z, 99, -5.0f, 15.0f);
            CHECK(val >= -5.0f);
            CHECK(val <= 15.0f);
        }
    }

    TEST_CASE("hashRange respects bounds") {
        float val = DeterministicRandom::hashRange(1.0f, 1.0f, 0, 10.0f, 20.0f);
        CHECK(val >= 10.0f);
        CHECK(val <= 20.0f);
    }

    TEST_CASE("hashInt returns values in [0, maxVal)") {
        for (int i = 0; i < 100; ++i) {
            float x = static_cast<float>(i) * 1.23f;
            float z = static_cast<float>(i) * 4.56f;
            uint32_t val = DeterministicRandom::hashInt(x, z, 7, 10);
            CHECK(val < 10);
        }
    }

    TEST_CASE("hashInt with maxVal=0 returns 0") {
        uint32_t val = DeterministicRandom::hashInt(1.0f, 2.0f, 0, 0);
        CHECK(val == 0);
    }

    TEST_CASE("hashInt produces varied output") {
        std::set<uint32_t> values;
        for (int i = 0; i < 50; ++i) {
            float x = static_cast<float>(i);
            uint32_t val = DeterministicRandom::hashInt(x, 0.0f, 42, 100);
            values.insert(val);
        }
        // Should produce at least a few different values
        CHECK(values.size() > 5);
    }

    TEST_CASE("hashDirection returns unit vectors") {
        for (int i = 0; i < 20; ++i) {
            float x = static_cast<float>(i) * 3.0f;
            glm::vec2 dir = DeterministicRandom::hashDirection(x, 0.0f, 42);
            float len = glm::length(dir);
            CHECK(len == doctest::Approx(1.0f).epsilon(0.01));
        }
    }

    TEST_CASE("hashDiskPoint returns points within radius") {
        float radius = 5.0f;
        for (int i = 0; i < 50; ++i) {
            float x = static_cast<float>(i);
            glm::vec2 point = DeterministicRandom::hashDiskPoint(x, 0.0f, 10, radius);
            float dist = glm::length(point);
            CHECK(dist <= radius + 0.001f);
        }
    }

    TEST_CASE("hashDiskPoint with zero radius returns origin") {
        glm::vec2 point = DeterministicRandom::hashDiskPoint(1.0f, 1.0f, 0, 0.0f);
        CHECK(glm::length(point) == doctest::Approx(0.0f).epsilon(0.001));
    }
}
