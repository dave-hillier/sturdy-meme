#pragma once

#include <memory>
#include <vulkan/vulkan.hpp>

// Forward declarations
class SkySystem;
class FroxelSystem;
class AtmosphereLUTSystem;
class CloudShadowSystem;

/**
 * AtmosphereSystemGroup - Groups atmosphere-related rendering systems
 *
 * This reduces coupling by providing a single interface to access
 * all atmosphere-related systems (sky, fog, LUTs, cloud shadows).
 *
 * Systems in this group:
 * - SkySystem: Sky dome and cloud rendering
 * - FroxelSystem: Volumetric fog with froxel-based scattering
 * - AtmosphereLUTSystem: Precomputed atmosphere LUTs
 * - CloudShadowSystem: Cloud shadow map generation
 *
 * Usage:
 *   auto& atmos = systems.atmosphere();
 *   atmos.sky().recordDraw(cmd, frameIndex);
 *   atmos.froxel().recordUpdate(cmd, ...);
 */
struct AtmosphereSystemGroup {
    // Non-owning references to systems (owned by RendererSystems)
    SkySystem* sky_ = nullptr;
    FroxelSystem* froxel_ = nullptr;
    AtmosphereLUTSystem* atmosphereLUT_ = nullptr;
    CloudShadowSystem* cloudShadow_ = nullptr;

    // Accessors
    SkySystem& sky() { return *sky_; }
    const SkySystem& sky() const { return *sky_; }

    FroxelSystem& froxel() { return *froxel_; }
    const FroxelSystem& froxel() const { return *froxel_; }

    AtmosphereLUTSystem& atmosphereLUT() { return *atmosphereLUT_; }
    const AtmosphereLUTSystem& atmosphereLUT() const { return *atmosphereLUT_; }

    CloudShadowSystem& cloudShadow() { return *cloudShadow_; }
    const CloudShadowSystem& cloudShadow() const { return *cloudShadow_; }

    // Validation
    bool isValid() const {
        return sky_ && froxel_ && atmosphereLUT_ && cloudShadow_;
    }
};
