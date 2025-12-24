#pragma once

/**
 * Interface for time, date, and celestial controls.
 * Implemented by TimeSystem - handles time-of-day, moon phase, eclipse settings.
 *
 * Note: Location controls are in ILocationControl (implemented by Renderer).
 */
class ITimeSystem {
public:
    virtual ~ITimeSystem() = default;

    // Time of day (0.0 = midnight, 0.5 = noon, 1.0 = midnight)
    virtual void setTimeOfDay(float time) = 0;
    virtual float getTimeOfDay() const = 0;

    // Time scale (simulation speed multiplier)
    virtual void setTimeScale(float scale) = 0;
    virtual float getTimeScale() const = 0;
    virtual void resumeAutoTime() = 0;

    // Date controls (affects sun position via astronomical calculations)
    virtual void setDate(int year, int month, int day) = 0;
    virtual int getCurrentYear() const = 0;
    virtual int getCurrentMonth() const = 0;
    virtual int getCurrentDay() const = 0;

    // Moon phase controls
    virtual void setMoonPhaseOverride(bool enabled) = 0;
    virtual bool isMoonPhaseOverrideEnabled() const = 0;
    virtual void setMoonPhase(float phase) = 0;
    virtual float getMoonPhase() const = 0;
    virtual float getCurrentMoonPhase() const = 0;

    // Moon brightness controls
    virtual void setMoonBrightness(float brightness) = 0;
    virtual float getMoonBrightness() const = 0;
    virtual void setMoonDiscIntensity(float intensity) = 0;
    virtual float getMoonDiscIntensity() const = 0;
    virtual void setMoonEarthshine(float earthshine) = 0;
    virtual float getMoonEarthshine() const = 0;

    // Eclipse controls
    virtual void setEclipseEnabled(bool enabled) = 0;
    virtual bool isEclipseEnabled() const = 0;
    virtual void setEclipseAmount(float amount) = 0;
    virtual float getEclipseAmount() const = 0;
};
