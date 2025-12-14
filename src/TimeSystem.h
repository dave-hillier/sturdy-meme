#pragma once

#include <chrono>
#include <glm/glm.hpp>

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
class TimeSystem {
public:
    TimeSystem() = default;
    ~TimeSystem() = default;

    /**
     * Update time state and return timing data for this frame.
     * Should be called once per frame at the start of the render loop.
     *
     * @return TimingData with deltaTime, elapsedTime, and timeOfDay
     */
    TimingData update();

    // Time scale control (affects day/night cycle speed)
    void setTimeScale(float scale) { timeScale = scale; }
    float getTimeScale() const { return timeScale; }

    // Manual time control
    void setTimeOfDay(float time) { manualTime = glm::clamp(time, 0.0f, 1.0f); useManualTime = true; }
    void resumeAutoTime() { useManualTime = false; }
    float getTimeOfDay() const { return currentTimeOfDay; }
    bool isUsingManualTime() const { return useManualTime; }
    float getManualTime() const { return manualTime; }

    // Day cycle duration (seconds for a full day cycle in auto mode)
    void setCycleDuration(float seconds) { cycleDuration = seconds; }
    float getCycleDuration() const { return cycleDuration; }

    // Date tracking (for celestial calculations)
    void setDate(int year, int month, int day);
    int getCurrentYear() const { return currentYear; }
    int getCurrentMonth() const { return currentMonth; }
    int getCurrentDay() const { return currentDay; }

    // Moon phase override
    void setMoonPhaseOverride(bool enabled) { useMoonPhaseOverride = enabled; }
    bool isMoonPhaseOverrideEnabled() const { return useMoonPhaseOverride; }
    void setMoonPhase(float phase) { manualMoonPhase = glm::clamp(phase, 0.0f, 1.0f); }
    float getMoonPhase() const { return manualMoonPhase; }
    float getCurrentMoonPhase() const { return currentMoonPhase; }
    void setCurrentMoonPhase(float phase) { currentMoonPhase = phase; }

    // Moon brightness controls
    void setMoonBrightness(float brightness) { moonBrightness = glm::clamp(brightness, 0.0f, 5.0f); }
    float getMoonBrightness() const { return moonBrightness; }
    void setMoonDiscIntensity(float intensity) { moonDiscIntensity = glm::clamp(intensity, 0.0f, 50.0f); }
    float getMoonDiscIntensity() const { return moonDiscIntensity; }
    void setMoonEarthshine(float earthshine) { moonEarthshine = glm::clamp(earthshine, 0.0f, 0.2f); }
    float getMoonEarthshine() const { return moonEarthshine; }

    // Eclipse simulation
    void setEclipseEnabled(bool enabled) { eclipseEnabled = enabled; }
    bool isEclipseEnabled() const { return eclipseEnabled; }
    void setEclipseAmount(float amount) { eclipseAmount = glm::clamp(amount, 0.0f, 1.0f); }
    float getEclipseAmount() const { return eclipseAmount; }

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
    float timeScale = 1.0f;
    float cycleDuration = 120.0f;  // Full day cycle in seconds
    float manualTime = 0.5f;       // Noon by default
    bool useManualTime = true;     // Start with manual time
    float currentTimeOfDay = 0.5f;

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
    float moonDiscIntensity = 20.0f;   // Visual disc intensity in sky (0-50)
    float moonEarthshine = 0.02f;      // Earthshine on dark side (0-0.2)

    // Eclipse simulation
    bool eclipseEnabled = false;
    float eclipseAmount = 0.0f;        // 0 = no eclipse, 1 = total eclipse
};
