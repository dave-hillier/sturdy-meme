#pragma once

struct GeographicLocation;

/**
 * Interface for geographic location controls.
 * Implemented by Renderer - affects sun/moon position calculations.
 *
 * Note: Time controls are in ITimeSystem (implemented by TimeSystem).
 */
class ILocationControl {
public:
    virtual ~ILocationControl() = default;

    // Geographic location (affects sun/moon position)
    virtual void setLocation(const GeographicLocation& location) = 0;
    virtual const GeographicLocation& getLocation() const = 0;
};
