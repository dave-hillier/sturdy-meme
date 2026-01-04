#include "TimeSystem.h"
#include <cmath>

TimingData TimeSystem::update() {
    auto currentTime = Clock::now();

    // Initialize on first call
    if (!initialized) {
        startTime = currentTime;
        lastFrameTime = currentTime;
        initialized = true;
    }

    // Calculate elapsed time since start
    float elapsedTime = std::chrono::duration<float>(currentTime - startTime).count();

    // Calculate delta time since last frame
    float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
    lastFrameTime = currentTime;

    // Store for accessor methods
    lastDeltaTime = deltaTime;
    lastElapsedTime = elapsedTime;

    // Update time of day incrementally based on timeScale
    // This allows smooth progression at any speed without jumps when changing scale
    if (timeScale > 0.0f) {
        float timeIncrement = (deltaTime * timeScale) / cycleDuration;
        currentTimeOfDay = std::fmod(currentTimeOfDay + timeIncrement, 1.0f);
        if (currentTimeOfDay < 0.0f) currentTimeOfDay += 1.0f;
    }
    // When timeScale is 0, time is paused at current position

    // Return timing data for this frame
    TimingData timing;
    timing.deltaTime = deltaTime;
    timing.elapsedTime = elapsedTime;
    timing.timeOfDay = currentTimeOfDay;

    return timing;
}

void TimeSystem::setDate(int year, int month, int day) {
    currentYear = year;
    currentMonth = month;
    currentDay = day;
}
