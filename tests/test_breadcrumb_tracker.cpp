#include <doctest/doctest.h>
#include <glm/glm.hpp>

#include "scene/BreadcrumbTracker.h"

TEST_SUITE("BreadcrumbTracker") {
    TEST_CASE("initially empty") {
        BreadcrumbTracker tracker;
        CHECK_FALSE(tracker.hasBreadcrumbs());
        CHECK(tracker.getBreadcrumbCount() == 0);
        CHECK_FALSE(tracker.getMostRecentBreadcrumb().has_value());
        CHECK_FALSE(tracker.getNearestSafeBreadcrumb(glm::vec3(0)).has_value());
    }

    TEST_CASE("adds breadcrumbs") {
        BreadcrumbTracker tracker;
        tracker.setMinDistance(0.0f); // Allow any distance

        tracker.update(glm::vec3(0.0f, 0.0f, 0.0f));
        CHECK(tracker.getBreadcrumbCount() == 1);

        tracker.update(glm::vec3(1.0f, 0.0f, 0.0f));
        CHECK(tracker.getBreadcrumbCount() == 2);
    }

    TEST_CASE("respects minimum distance") {
        BreadcrumbTracker tracker;
        tracker.setMinDistance(10.0f);

        tracker.update(glm::vec3(0.0f, 0.0f, 0.0f));
        CHECK(tracker.getBreadcrumbCount() == 1);

        // Too close - should not add
        tracker.update(glm::vec3(5.0f, 0.0f, 0.0f));
        CHECK(tracker.getBreadcrumbCount() == 1);

        // Far enough - should add
        tracker.update(glm::vec3(15.0f, 0.0f, 0.0f));
        CHECK(tracker.getBreadcrumbCount() == 2);
    }

    TEST_CASE("respects max breadcrumbs limit") {
        BreadcrumbTracker tracker;
        tracker.setMinDistance(0.0f);
        tracker.setMaxBreadcrumbs(3);

        for (int i = 0; i < 5; ++i) {
            tracker.update(glm::vec3(static_cast<float>(i) * 100.0f, 0.0f, 0.0f));
        }

        CHECK(tracker.getBreadcrumbCount() == 3);
    }

    TEST_CASE("safety check filters positions") {
        BreadcrumbTracker tracker;
        tracker.setMinDistance(0.0f);

        // Only positions with y > 0 are safe
        tracker.setSafetyCheck([](const glm::vec3& pos) {
            return pos.y > 0.0f;
        });

        tracker.update(glm::vec3(0.0f, -1.0f, 0.0f)); // Unsafe - rejected
        CHECK(tracker.getBreadcrumbCount() == 0);

        tracker.update(glm::vec3(0.0f, 1.0f, 0.0f));  // Safe - accepted
        CHECK(tracker.getBreadcrumbCount() == 1);
    }

    TEST_CASE("getNearestSafeBreadcrumb returns closest") {
        BreadcrumbTracker tracker;
        tracker.setMinDistance(0.0f);

        tracker.update(glm::vec3(0.0f, 0.0f, 0.0f));
        tracker.update(glm::vec3(10.0f, 0.0f, 0.0f));
        tracker.update(glm::vec3(100.0f, 0.0f, 0.0f));

        auto result = tracker.getNearestSafeBreadcrumb(glm::vec3(9.0f, 0.0f, 0.0f));
        REQUIRE(result.has_value());
        CHECK(result->x == doctest::Approx(10.0f));
    }

    TEST_CASE("getMostRecentBreadcrumb returns last added") {
        BreadcrumbTracker tracker;
        tracker.setMinDistance(0.0f);

        tracker.update(glm::vec3(1.0f, 0.0f, 0.0f));
        tracker.update(glm::vec3(2.0f, 0.0f, 0.0f));
        tracker.update(glm::vec3(3.0f, 0.0f, 0.0f));

        auto result = tracker.getMostRecentBreadcrumb();
        REQUIRE(result.has_value());
        CHECK(result->x == doctest::Approx(3.0f));
    }

    TEST_CASE("getSafeBreadcrumbAwayFrom respects minimum safe distance") {
        BreadcrumbTracker tracker;
        tracker.setMinDistance(0.0f);

        tracker.update(glm::vec3(1.0f, 0.0f, 0.0f));
        tracker.update(glm::vec3(5.0f, 0.0f, 0.0f));
        tracker.update(glm::vec3(20.0f, 0.0f, 0.0f));

        glm::vec3 dangerPos(4.0f, 0.0f, 0.0f);

        // Need at least 10 units away from danger
        auto result = tracker.getSafeBreadcrumbAwayFrom(dangerPos, 10.0f);
        REQUIRE(result.has_value());
        CHECK(result->x == doctest::Approx(20.0f));
    }

    TEST_CASE("getSafeBreadcrumbAwayFrom returns nullopt when none qualify") {
        BreadcrumbTracker tracker;
        tracker.setMinDistance(0.0f);

        tracker.update(glm::vec3(1.0f, 0.0f, 0.0f));
        tracker.update(glm::vec3(2.0f, 0.0f, 0.0f));

        auto result = tracker.getSafeBreadcrumbAwayFrom(glm::vec3(1.5f, 0.0f, 0.0f), 100.0f);
        CHECK_FALSE(result.has_value());
    }

    TEST_CASE("clear removes all breadcrumbs") {
        BreadcrumbTracker tracker;
        tracker.setMinDistance(0.0f);

        tracker.update(glm::vec3(1.0f, 0.0f, 0.0f));
        tracker.update(glm::vec3(2.0f, 0.0f, 0.0f));
        CHECK(tracker.getBreadcrumbCount() == 2);

        tracker.clear();
        CHECK(tracker.getBreadcrumbCount() == 0);
        CHECK_FALSE(tracker.hasBreadcrumbs());
    }

    TEST_CASE("oldest breadcrumbs are evicted first") {
        BreadcrumbTracker tracker;
        tracker.setMinDistance(0.0f);
        tracker.setMaxBreadcrumbs(2);

        tracker.update(glm::vec3(10.0f, 0.0f, 0.0f));  // Will be evicted
        tracker.update(glm::vec3(20.0f, 0.0f, 0.0f));
        tracker.update(glm::vec3(30.0f, 0.0f, 0.0f));

        CHECK(tracker.getBreadcrumbCount() == 2);

        // First crumb (10) should be gone; nearest to origin should be 20
        auto result = tracker.getNearestSafeBreadcrumb(glm::vec3(0.0f, 0.0f, 0.0f));
        REQUIRE(result.has_value());
        CHECK(result->x == doctest::Approx(20.0f));
    }
}
