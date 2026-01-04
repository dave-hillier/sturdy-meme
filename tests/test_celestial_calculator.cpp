#include <doctest/doctest.h>
#include "atmosphere/CelestialCalculator.h"
#include <glm/gtc/constants.hpp>
#include <cmath>

// Test DateTime structure
TEST_SUITE("DateTime") {
    TEST_CASE("fromTimeOfDay basic conversion") {
        // Midnight
        DateTime midnight = DateTime::fromTimeOfDay(0.0f);
        CHECK(midnight.hour == 0);
        CHECK(midnight.minute == 0);
        CHECK(midnight.second == doctest::Approx(0.0).epsilon(0.01));

        // Noon
        DateTime noon = DateTime::fromTimeOfDay(0.5f);
        CHECK(noon.hour == 12);
        CHECK(noon.minute == 0);
        CHECK(noon.second == doctest::Approx(0.0).epsilon(0.01));

        // 6 AM (0.25 of day)
        DateTime sixAM = DateTime::fromTimeOfDay(0.25f);
        CHECK(sixAM.hour == 6);
        CHECK(sixAM.minute == 0);

        // 6 PM (0.75 of day)
        DateTime sixPM = DateTime::fromTimeOfDay(0.75f);
        CHECK(sixPM.hour == 18);
        CHECK(sixPM.minute == 0);
    }

    TEST_CASE("fromTimeOfDay with minutes") {
        // 6:30 AM = 6.5 hours = 6.5/24 = 0.2708...
        DateTime dt = DateTime::fromTimeOfDay(6.5f / 24.0f);
        CHECK(dt.hour == 6);
        CHECK(dt.minute == 30);
    }

    TEST_CASE("getFractionalHour") {
        DateTime dt;
        dt.year = 2024;
        dt.month = 6;
        dt.day = 21;
        dt.hour = 14;
        dt.minute = 30;
        dt.second = 0.0;

        CHECK(dt.getFractionalHour() == doctest::Approx(14.5));

        dt.minute = 45;
        CHECK(dt.getFractionalHour() == doctest::Approx(14.75));

        dt.second = 30.0;
        CHECK(dt.getFractionalHour() == doctest::Approx(14.75 + 30.0/3600.0));
    }

    TEST_CASE("toJulianDay known dates") {
        // January 1, 2000 at noon is JD 2451545.0 (J2000.0 epoch)
        DateTime j2000;
        j2000.year = 2000;
        j2000.month = 1;
        j2000.day = 1;
        j2000.hour = 12;
        j2000.minute = 0;
        j2000.second = 0.0;

        CHECK(j2000.toJulianDay() == doctest::Approx(2451545.0).epsilon(0.0001));

        // January 1, 1970 (Unix epoch) at noon is approximately JD 2440588.0
        DateTime unixEpoch;
        unixEpoch.year = 1970;
        unixEpoch.month = 1;
        unixEpoch.day = 1;
        unixEpoch.hour = 12;
        unixEpoch.minute = 0;
        unixEpoch.second = 0.0;

        CHECK(unixEpoch.toJulianDay() == doctest::Approx(2440588.0).epsilon(0.01));

        // June 21, 2024 (summer solstice) noon
        DateTime solstice;
        solstice.year = 2024;
        solstice.month = 6;
        solstice.day = 21;
        solstice.hour = 12;
        solstice.minute = 0;
        solstice.second = 0.0;

        // Should be approximately JD 2460483.0
        CHECK(solstice.toJulianDay() == doctest::Approx(2460483.0).epsilon(0.01));
    }

    TEST_CASE("toJulianDay preserves time of day") {
        DateTime morning;
        morning.year = 2024;
        morning.month = 6;
        morning.day = 21;
        morning.hour = 6;
        morning.minute = 0;
        morning.second = 0.0;

        DateTime evening = morning;
        evening.hour = 18;

        // 12 hours difference = 0.5 Julian days
        double diff = evening.toJulianDay() - morning.toJulianDay();
        CHECK(diff == doctest::Approx(0.5).epsilon(0.0001));
    }
}

