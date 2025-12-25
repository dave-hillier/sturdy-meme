#pragma once

#include <glm/glm.hpp>

struct AtmosphereParams;
struct EnvironmentSettings;

/**
 * Interface for environment rendering controls.
 * Used by GuiEnvironmentTab to control fog, atmosphere, clouds, and leaves.
 */
class IEnvironmentControl {
public:
    virtual ~IEnvironmentControl() = default;

    // Froxel volumetric fog
    virtual void setFogEnabled(bool enabled) = 0;
    virtual bool isFogEnabled() const = 0;
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

    // Atmospheric scattering
    virtual void setSkyExposure(float exposure) = 0;
    virtual float getSkyExposure() const = 0;
    virtual void setAtmosphereParams(const AtmosphereParams& params) = 0;
    virtual const AtmosphereParams& getAtmosphereParams() const = 0;

    // Leaves/particles
    virtual void setLeafIntensity(float intensity) = 0;
    virtual float getLeafIntensity() const = 0;
    virtual void spawnConfetti(const glm::vec3& position, float velocity = 8.0f, float count = 100.0f, float coneAngle = 0.5f) = 0;

    // Cloud style and parameters
    virtual void toggleCloudStyle() = 0;
    virtual bool isUsingParaboloidClouds() const = 0;
    virtual void setCloudCoverage(float coverage) = 0;
    virtual float getCloudCoverage() const = 0;
    virtual void setCloudDensity(float density) = 0;
    virtual float getCloudDensity() const = 0;

    // Environment settings (shared with weather)
    virtual EnvironmentSettings& getEnvironmentSettings() = 0;
};
