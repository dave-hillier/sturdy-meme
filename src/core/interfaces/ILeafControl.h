#pragma once

/**
 * Interface for leaf/particle intensity control.
 * LeafSystem implements this directly.
 */
class ILeafControl {
public:
    virtual ~ILeafControl() = default;

    virtual void setIntensity(float intensity) = 0;
    virtual float getIntensity() const = 0;
};
