#pragma once

#include "SystemGroupMacros.h"
#include "InitContext.h"

#include <memory>
#include <optional>
#include <vulkan/vulkan.h>

// Forward declarations
class SnowMaskSystem;
class VolumetricSnowSystem;
class WeatherSystem;
class LeafSystem;
class RendererSystems;
class ResizeCoordinator;

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
 *
 * Self-initialization:
 *   auto bundle = SnowSystemGroup::createAll(deps);
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

    // ========================================================================
    // Factory methods for self-initialization
    // ========================================================================

    /**
     * Bundle of all snow/weather systems (owned pointers).
     */
    struct Bundle {
        std::unique_ptr<SnowMaskSystem> snowMask;
        std::unique_ptr<VolumetricSnowSystem> volumetricSnow;
        std::unique_ptr<WeatherSystem> weather;
        std::unique_ptr<LeafSystem> leaf;

        void registerAll(RendererSystems& systems);
    };

    /**
     * Dependencies required to create snow/weather systems.
     */
    struct CreateDeps {
        const InitContext& ctx;
        VkRenderPass hdrRenderPass;
    };

    /**
     * Factory: Create all snow and weather systems.
     * Returns nullopt on failure.
     */
    static std::optional<Bundle> createAll(const CreateDeps& deps);

    static bool createAndRegister(const CreateDeps& deps, RendererSystems& systems);

    /** Register snow/weather systems for resize. */
    static void registerResize(ResizeCoordinator& coord, RendererSystems& systems);
};
