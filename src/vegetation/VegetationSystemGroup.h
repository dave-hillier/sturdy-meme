#pragma once

#include <memory>
#include <vulkan/vulkan.h>

// Forward declarations
class GrassSystem;
class WindSystem;
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
 */
struct VegetationSystemGroup {
    // Non-owning references to systems (owned by RendererSystems)
    GrassSystem* grass_ = nullptr;
    WindSystem* wind_ = nullptr;
    TreeSystem* tree_ = nullptr;
    TreeRenderer* treeRenderer_ = nullptr;
    TreeLODSystem* treeLOD_ = nullptr;
    ImpostorCullSystem* impostorCull_ = nullptr;
    DetritusSystem* detritus_ = nullptr;
    RockSystem* rock_ = nullptr;

    // Required system accessors
    GrassSystem& grass() { return *grass_; }
    const GrassSystem& grass() const { return *grass_; }

    WindSystem& wind() { return *wind_; }
    const WindSystem& wind() const { return *wind_; }

    RockSystem& rock() { return *rock_; }
    const RockSystem& rock() const { return *rock_; }

    // Optional system accessors (may be null)
    TreeSystem* tree() { return tree_; }
    const TreeSystem* tree() const { return tree_; }
    bool hasTree() const { return tree_ != nullptr; }

    TreeRenderer* treeRenderer() { return treeRenderer_; }
    const TreeRenderer* treeRenderer() const { return treeRenderer_; }
    bool hasTreeRenderer() const { return treeRenderer_ != nullptr; }

    TreeLODSystem* treeLOD() { return treeLOD_; }
    const TreeLODSystem* treeLOD() const { return treeLOD_; }
    bool hasTreeLOD() const { return treeLOD_ != nullptr; }

    ImpostorCullSystem* impostorCull() { return impostorCull_; }
    const ImpostorCullSystem* impostorCull() const { return impostorCull_; }
    bool hasImpostorCull() const { return impostorCull_ != nullptr; }

    DetritusSystem* detritus() { return detritus_; }
    const DetritusSystem* detritus() const { return detritus_; }
    bool hasDetritus() const { return detritus_ != nullptr; }

    // Validation (only required systems)
    bool isValid() const {
        return grass_ && wind_ && rock_;
    }
};
