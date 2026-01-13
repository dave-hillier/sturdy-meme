#pragma once

#include "SystemGroupMacros.h"

// Forward declarations
class WaterSystem;
class WaterDisplacement;
class FlowMapGenerator;
class FoamBuffer;
class SSRSystem;
class WaterTileCull;
class WaterGBuffer;

/**
 * WaterSystemGroup - Groups water-related rendering systems
 *
 * This reduces coupling by providing a single interface to access
 * all water-related systems (water surface, FFT displacement, SSR).
 *
 * Systems in this group:
 * - WaterSystem: Main water surface rendering
 * - WaterDisplacement: FFT-based wave displacement
 * - FlowMapGenerator: Flow map for UV distortion
 * - FoamBuffer: Foam texture persistence
 * - SSRSystem: Screen-space reflections
 * - WaterTileCull: Water tile culling
 * - WaterGBuffer: Water G-buffer for deferred effects
 *
 * Usage:
 *   auto& water = systems.water();
 *   water.displacement().recordCompute(cmd, frameIndex);
 *   water.system().recordDraw(cmd, frameIndex);
 */
struct WaterSystemGroup {
    // Non-owning references to systems (owned by RendererSystems)
    SYSTEM_MEMBER(WaterSystem, system);
    SYSTEM_MEMBER(WaterDisplacement, displacement);
    SYSTEM_MEMBER(FlowMapGenerator, flowMap);
    SYSTEM_MEMBER(FoamBuffer, foam);
    SYSTEM_MEMBER(SSRSystem, ssr);
    SYSTEM_MEMBER(WaterTileCull, tileCull);
    SYSTEM_MEMBER(WaterGBuffer, gBuffer);

    // Required system accessors
    REQUIRED_SYSTEM_ACCESSORS(WaterSystem, system)
    REQUIRED_SYSTEM_ACCESSORS(WaterDisplacement, displacement)
    REQUIRED_SYSTEM_ACCESSORS(FlowMapGenerator, flowMap)
    REQUIRED_SYSTEM_ACCESSORS(FoamBuffer, foam)
    REQUIRED_SYSTEM_ACCESSORS(SSRSystem, ssr)

    // Optional system accessors (may be null)
    OPTIONAL_SYSTEM_ACCESSORS(WaterTileCull, tileCull, TileCull)
    OPTIONAL_SYSTEM_ACCESSORS(WaterGBuffer, gBuffer, GBuffer)

    // Validation (only required systems)
    bool isValid() const {
        return system_ && displacement_ && flowMap_ && foam_ && ssr_;
    }
};