// Test GeographicLocation presets
TEST_SUITE("GeographicLocation") {
    TEST_CASE("preset locations") {
        auto london = GeographicLocation::London();
        CHECK(london.latitude == doctest::Approx(51.5074).epsilon(0.001));
        CHECK(london.longitude == doctest::Approx(-0.1278).epsilon(0.001));

        auto tokyo = GeographicLocation::Tokyo();
        CHECK(tokyo.latitude == doctest::Approx(35.6762).epsilon(0.001));
        CHECK(tokyo.longitude == doctest::Approx(139.6503).epsilon(0.001));

        auto sydney = GeographicLocation::Sydney();
        CHECK(sydney.latitude < 0);  // Southern hemisphere
        CHECK(sydney.longitude > 0);  // Eastern hemisphere

        auto nyc = GeographicLocation::NewYork();
        CHECK(nyc.latitude > 0);  // Northern hemisphere
        CHECK(nyc.longitude < 0);  // Western hemisphere
    }
}

// Test CelestialCalculator
TEST_SUITE("CelestialCalculator") {
    TEST_CASE("default location is London") {
        CelestialCalculator calc;
        auto loc = calc.getLocation();
        CHECK(loc.latitude == doctest::Approx(51.5074).epsilon(0.001));
    }

    TEST_CASE("setLocation changes location") {
        CelestialCalculator calc;
        calc.setLocation(GeographicLocation::Tokyo());
        auto loc = calc.getLocation();
        CHECK(loc.latitude == doctest::Approx(35.6762).epsilon(0.001));
    }

    TEST_CASE("sun position at noon is high") {
        CelestialCalculator calc;
        calc.setLocation(GeographicLocation::London());

        // Summer solstice noon - sun should be high
        DateTime noon = DateTime::fromTimeOfDay(0.5f, 2024, 6, 21);
        auto pos = calc.calculateSunPosition(noon);

        // At London on summer solstice, sun altitude at noon should be ~62 degrees
        CHECK(pos.altitude > 50.0f);
        CHECK(pos.altitude < 70.0f);

        // Direction should point upward (positive Y)
        CHECK(pos.direction.y > 0.5f);

        // Intensity should be high at noon
        CHECK(pos.intensity > 0.8f);
    }

    TEST_CASE("sun position at midnight is below horizon") {
        CelestialCalculator calc;
        calc.setLocation(GeographicLocation::London());

        // Summer solstice midnight
        DateTime midnight = DateTime::fromTimeOfDay(0.0f, 2024, 6, 21);
        auto pos = calc.calculateSunPosition(midnight);

        // Sun should be below horizon at midnight
        CHECK(pos.altitude < 0.0f);

        // Intensity should be zero or very low
        CHECK(pos.intensity < 0.3f);
    }

    TEST_CASE("sun rises in east sets in west") {
        CelestialCalculator calc;
        calc.setLocation(GeographicLocation::London());

        // Morning (6 AM)
        DateTime morning = DateTime::fromTimeOfDay(0.25f, 2024, 6, 21);
        auto morningPos = calc.calculateSunPosition(morning);

        // Evening (6 PM)
        DateTime evening = DateTime::fromTimeOfDay(0.75f, 2024, 6, 21);
        auto eveningPos = calc.calculateSunPosition(evening);

        // Morning azimuth should be ~East (around 90 degrees or less)
        // Note: actual sunrise position varies by season
        CHECK(morningPos.azimuth < 180.0f);

        // Evening azimuth should be ~West (around 270 degrees or more)
        CHECK(eveningPos.azimuth > 180.0f);
    }

    TEST_CASE("moon phase cycle") {
        CelestialCalculator calc;

        // Check that moon phase changes over time
        DateTime day1 = DateTime::fromTimeOfDay(0.5f, 2024, 1, 1);
        DateTime day15 = DateTime::fromTimeOfDay(0.5f, 2024, 1, 15);
        DateTime day29 = DateTime::fromTimeOfDay(0.5f, 2024, 1, 29);

        auto moon1 = calc.calculateMoonPosition(day1);
        auto moon15 = calc.calculateMoonPosition(day15);
        auto moon29 = calc.calculateMoonPosition(day29);

        // Phases should be different
        CHECK(moon1.phase != doctest::Approx(moon15.phase).epsilon(0.1));

        // ~29 days later should be close to same phase (synodic month)
        CHECK(moon29.phase == doctest::Approx(moon1.phase).epsilon(0.1));
    }

    TEST_CASE("moon illumination matches phase") {
        CelestialCalculator calc;

        // New moon should have low illumination
        // Full moon should have high illumination
        // Find approximate new moon (phase ~0) and full moon (phase ~0.5)

        DateTime dt = DateTime::fromTimeOfDay(0.5f, 2024, 1, 1);

        float minIllum = 1.0f;
        float maxIllum = 0.0f;
        float phaseAtMinIllum = 0.0f;
        float phaseAtMaxIllum = 0.0f;

        // Check over a month
        for (int day = 1; day <= 30; day++) {
            dt.day = day;
            auto moon = calc.calculateMoonPosition(dt);
            if (moon.illumination < minIllum) {
                minIllum = moon.illumination;
                phaseAtMinIllum = moon.phase;
            }
            if (moon.illumination > maxIllum) {
                maxIllum = moon.illumination;
                phaseAtMaxIllum = moon.phase;
            }
        }

        // Minimum illumination should be near phase 0 or 1
        bool nearNewMoon = (phaseAtMinIllum < 0.15f || phaseAtMinIllum > 0.85f);
        CHECK(nearNewMoon);

        // Maximum illumination should be near phase 0.5
        CHECK(phaseAtMaxIllum == doctest::Approx(0.5f).epsilon(0.15));
    }

    TEST_CASE("sun color varies with altitude") {
        CelestialCalculator calc;

        // Low sun (horizon) should be more orange/red
        glm::vec3 horizonColor = calc.getSunColor(0.0f);

        // High sun (zenith) should be more white
        glm::vec3 zenithColor = calc.getSunColor(60.0f);

        // Horizon should have lower blue component relative to red
        CHECK(horizonColor.b < horizonColor.r);

        // Zenith should be more balanced/white
        CHECK(zenithColor.b > horizonColor.b);
    }

    TEST_CASE("ambient color varies with sun altitude") {
        CelestialCalculator calc;

        glm::vec3 nightAmbient = calc.getAmbientColor(-30.0f);
        glm::vec3 dayAmbient = calc.getAmbientColor(45.0f);

        // Day ambient should be brighter than night
        float nightBrightness = (nightAmbient.r + nightAmbient.g + nightAmbient.b) / 3.0f;
        float dayBrightness = (dayAmbient.r + dayAmbient.g + dayAmbient.b) / 3.0f;

        CHECK(dayBrightness > nightBrightness);
    }

    TEST_CASE("tide cycle") {
        CelestialCalculator calc;
        calc.setLocation(GeographicLocation::London());

        // Check that tide height varies over time
        std::vector<float> heights;
        DateTime dt = DateTime::fromTimeOfDay(0.0f, 2024, 6, 21);

        // Sample every hour for 24 hours
        for (int hour = 0; hour < 24; hour++) {
            dt.hour = hour;
            auto tide = calc.calculateTide(dt);
            heights.push_back(tide.height);

            // Height should be in valid range
            CHECK(tide.height >= -1.0f);
            CHECK(tide.height <= 1.0f);

            // Range should be positive
            CHECK(tide.range > 0.0f);
            CHECK(tide.range <= 1.0f);
        }

        // Should see variation (not all same height)
        float minHeight = *std::min_element(heights.begin(), heights.end());
        float maxHeight = *std::max_element(heights.begin(), heights.end());
        CHECK(maxHeight - minHeight > 0.5f);  // Expect significant tidal range
    }

    TEST_CASE("altAzToDirection produces unit vectors") {
        CelestialCalculator calc;
        calc.setLocation(GeographicLocation::London());

        // Test multiple times of day
        for (float timeOfDay = 0.0f; timeOfDay < 1.0f; timeOfDay += 0.1f) {
            DateTime dt = DateTime::fromTimeOfDay(timeOfDay, 2024, 6, 21);
            auto sunPos = calc.calculateSunPosition(dt);
            auto moonPos = calc.calculateMoonPosition(dt);

            // Directions should be unit vectors
            float sunLen = glm::length(sunPos.direction);
            float moonLen = glm::length(moonPos.direction);

            CHECK(sunLen == doctest::Approx(1.0f).epsilon(0.0001));
            CHECK(moonLen == doctest::Approx(1.0f).epsilon(0.0001));
        }
    }
}
