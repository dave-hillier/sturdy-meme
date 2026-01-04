#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/epsilon.hpp>

// Include just the AnimationSampler template (header-only)
// We'll test the template directly without needing the full animation system
#include "animation/Animation.h"

TEST_SUITE("AnimationSampler<vec3>") {
    TEST_CASE("empty sampler returns default") {
        AnimationSampler<glm::vec3> sampler;
        glm::vec3 result = sampler.sample(0.5f);
        CHECK(result == glm::vec3(0.0f));
    }

    TEST_CASE("single keyframe returns that value") {
        AnimationSampler<glm::vec3> sampler;
        sampler.times = {0.0f};
        sampler.values = {glm::vec3(1.0f, 2.0f, 3.0f)};

        CHECK(sampler.sample(0.0f) == glm::vec3(1.0f, 2.0f, 3.0f));
        CHECK(sampler.sample(-1.0f) == glm::vec3(1.0f, 2.0f, 3.0f));
        CHECK(sampler.sample(10.0f) == glm::vec3(1.0f, 2.0f, 3.0f));
    }

    TEST_CASE("two keyframes interpolate linearly") {
        AnimationSampler<glm::vec3> sampler;
        sampler.times = {0.0f, 1.0f};
        sampler.values = {
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(10.0f, 20.0f, 30.0f)
        };

        // At start
        glm::vec3 start = sampler.sample(0.0f);
        CHECK(start.x == doctest::Approx(0.0f));
        CHECK(start.y == doctest::Approx(0.0f));
        CHECK(start.z == doctest::Approx(0.0f));

        // At end
        glm::vec3 end = sampler.sample(1.0f);
        CHECK(end.x == doctest::Approx(10.0f));
        CHECK(end.y == doctest::Approx(20.0f));
        CHECK(end.z == doctest::Approx(30.0f));

        // At middle
        glm::vec3 mid = sampler.sample(0.5f);
        CHECK(mid.x == doctest::Approx(5.0f));
        CHECK(mid.y == doctest::Approx(10.0f));
        CHECK(mid.z == doctest::Approx(15.0f));

        // At 25%
        glm::vec3 quarter = sampler.sample(0.25f);
        CHECK(quarter.x == doctest::Approx(2.5f));
        CHECK(quarter.y == doctest::Approx(5.0f));
        CHECK(quarter.z == doctest::Approx(7.5f));
    }

    TEST_CASE("clamping before first keyframe") {
        AnimationSampler<glm::vec3> sampler;
        sampler.times = {1.0f, 2.0f};
        sampler.values = {
            glm::vec3(100.0f, 0.0f, 0.0f),
            glm::vec3(200.0f, 0.0f, 0.0f)
        };

        // Before first keyframe should return first value
        glm::vec3 result = sampler.sample(0.0f);
        CHECK(result.x == doctest::Approx(100.0f));
    }

    TEST_CASE("clamping after last keyframe") {
        AnimationSampler<glm::vec3> sampler;
        sampler.times = {0.0f, 1.0f};
        sampler.values = {
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(100.0f, 0.0f, 0.0f)
        };

        // After last keyframe should return last value
        glm::vec3 result = sampler.sample(10.0f);
        CHECK(result.x == doctest::Approx(100.0f));
    }

    TEST_CASE("multiple keyframes") {
        AnimationSampler<glm::vec3> sampler;
        sampler.times = {0.0f, 1.0f, 2.0f, 3.0f};
        sampler.values = {
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(10.0f, 0.0f, 0.0f),
            glm::vec3(10.0f, 10.0f, 0.0f),
            glm::vec3(10.0f, 10.0f, 10.0f)
        };

        // Between 0 and 1
        glm::vec3 t05 = sampler.sample(0.5f);
        CHECK(t05.x == doctest::Approx(5.0f));
        CHECK(t05.y == doctest::Approx(0.0f));

        // Between 1 and 2
        glm::vec3 t15 = sampler.sample(1.5f);
        CHECK(t15.x == doctest::Approx(10.0f));
        CHECK(t15.y == doctest::Approx(5.0f));
        CHECK(t15.z == doctest::Approx(0.0f));

        // Between 2 and 3
        glm::vec3 t25 = sampler.sample(2.5f);
        CHECK(t25.x == doctest::Approx(10.0f));
        CHECK(t25.y == doctest::Approx(10.0f));
        CHECK(t25.z == doctest::Approx(5.0f));
    }
}

