#pragma once

#include "SystemGroupMacros.h"
#include "InitContext.h"

#include <memory>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

// Forward declarations
class SkySystem;
class FroxelSystem;
class AtmosphereLUTSystem;
class CloudShadowSystem;
class PostProcessSystem;
class RendererSystems;

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
 *
 * Self-initialization:
 *   auto bundle = AtmosphereSystemGroup::createAll(deps);
 *   if (bundle) {
 *       bundle->registerAll(systems);
 *   }
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

    // ========================================================================
    // Factory methods for self-initialization
    // ========================================================================

    /**
     * Bundle of all atmosphere-related systems (owned pointers).
     * Used during initialization - systems are moved to RendererSystems after creation.
     */
    struct Bundle {
        std::unique_ptr<SkySystem> sky;
        std::unique_ptr<FroxelSystem> froxel;
        std::unique_ptr<AtmosphereLUTSystem> atmosphereLUT;
        std::unique_ptr<CloudShadowSystem> cloudShadow;

        void registerAll(RendererSystems& systems);
    };

    /**
     * Dependencies required to create atmosphere systems.
     * Avoids passing many parameters through factory methods.
     */
    struct CreateDeps {
        const InitContext& ctx;
        VkRenderPass hdrRenderPass;        // For SkySystem
        VkImageView shadowMapView;         // For FroxelSystem (cascade shadows)
        VkSampler shadowSampler;           // For FroxelSystem
        const std::vector<VkBuffer>& lightBuffers;  // For FroxelSystem
    };

    /**
     * Factory: Create all atmosphere systems with proper initialization order.
     * Returns nullopt on failure.
     *
     * Creation order (respects dependencies):
     * 1. AtmosphereLUTSystem - no dependencies, computes LUTs
     * 2. FroxelSystem - needs shadow resources
     * 3. CloudShadowSystem - needs AtmosphereLUT cloud map
     * 4. SkySystem - needs HDR render pass
     *
     * Note: LUT computation happens inside this factory.
     */
    static std::optional<Bundle> createAll(const CreateDeps& deps);

    /**
     * Create all atmosphere systems and register them in RendererSystems.
     * Combines createAll() + registerAll() so callers don't need concrete type includes.
     */
    static bool createAndRegister(const CreateDeps& deps, RendererSystems& systems);

    /**
     * Wire atmosphere systems to dependent systems.
     * Call after createAll() and after systems are stored in RendererSystems.
     *
     * Wiring performed:
     * - PostProcessSystem gets froxel volume for compositing
     */
    static void wireToPostProcess(
        FroxelSystem& froxel,
        PostProcessSystem& postProcess);
};
