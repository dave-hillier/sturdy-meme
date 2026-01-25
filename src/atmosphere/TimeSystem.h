#pragma once

#include <chrono>
#include <glm/glm.hpp>
#include "core/interfaces/ITimeSystem.h"

/**
 * TimingData - Time-related values for a single frame
 *
 * Returned by TimeSystem::update() and used to populate FrameData
 * and other systems that need timing information.
 */
struct TimingData {
    float deltaTime = 0.0f;      // Time since last frame (seconds)
    float elapsedTime = 0.0f;    // Total elapsed time since start (seconds)
    float timeOfDay = 0.5f;      // Normalized day/night cycle [0, 1] where 0.5 is noon
};

/**
 * TimeSystem - Manages all time-related state and calculations
 *
 * Extracted from Renderer to centralize time management. Handles:
 * - Frame timing (delta time, elapsed time)
 * - Day/night cycle with manual or automatic progression
 * - Date tracking for celestial calculations
 * - Moon phase override controls
 *
 * Usage:
 *   TimeSystem timeSystem;
 *   // In render loop:
 *   TimingData timing = timeSystem.update();
 *   // Use timing.deltaTime, timing.elapsedTime, timing.timeOfDay
 */
class TimeSystem : public ITimeSystem {
public:
    TimeSystem() = default;
    ~TimeSystem() override = default;

    /**
     * Update time state and return timing data for this frame.
     * Should be called once per frame at the start of the render loop.
     *
     * @return TimingData with deltaTime, elapsedTime, and timeOfDay
     */
    TimingData update();

    // ITimeSystem implementation - Time scale control
    void setTimeScale(float scale) override { timeScale = scale; }
    float getTimeScale() const override { return timeScale; }

    // ITimeSystem implementation - Time of day control
    // setTimeOfDay: jumps to a specific time and pauses (timeScale = 0)
    // Use setTimeScale() after to resume progression
    void setTimeOfDay(float time) override {
        currentTimeOfDay = glm::clamp(time, 0.0f, 1.0f);
        timeScale = 0.0f;  // Pause so user can see the set time
    }
    void resumeAutoTime() override { if (timeScale == 0.0f) timeScale = 1.0f; }
    float getTimeOfDay() const override { return currentTimeOfDay; }

    // Day cycle duration (seconds for a full day cycle in auto mode)
    void setCycleDuration(float seconds) { cycleDuration = seconds; }
    float getCycleDuration() const { return cycleDuration; }

    // ITimeSystem implementation - Date tracking
    void setDate(int year, int month, int day) override;
    int getCurrentYear() const override { return currentYear; }
    int getCurrentMonth() const override { return currentMonth; }
    int getCurrentDay() const override { return currentDay; }

    // ITimeSystem implementation - Moon phase override
    void setMoonPhaseOverride(bool enabled) override { useMoonPhaseOverride = enabled; }
    bool isMoonPhaseOverrideEnabled() const override { return useMoonPhaseOverride; }
    void setMoonPhase(float phase) override { manualMoonPhase = glm::clamp(phase, 0.0f, 1.0f); }
    float getMoonPhase() const override { return manualMoonPhase; }
    float getCurrentMoonPhase() const override { return currentMoonPhase; }
    void setCurrentMoonPhase(float phase) { currentMoonPhase = phase; }

    // ITimeSystem implementation - Moon brightness controls
    void setMoonBrightness(float brightness) override { moonBrightness = glm::clamp(brightness, 0.0f, 5.0f); }
    float getMoonBrightness() const override { return moonBrightness; }
    void setMoonDiscIntensity(float intensity) override { moonDiscIntensity = glm::clamp(intensity, 0.0f, 50.0f); }
    float getMoonDiscIntensity() const override { return moonDiscIntensity; }
    void setMoonEarthshine(float earthshine) override { moonEarthshine = glm::clamp(earthshine, 0.0f, 0.2f); }
    float getMoonEarthshine() const override { return moonEarthshine; }

    // ITimeSystem implementation - Eclipse simulation
    void setEclipseEnabled(bool enabled) override { eclipseEnabled = enabled; }
    bool isEclipseEnabled() const override { return eclipseEnabled; }
    void setEclipseAmount(float amount) override { eclipseAmount = glm::clamp(amount, 0.0f, 1.0f); }
    float getEclipseAmount() const override { return eclipseAmount; }

    // Access to raw timing values (for systems that need more control)
    float getDeltaTime() const { return lastDeltaTime; }
    float getElapsedTime() const { return lastElapsedTime; }

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    // Frame timing state
    bool initialized = false;
    TimePoint startTime;
    TimePoint lastFrameTime;
    float lastDeltaTime = 0.0f;
    float lastElapsedTime = 0.0f;

    // Day/night cycle
    float timeScale = 0.0f;        // Start paused (0 = paused, 1 = real-time, higher = faster)
    float cycleDuration = 120.0f;  // Full day cycle in seconds at timeScale=1
    float currentTimeOfDay = 0.5f; // Noon by default

    // Date for celestial calculations
    int currentYear = 2024;
    int currentMonth = 6;
    int currentDay = 21;  // Summer solstice by default

    // Moon phase override
    bool useMoonPhaseOverride = false;
    float manualMoonPhase = 0.5f;  // Default to full moon (0=new, 0.5=full, 1=new)
    float currentMoonPhase = 0.5f;

    // Moon brightness controls
    float moonBrightness = 1.0f;       // Multiplier for moon light intensity (0-5)
    float moonDiscIntensity = 12.0f;   // Visual disc intensity in sky (0-50)
    float moonEarthshine = 0.02f;      // Earthshine on dark side (0-0.2)

    // Eclipse simulation
    bool eclipseEnabled = false;
    float eclipseAmount = 0.0f;        // 0 = no eclipse, 1 = total eclipse
};
