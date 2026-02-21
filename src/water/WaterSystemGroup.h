#pragma once

#include "SystemGroupMacros.h"
#include "InitContext.h"

#include <memory>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

// Forward declarations
struct TerrainConfig;
class WaterSystem;
class WaterDisplacement;
class FlowMapGenerator;
class FoamBuffer;
class SSRSystem;
class WaterTileCull;
class WaterGBuffer;
class ShadowSystem;
class TerrainSystem;
class PostProcessSystem;
class RendererSystems;
class ResizeCoordinator;

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
 *       bundle->registerAll(systems);
 *   }
 *
 * Configuration (after systems are stored in RendererSystems):
 *   WaterSystemGroup::configureSubsystems(systems, terrainConfig);
 *   WaterSystemGroup::createDescriptorSets(systems, ...);
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

        void registerAll(RendererSystems& systems);
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

    static bool createAndRegister(const CreateDeps& deps, RendererSystems& systems);

    /** Register water systems for resize and temporal history. */
    static void registerResize(ResizeCoordinator& coord, RendererSystems& systems);
    static void registerTemporalSystems(RendererSystems& systems);

    // ========================================================================
    // Configuration methods (call after systems are in RendererSystems)
    // ========================================================================

    /**
     * Configure water subsystems with terrain-derived parameters.
     * Sets water level, extent, wave properties, and generates flow map.
     */
    static bool configureSubsystems(
        RendererSystems& systems,
        const TerrainConfig& terrainConfig
    );

    /**
     * Create descriptor sets for water rendering.
     * Must be called after configureSubsystems().
     */
    static bool createDescriptorSets(
        RendererSystems& systems,
        const std::vector<VkBuffer>& uniformBuffers,
        size_t uniformBufferSize,
        ShadowSystem& shadowSystem,
        const TerrainSystem& terrainSystem,
        const PostProcessSystem& postProcessSystem,
        VkSampler depthSampler
    );
};
