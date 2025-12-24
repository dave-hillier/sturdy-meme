#pragma once

/**
 * Interface for cloud shadow controls.
 * CloudShadowSystem implements this directly.
 */
class ICloudShadowControl {
public:
    virtual ~ICloudShadowControl() = default;

    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled() const = 0;
    virtual void setShadowIntensity(float intensity) = 0;
    virtual float getShadowIntensity() const = 0;
};
