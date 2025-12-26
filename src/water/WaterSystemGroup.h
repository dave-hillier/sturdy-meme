#pragma once

#include <memory>
#include <vulkan/vulkan.h>

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
    WaterSystem* system_ = nullptr;
    WaterDisplacement* displacement_ = nullptr;
    FlowMapGenerator* flowMap_ = nullptr;
    FoamBuffer* foam_ = nullptr;
    SSRSystem* ssr_ = nullptr;
    WaterTileCull* tileCull_ = nullptr;
    WaterGBuffer* gBuffer_ = nullptr;

    // Required system accessors
    WaterSystem& system() { return *system_; }
    const WaterSystem& system() const { return *system_; }

    WaterDisplacement& displacement() { return *displacement_; }
    const WaterDisplacement& displacement() const { return *displacement_; }

    FlowMapGenerator& flowMap() { return *flowMap_; }
    const FlowMapGenerator& flowMap() const { return *flowMap_; }

    FoamBuffer& foam() { return *foam_; }
    const FoamBuffer& foam() const { return *foam_; }

    SSRSystem& ssr() { return *ssr_; }
    const SSRSystem& ssr() const { return *ssr_; }

    // Optional system accessors (may be null)
    WaterTileCull* tileCull() { return tileCull_; }
    const WaterTileCull* tileCull() const { return tileCull_; }
    bool hasTileCull() const { return tileCull_ != nullptr; }

    WaterGBuffer* gBuffer() { return gBuffer_; }
    const WaterGBuffer* gBuffer() const { return gBuffer_; }
    bool hasGBuffer() const { return gBuffer_ != nullptr; }

    // Validation (only required systems)
    bool isValid() const {
        return system_ && displacement_ && flowMap_ && foam_ && ssr_;
    }
};
