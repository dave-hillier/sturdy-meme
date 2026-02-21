#pragma once

#include "SystemGroupMacros.h"
#include "InitContext.h"
#include "CatmullClarkSystem.h"  // For CatmullClarkConfig

#include <memory>
#include <optional>
#include <functional>
#include <vulkan/vulkan.h>

// Forward declarations
class CatmullClarkSystem;
class RendererSystems;
class ResizeCoordinator;

/**
 * GeometrySystemGroup - Groups procedural geometry systems
 *
 * This reduces coupling by providing a single interface to access
 * procedural geometry systems (subdivision, mesh processing).
 *
 * Systems in this group:
 * - CatmullClarkSystem: Adaptive Catmull-Clark subdivision with CBT
 *
 * Usage:
 *   auto& geom = systems.geometry();
 *   geom.catmullClark().recordCompute(cmd, frameIndex);
 *   geom.catmullClark().recordDraw(cmd, frameIndex);
 *
 * Self-initialization:
 *   auto bundle = GeometrySystemGroup::createAll(deps);
 *   if (bundle) {
 *       bundle->registerAll(systems);
 *   }
 */
struct GeometrySystemGroup {
    // Non-owning references to systems (owned by RendererSystems)
    SYSTEM_MEMBER(CatmullClarkSystem, catmullClark);

    // Required system accessors
    REQUIRED_SYSTEM_ACCESSORS(CatmullClarkSystem, catmullClark)

    // Validation
    bool isValid() const {
        return catmullClark_;
    }

    // ========================================================================
    // Factory methods for self-initialization
    // ========================================================================

    /**
     * Bundle of all geometry-related systems (owned pointers).
     * Used during initialization - systems are moved to RendererSystems after creation.
     */
    struct Bundle {
        std::unique_ptr<CatmullClarkSystem> catmullClark;

        void registerAll(RendererSystems& systems);
    };

    using HeightFunc = std::function<float(float, float)>;

    /**
     * Dependencies required to create geometry systems.
     * Avoids passing many parameters through factory methods.
     */
    struct CreateDeps {
        const InitContext& ctx;
        VkRenderPass hdrRenderPass;
        const std::vector<VkBuffer>& uniformBuffers;  // For descriptor updates
        std::string resourcePath;                      // For loading assets (e.g., suzanne.obj)
        HeightFunc getTerrainHeight;                   // For placing objects on terrain
        CatmullClarkConfig catmullClarkConfig = {};    // Optional config override
    };

    /**
     * Factory: Create all geometry systems with proper initialization.
     * Returns nullopt on failure.
     *
     * Creation steps:
     * 1. CatmullClarkSystem - adaptive subdivision mesh
     * 2. Update descriptor sets with uniform buffers
     *
     * Note: Object position is computed using terrain height if getTerrainHeight is provided.
     */
    static std::optional<Bundle> createAll(const CreateDeps& deps);

    static bool createAndRegister(const CreateDeps& deps, RendererSystems& systems);

    /** Register geometry systems for resize. */
    static void registerResize(ResizeCoordinator& coord, RendererSystems& systems);
};
