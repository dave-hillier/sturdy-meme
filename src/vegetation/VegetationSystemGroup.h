#pragma once

#include "SystemGroupMacros.h"
#include "InitContext.h"
#include "RockSystem.h"  // For RockConfig

#include <memory>
#include <optional>
#include <functional>
#include <vulkan/vulkan.h>

// Forward declarations
class GrassSystem;
class WindSystem;
class DisplacementSystem;
class TreeSystem;
class TreeRenderer;
class TreeLODSystem;
class ImpostorCullSystem;
class DetritusSystem;
class RockSystem;

/**
 * VegetationSystemGroup - Groups vegetation-related rendering systems
 *
 * This reduces coupling by providing a single interface to access
 * all vegetation-related systems (grass, trees, rocks, detritus).
 *
 * Systems in this group:
 * - GrassSystem: Procedural grass with wind animation
 * - WindSystem: Global wind simulation
 * - TreeSystem: Tree mesh data and instances
 * - TreeRenderer: Tree rendering with wind animation
 * - TreeLODSystem: Impostor generation and LOD management
 * - ImpostorCullSystem: GPU-driven impostor culling
 * - DetritusSystem: Fallen branches and debris
 * - RockSystem: Static rock geometry
 *
 * Usage:
 *   auto& veg = systems.vegetation();
 *   veg.grass().recordDraw(cmd, frameIndex, time);
 *   if (veg.hasTreeRenderer()) {
 *       veg.treeRenderer().render(cmd, frameIndex, *veg.tree());
 *   }
 *
 * Self-initialization:
 *   auto bundle = VegetationSystemGroup::createAll(deps);
 *   // Move systems to RendererSystems, then use VegetationContentGenerator for content
 */
struct VegetationSystemGroup {
    // Non-owning references to systems (owned by RendererSystems)
    SYSTEM_MEMBER(GrassSystem, grass);
    SYSTEM_MEMBER(WindSystem, wind);
    SYSTEM_MEMBER(DisplacementSystem, displacement);
    SYSTEM_MEMBER(TreeSystem, tree);
    SYSTEM_MEMBER(TreeRenderer, treeRenderer);
    SYSTEM_MEMBER(TreeLODSystem, treeLOD);
    SYSTEM_MEMBER(ImpostorCullSystem, impostorCull);
    SYSTEM_MEMBER(DetritusSystem, detritus);
    SYSTEM_MEMBER(RockSystem, rock);

    // Required system accessors
    REQUIRED_SYSTEM_ACCESSORS(GrassSystem, grass)
    REQUIRED_SYSTEM_ACCESSORS(WindSystem, wind)
    REQUIRED_SYSTEM_ACCESSORS(DisplacementSystem, displacement)
    REQUIRED_SYSTEM_ACCESSORS(RockSystem, rock)

    // Optional system accessors (may be null)
    OPTIONAL_SYSTEM_ACCESSORS(TreeSystem, tree, Tree)
    OPTIONAL_SYSTEM_ACCESSORS(TreeRenderer, treeRenderer, TreeRenderer)
    OPTIONAL_SYSTEM_ACCESSORS(TreeLODSystem, treeLOD, TreeLOD)
    OPTIONAL_SYSTEM_ACCESSORS(ImpostorCullSystem, impostorCull, ImpostorCull)
    OPTIONAL_SYSTEM_ACCESSORS(DetritusSystem, detritus, Detritus)

    // Validation (only required systems)
    bool isValid() const {
        return grass_ && wind_ && displacement_ && rock_;
    }

    // ========================================================================
    // Factory methods for self-initialization
    // ========================================================================

    /**
     * Bundle of all vegetation-related systems (owned pointers).
     */
    struct Bundle {
        std::unique_ptr<GrassSystem> grass;
        std::unique_ptr<WindSystem> wind;
        std::unique_ptr<DisplacementSystem> displacement;
        std::unique_ptr<RockSystem> rock;
        std::unique_ptr<TreeSystem> tree;
        std::unique_ptr<TreeRenderer> treeRenderer;
        std::unique_ptr<TreeLODSystem> treeLOD;
        std::unique_ptr<ImpostorCullSystem> impostorCull;
        // Note: DetritusSystem needs tree positions, created separately after content generation
    };

    using HeightFunc = std::function<float(float, float)>;

    /**
     * Dependencies required to create vegetation systems.
     */
    struct CreateDeps {
        const InitContext& ctx;
        VkRenderPass hdrRenderPass;
        VkRenderPass shadowRenderPass;
        uint32_t shadowMapSize;
        float terrainSize;
        HeightFunc getTerrainHeight;
        RockConfig rockConfig;  // Rock generation config
    };

    /**
     * Factory: Create all vegetation systems.
     * Returns nullopt if required systems fail.
     *
     * Note: Content generation (trees, impostors) should be done via
     * VegetationContentGenerator after systems are stored in RendererSystems.
     */
    static std::optional<Bundle> createAll(const CreateDeps& deps);
};
