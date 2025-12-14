#pragma once

#include "HDRResources.h"
#include "ShadowResources.h"
#include "TerrainResources.h"

/**
 * CoreResources - Combined resources from tier-1 systems
 *
 * After initializing PostProcessSystem, ShadowSystem, and TerrainSystem,
 * their resources are collected here and passed to tier-2+ systems.
 * This decouples systems from each other - they only depend on the resources,
 * not the systems that created them.
 *
 * For finer-grained dependencies, include the individual resource headers directly:
 *   - HDRResources.h    - just HDR render pass/framebuffer
 *   - ShadowResources.h - just shadow maps
 *   - TerrainResources.h - just terrain heightmap
 *
 * Usage:
 *   // After tier-1 init
 *   CoreResources core = CoreResources::collect(postProcess, shadow, terrain, framesInFlight);
 *
 *   // Pass to tier-2 systems
 *   grassSystem.init(ctx, core.hdr, core.shadow, core.terrain);
 */
struct CoreResources {
    HDRResources hdr;
    ShadowResources shadow;
    TerrainResources terrain;

    bool isValid() const {
        return hdr.isValid() && shadow.isValid();
        // terrain is optional for some systems
    }

    // Collect all resources from tier-1 systems
    static CoreResources collect(
        const PostProcessSystem& postProcess,
        const ShadowSystem& shadow,
        const TerrainSystem& terrain,
        uint32_t framesInFlight
    );
};
