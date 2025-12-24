#pragma once

#include <cstdint>

/**
 * Interface for weather state (rain/snow type and intensity).
 * WeatherSystem implements this directly.
 */
class IWeatherState {
public:
    virtual ~IWeatherState() = default;

    // Weather type (0 = Rain, 1 = Snow)
    virtual void setWeatherType(uint32_t type) = 0;
    virtual uint32_t getWeatherType() const = 0;

    // Weather intensity (0.0 = clear, 1.0 = heavy)
    virtual void setIntensity(float intensity) = 0;
    virtual float getIntensity() const = 0;
};
