#pragma once

#include "interfaces/IEnvironmentControl.h"

class FroxelSystem;
class AtmosphereLUTSystem;
class LeafSystem;
class CloudShadowSystem;
class PostProcessSystem;
struct EnvironmentSettings;
struct AtmosphereParams;

/**
 * EnvironmentControlSubsystem - Implements IEnvironmentControl
 * Coordinates fog, atmosphere, clouds, and leaf particle systems.
 */
class EnvironmentControlSubsystem : public IEnvironmentControl {
public:
    EnvironmentControlSubsystem(FroxelSystem& froxel,
                                 AtmosphereLUTSystem& atmosphereLUT,
                                 LeafSystem& leaf,
                                 CloudShadowSystem& cloudShadow,
                                 PostProcessSystem& postProcess,
                                 EnvironmentSettings& envSettings)
        : froxel_(froxel)
        , atmosphereLUT_(atmosphereLUT)
        , leaf_(leaf)
        , cloudShadow_(cloudShadow)
        , postProcess_(postProcess)
        , envSettings_(envSettings) {}

    // Froxel volumetric fog
    void setFogEnabled(bool enabled) override;
    bool isFogEnabled() const override;
    void setFogDensity(float density) override;
    float getFogDensity() const override;
    void setFogAbsorption(float absorption) override;
    float getFogAbsorption() const override;
    void setFogBaseHeight(float height) override;
    float getFogBaseHeight() const override;
    void setFogScaleHeight(float height) override;
    float getFogScaleHeight() const override;
    void setVolumetricFarPlane(float farPlane) override;
    float getVolumetricFarPlane() const override;
    void setTemporalBlend(float blend) override;
    float getTemporalBlend() const override;

    // Height fog layer
    void setLayerHeight(float height) override;
    float getLayerHeight() const override;
    void setLayerThickness(float thickness) override;
    float getLayerThickness() const override;
    void setLayerDensity(float density) override;
    float getLayerDensity() const override;

    // Atmospheric scattering
    void setSkyExposure(float exposure) override;
    float getSkyExposure() const override;
    void setAtmosphereParams(const AtmosphereParams& params) override;
    const AtmosphereParams& getAtmosphereParams() const override;

    // Leaves/particles
    void setLeafIntensity(float intensity) override;
    float getLeafIntensity() const override;

    // Cloud style and parameters
    void toggleCloudStyle() override;
    bool isUsingParaboloidClouds() const override;
    void setCloudCoverage(float coverage) override;
    float getCloudCoverage() const override;
    void setCloudDensity(float density) override;
    float getCloudDensity() const override;

    // Environment settings
    EnvironmentSettings& getEnvironmentSettings() override;

private:
    FroxelSystem& froxel_;
    AtmosphereLUTSystem& atmosphereLUT_;
    LeafSystem& leaf_;
    CloudShadowSystem& cloudShadow_;
    PostProcessSystem& postProcess_;
    EnvironmentSettings& envSettings_;

    // Local state for cloud parameters
    float cloudCoverage_ = 0.5f;
    float cloudDensity_ = 0.3f;
    float skyExposure_ = 5.0f;
    bool useParaboloidClouds_ = true;
};