TEST_SUITE("AnimationSampler<quat>") {
    TEST_CASE("empty sampler returns identity") {
        AnimationSampler<glm::quat> sampler;
        glm::quat result = sampler.sample(0.5f);
        CHECK(result.w == doctest::Approx(1.0f));
        CHECK(result.x == doctest::Approx(0.0f));
        CHECK(result.y == doctest::Approx(0.0f));
        CHECK(result.z == doctest::Approx(0.0f));
    }

    TEST_CASE("single keyframe returns that value") {
        AnimationSampler<glm::quat> sampler;
        glm::quat rot = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
        sampler.times = {0.0f};
        sampler.values = {rot};

        glm::quat result = sampler.sample(0.5f);
        CHECK(result.w == doctest::Approx(rot.w).epsilon(0.0001));
        CHECK(result.x == doctest::Approx(rot.x).epsilon(0.0001));
        CHECK(result.y == doctest::Approx(rot.y).epsilon(0.0001));
        CHECK(result.z == doctest::Approx(rot.z).epsilon(0.0001));
    }

    TEST_CASE("quaternion SLERP interpolation") {
        AnimationSampler<glm::quat> sampler;

        // Rotate from identity to 90 degrees around Y
        glm::quat identity = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::quat rot90 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));

        sampler.times = {0.0f, 1.0f};
        sampler.values = {identity, rot90};

        // At middle should be 45 degree rotation
        glm::quat mid = sampler.sample(0.5f);

        // Apply the rotation to a forward vector
        glm::vec3 forward(0, 0, 1);
        glm::vec3 rotated = mid * forward;

        // 45 degrees around Y: forward vector becomes ~(0.707, 0, 0.707)
        CHECK(rotated.x == doctest::Approx(0.7071f).epsilon(0.01));
        CHECK(rotated.y == doctest::Approx(0.0f).epsilon(0.01));
        CHECK(rotated.z == doctest::Approx(0.7071f).epsilon(0.01));
    }

    TEST_CASE("quaternion clamping") {
        AnimationSampler<glm::quat> sampler;

        glm::quat start = glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0));
        glm::quat end = glm::angleAxis(glm::radians(60.0f), glm::vec3(0, 1, 0));

        sampler.times = {1.0f, 2.0f};
        sampler.values = {start, end};

        // Before first keyframe
        glm::quat before = sampler.sample(0.0f);
        CHECK(before.w == doctest::Approx(start.w).epsilon(0.0001));

        // After last keyframe
        glm::quat after = sampler.sample(10.0f);
        CHECK(after.w == doctest::Approx(end.w).epsilon(0.0001));
    }

    TEST_CASE("quaternion interpolation preserves unit length") {
        AnimationSampler<glm::quat> sampler;

        glm::quat q1 = glm::angleAxis(glm::radians(45.0f), glm::vec3(1, 0, 0));
        glm::quat q2 = glm::angleAxis(glm::radians(135.0f), glm::vec3(0, 1, 0));

        sampler.times = {0.0f, 1.0f};
        sampler.values = {q1, q2};

        // Sample at various points
        for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
            glm::quat result = sampler.sample(t);
            float len = glm::length(result);
            CHECK(len == doctest::Approx(1.0f).epsilon(0.0001));
        }
    }
}

