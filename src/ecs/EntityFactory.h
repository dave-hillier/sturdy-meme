#pragma once

#include "World.h"
#include "Components.h"
#include "core/RenderableBuilder.h"

namespace ecs {

// =============================================================================
// Entity Factory
// =============================================================================
// Provides helpers for creating ECS entities from existing data structures.
// This bridges the gap between the current Renderable-based system and ECS.

class EntityFactory {
public:
    explicit EntityFactory(World& world) : world_(world) {}

    // -------------------------------------------------------------------------
    // Create entity from existing Renderable
    // -------------------------------------------------------------------------
    // Converts a Renderable struct into an ECS entity with appropriate components.
    // This is the primary migration path from the current rendering system.
    //
    // Components added:
    //   - Transform (always)
    //   - MeshRef (always)
    //   - MaterialRef (if valid material ID)
    //   - CastsShadow (if renderable casts shadows)
    //   - PBRProperties (if non-default values)
    //   - HueShift (if non-zero)
    //   - Opacity (if non-default)
    //   - TreeData (if tree-related indices set)

    [[nodiscard]] Entity createFromRenderable(const Renderable& renderable) {
        Entity entity = world_.create();

        // Core components - always present
        world_.add<Transform>(entity, renderable.transform);
        world_.add<MeshRef>(entity, renderable.mesh);

        // Material reference
        if (renderable.materialId != INVALID_MATERIAL_ID) {
            world_.add<MaterialRef>(entity, renderable.materialId);
        }

        // Shadow casting
        if (renderable.castsShadow) {
            world_.add<CastsShadow>(entity);
        }

        // PBR properties - only add if non-default
        if (hasCustomPBR(renderable)) {
            PBRProperties pbr;
            pbr.roughness = renderable.roughness;
            pbr.metallic = renderable.metallic;
            pbr.emissiveIntensity = renderable.emissiveIntensity;
            pbr.emissiveColor = renderable.emissiveColor;
            pbr.alphaTestThreshold = renderable.alphaTestThreshold;
            pbr.pbrFlags = renderable.pbrFlags;
            world_.add<PBRProperties>(entity, pbr);
        }

        // Hue shift for NPCs
        if (renderable.hueShift != 0.0f) {
            world_.add<HueShift>(entity, renderable.hueShift);
        }

        // Opacity for fade effects
        if (renderable.opacity != 1.0f) {
            world_.add<Opacity>(entity, renderable.opacity);
        }

        // Tree-specific data
        if (isTree(renderable)) {
            TreeData treeData;
            treeData.leafInstanceIndex = renderable.leafInstanceIndex;
            treeData.treeInstanceIndex = renderable.treeInstanceIndex;
            treeData.leafTint = renderable.leafTint;
            treeData.autumnHueShift = renderable.autumnHueShift;
            world_.add<TreeData>(entity, treeData);
        }

        return entity;
    }

    // -------------------------------------------------------------------------
    // Batch create entities from vector of Renderables
    // -------------------------------------------------------------------------
    // Returns a vector of entity handles corresponding to each renderable.

    [[nodiscard]] std::vector<Entity> createFromRenderables(
        const std::vector<Renderable>& renderables) {

        std::vector<Entity> entities;
        entities.reserve(renderables.size());

        for (const auto& renderable : renderables) {
            entities.push_back(createFromRenderable(renderable));
        }

        return entities;
    }

    // -------------------------------------------------------------------------
    // Create basic static mesh entity
    // -------------------------------------------------------------------------
    // Simplified factory for common static mesh objects.

    [[nodiscard]] Entity createStaticMesh(
        Mesh* mesh,
        MaterialId materialId,
        const glm::mat4& transform,
        bool castsShadow = true) {

        Entity entity = world_.create();
        world_.add<Transform>(entity, transform);
        world_.add<MeshRef>(entity, mesh);
        world_.add<MaterialRef>(entity, materialId);

        if (castsShadow) {
            world_.add<CastsShadow>(entity);
        }

        return entity;
    }

    // -------------------------------------------------------------------------
    // Create entity with bounding sphere for culling
    // -------------------------------------------------------------------------

    [[nodiscard]] Entity createWithBounds(
        Mesh* mesh,
        MaterialId materialId,
        const glm::mat4& transform,
        const glm::vec3& boundCenter,
        float boundRadius,
        bool castsShadow = true) {

        Entity entity = createStaticMesh(mesh, materialId, transform, castsShadow);
        world_.add<BoundingSphere>(entity, boundCenter, boundRadius);

        return entity;
    }

    // -------------------------------------------------------------------------
    // Create NPC entity
    // -------------------------------------------------------------------------
    // NPCs have hue shift for visual variety.

    [[nodiscard]] Entity createNPC(
        Mesh* mesh,
        MaterialId materialId,
        const glm::mat4& transform,
        float hueShift) {

        Entity entity = createStaticMesh(mesh, materialId, transform, true);
        world_.add<HueShift>(entity, hueShift);

        return entity;
    }

    // -------------------------------------------------------------------------
    // Create tree entity
    // -------------------------------------------------------------------------

    [[nodiscard]] Entity createTree(
        Mesh* mesh,
        MaterialId materialId,
        const glm::mat4& transform,
        int treeInstanceIndex,
        int leafInstanceIndex,
        const glm::vec3& leafTint = glm::vec3(1.0f),
        float autumnHueShift = 0.0f) {

        Entity entity = createStaticMesh(mesh, materialId, transform, true);

        TreeData treeData;
        treeData.treeInstanceIndex = treeInstanceIndex;
        treeData.leafInstanceIndex = leafInstanceIndex;
        treeData.leafTint = leafTint;
        treeData.autumnHueShift = autumnHueShift;
        world_.add<TreeData>(entity, treeData);

        return entity;
    }

