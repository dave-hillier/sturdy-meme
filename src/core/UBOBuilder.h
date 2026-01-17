#pragma once

#include <glm/glm.hpp>
#include "UBOs.h"

// Forward declarations
class Camera;
class TimeSystem;
class CelestialCalculator;
class ShadowSystem;
class WindSystem;
class AtmosphereLUTSystem;
class FroxelSystem;
class SceneManager;
class SnowMaskSystem;
class VolumetricSnowSystem;
class CloudShadowSystem;
struct EnvironmentSettings;
struct DateTime;

/**
 * UBOBuilder - Builds Uniform Buffer Object data structures
 *
 * Pure calculation class that extracts UBO building logic from Renderer.
 * Takes const references to the various systems needed and produces
 * populated UBO structs ready for upload to GPU.
 *
 * This class has no state mutation - all methods are const.
 */
class UBOBuilder {
public:
    // Lighting parameters computed from celestial positions
    struct LightingParams {
        glm::vec3 sunDir;
        glm::vec3 moonDir;
        float sunIntensity;
        float moonIntensity;
        glm::vec3 sunColor;
        glm::vec3 moonColor;
        glm::vec3 ambientColor;
        float moonPhase;       // Moon phase (0 = new moon, 0.5 = full moon, 1 = new moon)
        float eclipseAmount;   // Eclipse amount (0 = none, 1 = total solar eclipse)
        double julianDay;
    };

    // Configuration for snow UBO building
    struct SnowConfig {
        bool useVolumetricSnow = true;
        bool showSnowDepthDebug = false;
        float maxSnowHeight = 150.0f;
    };

    // Configuration for main UBO building
    struct MainUBOConfig {
        bool showCascadeDebug = false;
        bool useParaboloidClouds = true;
        float cloudCoverage = 0.5f;
        float cloudDensity = 0.3f;
        float skyExposure = 5.0f;
        bool shadowsEnabled = true;  // Performance toggle for shadow sampling
    };

    // Systems references for building - set once and reused
    struct Systems {
        const TimeSystem* timeSystem = nullptr;
        const CelestialCalculator* celestialCalculator = nullptr;
        const ShadowSystem* shadowSystem = nullptr;
        const WindSystem* windSystem = nullptr;
        const AtmosphereLUTSystem* atmosphereLUTSystem = nullptr;
        const FroxelSystem* froxelSystem = nullptr;
        const SceneManager* sceneManager = nullptr;
        const SnowMaskSystem* snowMaskSystem = nullptr;
        const VolumetricSnowSystem* volumetricSnowSystem = nullptr;
        const CloudShadowSystem* cloudShadowSystem = nullptr;
        const EnvironmentSettings* environmentSettings = nullptr;
    };

    UBOBuilder() = default;
    explicit UBOBuilder(const Systems& systems);

    // Set systems references (alternative to constructor)
    void setSystems(const Systems& systems);

    // Calculate lighting parameters from celestial positions
    // Pure calculation - no state mutation
    LightingParams calculateLightingParams(float timeOfDay) const;

    // Build main UBO data
    // Pure calculation - no state mutation
    UniformBufferObject buildUniformBufferData(
        const Camera& camera,
        const LightingParams& lighting,
        float timeOfDay,
        const MainUBOConfig& config) const;

    // Build snow UBO data
    // Pure calculation - no state mutation
    SnowUBO buildSnowUBOData(const SnowConfig& config) const;

    // Build cloud shadow UBO data
    // Pure calculation - no state mutation
    CloudShadowUBO buildCloudShadowUBOData() const;

private:
    Systems systems_;
};
