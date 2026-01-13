#pragma once

#include "SystemGroupMacros.h"

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
    SYSTEM_MEMBER(SkySystem, sky);
    SYSTEM_MEMBER(FroxelSystem, froxel);
    SYSTEM_MEMBER(AtmosphereLUTSystem, atmosphereLUT);
    SYSTEM_MEMBER(CloudShadowSystem, cloudShadow);

    // Accessors
    REQUIRED_SYSTEM_ACCESSORS(SkySystem, sky)
    REQUIRED_SYSTEM_ACCESSORS(FroxelSystem, froxel)
    REQUIRED_SYSTEM_ACCESSORS(AtmosphereLUTSystem, atmosphereLUT)
    REQUIRED_SYSTEM_ACCESSORS(CloudShadowSystem, cloudShadow)

    // Validation
    bool isValid() const {
        return sky_ && froxel_ && atmosphereLUT_ && cloudShadow_;
    }
};