    // -------------------------------------------------------------------------
    // Bone-attached entity creation
    // -------------------------------------------------------------------------
    // Creates an entity that follows a skeleton bone transform.

    [[nodiscard]] Entity createBoneAttached(
        Mesh* mesh,
        MaterialId materialId,
        int32_t boneIndex,
        const glm::mat4& localOffset = glm::mat4(1.0f),
        bool castsShadow = true) {

        Entity entity = world_.create();
        world_.add<Transform>(entity);  // Will be computed by bone attachment system
        world_.add<MeshRef>(entity, mesh);
        world_.add<MaterialRef>(entity, materialId);
        world_.add<BoneAttachment>(entity, boneIndex, localOffset);

        if (castsShadow) {
            world_.add<CastsShadow>(entity);
        }

        return entity;
    }

    // -------------------------------------------------------------------------
    // Child entity creation (hierarchical)
    // -------------------------------------------------------------------------
    // Creates an entity as a child of another entity.

    [[nodiscard]] Entity createChild(
        Entity parent,
        Mesh* mesh,
        MaterialId materialId,
        const LocalTransform& localTransform,
        bool castsShadow = true) {

        Entity entity = world_.create();
        world_.add<Transform>(entity);  // Will be computed by hierarchy system
        world_.add<LocalTransform>(entity, localTransform);
        world_.add<Parent>(entity, parent);
        world_.add<MeshRef>(entity, mesh);
        world_.add<MaterialRef>(entity, materialId);

        if (castsShadow) {
            world_.add<CastsShadow>(entity);
        }

        // Add to parent's Children list if it has one
        if (world_.has<Children>(parent)) {
            world_.get<Children>(parent).add(entity);
        }

        // Update hierarchy depths
        if (world_.has<HierarchyDepth>(parent)) {
            uint16_t parentDepth = world_.get<HierarchyDepth>(parent).depth;
            world_.add<HierarchyDepth>(entity, static_cast<uint16_t>(parentDepth + 1));
        } else {
            world_.add<HierarchyDepth>(entity, uint16_t(1));
        }

        return entity;
    }

    // Create a transform-only child (no mesh, just for grouping/pivot)
    [[nodiscard]] Entity createTransformChild(
        Entity parent,
        const LocalTransform& localTransform) {

        Entity entity = world_.create();
        world_.add<Transform>(entity);
        world_.add<LocalTransform>(entity, localTransform);
        world_.add<Parent>(entity, parent);
        world_.add<Children>(entity);  // Can have children of its own

        if (world_.has<Children>(parent)) {
            world_.get<Children>(parent).add(entity);
        }

        if (world_.has<HierarchyDepth>(parent)) {
            uint16_t parentDepth = world_.get<HierarchyDepth>(parent).depth;
            world_.add<HierarchyDepth>(entity, static_cast<uint16_t>(parentDepth + 1));
        } else {
            world_.add<HierarchyDepth>(entity, uint16_t(1));
        }

        return entity;
    }

    // -------------------------------------------------------------------------
    // Root entity with children support
    // -------------------------------------------------------------------------
    // Creates a root entity that can have children attached.

    [[nodiscard]] Entity createRoot(const glm::mat4& transform) {
        Entity entity = world_.create();
        world_.add<Transform>(entity, transform);
        world_.add<Children>(entity);
        world_.add<HierarchyDepth>(entity, uint16_t(0));
        return entity;
    }

    [[nodiscard]] Entity createRootWithMesh(
        Mesh* mesh,
        MaterialId materialId,
        const glm::mat4& transform,
        bool castsShadow = true) {

        Entity entity = createRoot(transform);
        world_.add<MeshRef>(entity, mesh);
        world_.add<MaterialRef>(entity, materialId);

        if (castsShadow) {
            world_.add<CastsShadow>(entity);
        }

        return entity;
    }

    // -------------------------------------------------------------------------
    // Utility helpers
    // -------------------------------------------------------------------------

private:
    // Check if renderable has non-default PBR properties
    static bool hasCustomPBR(const Renderable& r) {
        return r.roughness != 0.5f ||
               r.metallic != 0.0f ||
               r.emissiveIntensity != 0.0f ||
               r.emissiveColor != glm::vec3(1.0f) ||
               r.alphaTestThreshold != 0.0f ||
               r.pbrFlags != 0;
    }

    // Check if renderable represents a tree
    static bool isTree(const Renderable& r) {
        return r.treeInstanceIndex >= 0 || r.leafInstanceIndex >= 0;
    }

    World& world_;
};

// =============================================================================
// Sync utilities - for keeping ECS in sync during migration
// =============================================================================

// Update ECS Transform from Renderable transform (for objects that still
// use the old system but are mirrored in ECS)
inline void syncTransformFromRenderable(
    World& world,
    Entity entity,
    const Renderable& renderable) {

    if (world.has<Transform>(entity)) {
        world.get<Transform>(entity).matrix = renderable.transform;
    }
}

// Update Renderable transform from ECS (for physics-driven objects)
inline void syncRenderableFromTransform(
    Renderable& renderable,
    const World& world,
    Entity entity) {

    if (world.has<Transform>(entity)) {
        renderable.transform = world.get<Transform>(entity).matrix;
    }
}

} // namespace ecs
