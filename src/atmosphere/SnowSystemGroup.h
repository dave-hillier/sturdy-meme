#pragma once

#include "SystemGroupMacros.h"

// Forward declarations
class SnowMaskSystem;
class VolumetricSnowSystem;
class WeatherSystem;
class LeafSystem;

/**
 * SnowSystemGroup - Groups snow and weather-related rendering systems
 *
 * This reduces coupling by providing a single interface to access
 * all snow and weather-related systems.
 *
 * Systems in this group:
 * - SnowMaskSystem: Snow accumulation mask
 * - VolumetricSnowSystem: Volumetric snow rendering
 * - WeatherSystem: Rain/snow particles
 * - LeafSystem: Leaf/confetti particles (affected by wind/weather)
 *
 * Usage:
 *   auto& snow = systems.snow();
 *   snow.mask().recordCompute(cmd, frameIndex);
 *   snow.volumetric().recordCompute(cmd, frameIndex);
 */
struct SnowSystemGroup {
    // Non-owning references to systems (owned by RendererSystems)
    SYSTEM_MEMBER(SnowMaskSystem, mask);
    SYSTEM_MEMBER(VolumetricSnowSystem, volumetric);
    SYSTEM_MEMBER(WeatherSystem, weather);
    SYSTEM_MEMBER(LeafSystem, leaf);

    // Accessors
    REQUIRED_SYSTEM_ACCESSORS(SnowMaskSystem, mask)
    REQUIRED_SYSTEM_ACCESSORS(VolumetricSnowSystem, volumetric)
    REQUIRED_SYSTEM_ACCESSORS(WeatherSystem, weather)
    REQUIRED_SYSTEM_ACCESSORS(LeafSystem, leaf)

    // Validation
    bool isValid() const {
        return mask_ && volumetric_ && weather_ && leaf_;
    }
};