TEST_SUITE("AnimationChannel") {
    TEST_CASE("channel component flags") {
        AnimationChannel channel;
        channel.jointIndex = 0;

        // Initially empty
        CHECK(channel.hasTranslation() == false);
        CHECK(channel.hasRotation() == false);
        CHECK(channel.hasScale() == false);

        // Add translation data
        channel.translation.times = {0.0f};
        channel.translation.values = {glm::vec3(0.0f)};
        CHECK(channel.hasTranslation() == true);
        CHECK(channel.hasRotation() == false);
        CHECK(channel.hasScale() == false);

        // Add rotation data
        channel.rotation.times = {0.0f};
        channel.rotation.values = {glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
        CHECK(channel.hasTranslation() == true);
        CHECK(channel.hasRotation() == true);
        CHECK(channel.hasScale() == false);

        // Add scale data
        channel.scale.times = {0.0f};
        channel.scale.values = {glm::vec3(1.0f)};
        CHECK(channel.hasTranslation() == true);
        CHECK(channel.hasRotation() == true);
        CHECK(channel.hasScale() == true);
    }
}

TEST_SUITE("AnimationClip") {
    TEST_CASE("root motion speed calculation") {
        AnimationClip clip;
        clip.duration = 2.0f;
        clip.rootMotionPerCycle = glm::vec3(4.0f, 0.0f, 3.0f);  // 5 units horizontal distance

        float speed = clip.getRootMotionSpeed();
        CHECK(speed == doctest::Approx(2.5f));  // 5 units / 2 seconds
    }

    TEST_CASE("root motion speed with zero duration") {
        AnimationClip clip;
        clip.duration = 0.0f;
        clip.rootMotionPerCycle = glm::vec3(10.0f, 0.0f, 0.0f);

        float speed = clip.getRootMotionSpeed();
        CHECK(speed == doctest::Approx(0.0f));
    }

    TEST_CASE("add events keeps them sorted") {
        AnimationClip clip;
        clip.duration = 3.0f;

        clip.addEvent("event3", 2.5f);
        clip.addEvent("event1", 0.5f);
        clip.addEvent("event2", 1.5f);

        REQUIRE(clip.events.size() == 3);
        CHECK(clip.events[0].time == doctest::Approx(0.5f));
        CHECK(clip.events[0].name == "event1");
        CHECK(clip.events[1].time == doctest::Approx(1.5f));
        CHECK(clip.events[1].name == "event2");
        CHECK(clip.events[2].time == doctest::Approx(2.5f));
        CHECK(clip.events[2].name == "event3");
    }

    TEST_CASE("addEventNormalized converts to absolute time") {
        AnimationClip clip;
        clip.duration = 4.0f;

        clip.addEventNormalized("halfway", 0.5f);
        clip.addEventNormalized("start", 0.0f);
        clip.addEventNormalized("end", 1.0f);

        REQUIRE(clip.events.size() == 3);
        CHECK(clip.events[0].time == doctest::Approx(0.0f));
        CHECK(clip.events[1].time == doctest::Approx(2.0f));  // 0.5 * 4.0
        CHECK(clip.events[2].time == doctest::Approx(4.0f));  // 1.0 * 4.0
    }

    TEST_CASE("getEventsInRange") {
        AnimationClip clip;
        clip.duration = 10.0f;

        clip.addEvent("e1", 1.0f);
        clip.addEvent("e2", 2.0f);
        clip.addEvent("e3", 3.0f);
        clip.addEvent("e4", 4.0f);
        clip.addEvent("e5", 5.0f);

        // Range that includes some events (exclusive start, inclusive end)
        auto events = clip.getEventsInRange(1.5f, 3.5f);
        CHECK(events.size() == 2);
        CHECK(events[0]->name == "e2");
        CHECK(events[1]->name == "e3");

        // Range that includes exactly one event
        events = clip.getEventsInRange(0.5f, 1.0f);
        CHECK(events.size() == 1);
        CHECK(events[0]->name == "e1");

        // Range with no events
        events = clip.getEventsInRange(5.5f, 9.0f);
        CHECK(events.size() == 0);

        // Boundary test: start is exclusive
        events = clip.getEventsInRange(2.0f, 3.0f);
        CHECK(events.size() == 1);
        CHECK(events[0]->name == "e3");  // e2 at 2.0 is excluded, e3 at 3.0 is included
    }
}
