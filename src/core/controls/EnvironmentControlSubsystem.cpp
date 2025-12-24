#include "EnvironmentControlSubsystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "LeafSystem.h"
#include "CloudShadowSystem.h"
#include "PostProcessSystem.h"
#include "EnvironmentSettings.h"
#include <glm/glm.hpp>

// Froxel volumetric fog
void EnvironmentControlSubsystem::setFogEnabled(bool enabled) {
    froxel_.setEnabled(enabled);
    postProcess_.setFroxelEnabled(enabled);
}

bool EnvironmentControlSubsystem::isFogEnabled() const {
    return froxel_.isEnabled();
}

void EnvironmentControlSubsystem::setFogDensity(float density) {
    froxel_.setFogDensity(density);
}

float EnvironmentControlSubsystem::getFogDensity() const {
    return froxel_.getFogDensity();
}

void EnvironmentControlSubsystem::setFogAbsorption(float absorption) {
    froxel_.setFogAbsorption(absorption);
}

float EnvironmentControlSubsystem::getFogAbsorption() const {
    return froxel_.getFogAbsorption();
}

void EnvironmentControlSubsystem::setFogBaseHeight(float height) {
    froxel_.setFogBaseHeight(height);
}

float EnvironmentControlSubsystem::getFogBaseHeight() const {
    return froxel_.getFogBaseHeight();
}

void EnvironmentControlSubsystem::setFogScaleHeight(float height) {
    froxel_.setFogScaleHeight(height);
}

float EnvironmentControlSubsystem::getFogScaleHeight() const {
    return froxel_.getFogScaleHeight();
}

void EnvironmentControlSubsystem::setVolumetricFarPlane(float farPlane) {
    froxel_.setVolumetricFarPlane(farPlane);
    postProcess_.setFroxelParams(farPlane, FroxelSystem::DEPTH_DISTRIBUTION);
}

float EnvironmentControlSubsystem::getVolumetricFarPlane() const {
    return froxel_.getVolumetricFarPlane();
}

void EnvironmentControlSubsystem::setTemporalBlend(float blend) {
    froxel_.setTemporalBlend(blend);
}

float EnvironmentControlSubsystem::getTemporalBlend() const {
    return froxel_.getTemporalBlend();
}

// Height fog layer
void EnvironmentControlSubsystem::setLayerHeight(float height) {
    froxel_.setLayerHeight(height);
}

float EnvironmentControlSubsystem::getLayerHeight() const {
    return froxel_.getLayerHeight();
}

void EnvironmentControlSubsystem::setLayerThickness(float thickness) {
    froxel_.setLayerThickness(thickness);
}

float EnvironmentControlSubsystem::getLayerThickness() const {
    return froxel_.getLayerThickness();
}

void EnvironmentControlSubsystem::setLayerDensity(float density) {
    froxel_.setLayerDensity(density);
}

float EnvironmentControlSubsystem::getLayerDensity() const {
    return froxel_.getLayerDensity();
}

// Atmospheric scattering
void EnvironmentControlSubsystem::setSkyExposure(float exposure) {
    skyExposure_ = glm::clamp(exposure, 1.0f, 20.0f);
}

float EnvironmentControlSubsystem::getSkyExposure() const {
    return skyExposure_;
}

void EnvironmentControlSubsystem::setAtmosphereParams(const AtmosphereParams& params) {
    atmosphereLUT_.setAtmosphereParams(params);
}

const AtmosphereParams& EnvironmentControlSubsystem::getAtmosphereParams() const {
    return atmosphereLUT_.getAtmosphereParams();
}

// Leaves/particles
void EnvironmentControlSubsystem::setLeafIntensity(float intensity) {
    leaf_.setIntensity(intensity);
}

float EnvironmentControlSubsystem::getLeafIntensity() const {
    return leaf_.getIntensity();
}

// Cloud style and parameters
void EnvironmentControlSubsystem::toggleCloudStyle() {
    useParaboloidClouds_ = !useParaboloidClouds_;
}

bool EnvironmentControlSubsystem::isUsingParaboloidClouds() const {
    return useParaboloidClouds_;
}

void EnvironmentControlSubsystem::setCloudCoverage(float coverage) {
    cloudCoverage_ = glm::clamp(coverage, 0.0f, 1.0f);
    cloudShadow_.setCloudCoverage(cloudCoverage_);
    atmosphereLUT_.setCloudCoverage(cloudCoverage_);
}

float EnvironmentControlSubsystem::getCloudCoverage() const {
    return cloudCoverage_;
}

void EnvironmentControlSubsystem::setCloudDensity(float density) {
    cloudDensity_ = glm::clamp(density, 0.0f, 1.0f);
    cloudShadow_.setCloudDensity(cloudDensity_);
    atmosphereLUT_.setCloudDensity(cloudDensity_);
}

float EnvironmentControlSubsystem::getCloudDensity() const {
    return cloudDensity_;
}

// Environment settings
EnvironmentSettings& EnvironmentControlSubsystem::getEnvironmentSettings() {
    return envSettings_;
}
