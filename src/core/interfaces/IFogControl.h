#pragma once

/**
 * Interface for volumetric fog controls.
 * FroxelSystem implements this directly.
 */
class IFogControl {
public:
    virtual ~IFogControl() = default;

    // Enable/disable
    virtual void setEnabled(bool enabled) = 0;
    virtual bool isEnabled() const = 0;

    // Fog parameters
    virtual void setFogDensity(float density) = 0;
    virtual float getFogDensity() const = 0;
    virtual void setFogAbsorption(float absorption) = 0;
    virtual float getFogAbsorption() const = 0;
    virtual void setFogBaseHeight(float height) = 0;
    virtual float getFogBaseHeight() const = 0;
    virtual void setFogScaleHeight(float height) = 0;
    virtual float getFogScaleHeight() const = 0;
    virtual void setVolumetricFarPlane(float farPlane) = 0;
    virtual float getVolumetricFarPlane() const = 0;
    virtual void setTemporalBlend(float blend) = 0;
    virtual float getTemporalBlend() const = 0;

    // Height fog layer
    virtual void setLayerHeight(float height) = 0;
    virtual float getLayerHeight() const = 0;
    virtual void setLayerThickness(float thickness) = 0;
    virtual float getLayerThickness() const = 0;
    virtual void setLayerDensity(float density) = 0;
    virtual float getLayerDensity() const = 0;
};
