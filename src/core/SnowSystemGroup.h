#pragma once

#include <memory>
#include <vulkan/vulkan.h>

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
    SnowMaskSystem* mask_ = nullptr;
    VolumetricSnowSystem* volumetric_ = nullptr;
    WeatherSystem* weather_ = nullptr;
    LeafSystem* leaf_ = nullptr;

    // Accessors
    SnowMaskSystem& mask() { return *mask_; }
    const SnowMaskSystem& mask() const { return *mask_; }

    VolumetricSnowSystem& volumetric() { return *volumetric_; }
    const VolumetricSnowSystem& volumetric() const { return *volumetric_; }

    WeatherSystem& weather() { return *weather_; }
    const WeatherSystem& weather() const { return *weather_; }

    LeafSystem& leaf() { return *leaf_; }
    const LeafSystem& leaf() const { return *leaf_; }

    // Validation
    bool isValid() const {
        return mask_ && volumetric_ && weather_ && leaf_;
    }
};
