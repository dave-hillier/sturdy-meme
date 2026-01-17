#pragma once

#include "SystemGroupMacros.h"
#include "InitContext.h"

#include <memory>
#include <optional>
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
 *
 * Self-initialization:
 *   auto bundle = WaterSystemGroup::createAll(deps);
 *   if (bundle) {
 *       systems.setWater(std::move(bundle->system));
 *       // ... etc
 *   }
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

    // ========================================================================
    // Factory methods for self-initialization
    // ========================================================================

    /**
     * Bundle of all water-related systems (owned pointers).
     * Used during initialization - systems are moved to RendererSystems after creation.
     */
    struct Bundle {
        std::unique_ptr<WaterSystem> system;
        std::unique_ptr<WaterDisplacement> displacement;
        std::unique_ptr<FlowMapGenerator> flowMap;
        std::unique_ptr<FoamBuffer> foam;
        std::unique_ptr<SSRSystem> ssr;
        std::unique_ptr<WaterTileCull> tileCull;      // Optional
        std::unique_ptr<WaterGBuffer> gBuffer;        // Optional
    };

    /**
     * Dependencies required to create water systems.
     */
    struct CreateDeps {
        const InitContext& ctx;
        VkRenderPass hdrRenderPass;
        float waterSize = 65536.0f;
        std::string assetPath;
    };

    /**
     * Factory: Create all water systems with proper initialization order.
     * Returns nullopt on failure of required systems.
     *
     * Note: SSR is created but not fully initialized here - it needs
     * additional wiring after other systems are ready.
     */
    static std::optional<Bundle> createAll(const CreateDeps& deps);
};
