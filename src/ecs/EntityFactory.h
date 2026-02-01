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

        // PBR properties - only add if non-default (sparse component pattern)
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

// =============================================================================
// Entity Builder - Fluent API for creating ECS entities
// =============================================================================
// Mirrors RenderableBuilder API for easy migration from Renderable-based system
// to direct ECS entity creation.

class EntityBuilder {
public:
    explicit EntityBuilder(World& world) : world_(world) {}

    // Required: Set the mesh for this entity
    EntityBuilder& withMesh(Mesh* mesh) {
        mesh_ = mesh;
        return *this;
    }

    // Required: Set material ID (for MaterialRegistry-based rendering)
    EntityBuilder& withMaterialId(MaterialId id) {
        materialId_ = id;
        return *this;
    }

    // Required: Set the world transform
    EntityBuilder& withTransform(const glm::mat4& transform) {
        transform_ = transform;
        return *this;
    }

    // Convenience: Set position only (creates translation matrix)
    EntityBuilder& atPosition(const glm::vec3& position) {
        transform_ = glm::translate(glm::mat4(1.0f), position);
        return *this;
    }

    // Optional: Set PBR roughness (default: 0.5)
    EntityBuilder& withRoughness(float roughness) {
        roughness_ = roughness;
        hasCustomPBR_ = true;
        return *this;
    }

    // Optional: Set PBR metallic (default: 0.0)
    EntityBuilder& withMetallic(float metallic) {
        metallic_ = metallic;
        hasCustomPBR_ = true;
        return *this;
    }

    // Optional: Set emissive intensity (default: 0.0, no emission)
    EntityBuilder& withEmissiveIntensity(float intensity) {
        emissiveIntensity_ = intensity;
        hasCustomPBR_ = true;
        return *this;
    }

    // Optional: Set emissive color (default: white)
    EntityBuilder& withEmissiveColor(const glm::vec3& color) {
        emissiveColor_ = color;
        hasCustomPBR_ = true;
        return *this;
    }

    // Optional: Set whether entity casts shadows (default: true)
    EntityBuilder& withCastsShadow(bool casts) {
        castsShadow_ = casts;
        return *this;
    }

    // Optional: Set alpha test threshold (default: 0.0 = disabled)
    EntityBuilder& withAlphaTest(float threshold) {
        alphaTestThreshold_ = threshold;
        hasCustomPBR_ = true;
        return *this;
    }

    // Optional: Set hue shift in radians (for NPC tinting)
    EntityBuilder& withHueShift(float shift) {
        hueShift_ = shift;
        return *this;
    }

    // Optional: Set opacity (for fade effects)
    EntityBuilder& withOpacity(float opacity) {
        opacity_ = opacity;
        return *this;
    }

    // Optional: Set tree instance index
    EntityBuilder& withTreeInstanceIndex(int index) {
        treeInstanceIndex_ = index;
        return *this;
    }

    // Optional: Set leaf instance index
    EntityBuilder& withLeafInstanceIndex(int index) {
        leafInstanceIndex_ = index;
        return *this;
    }

    // Optional: Set leaf tint
    EntityBuilder& withLeafTint(const glm::vec3& tint) {
        leafTint_ = tint;
        return *this;
    }

    // Optional: Set autumn hue shift
    EntityBuilder& withAutumnHueShift(float shift) {
        autumnHueShift_ = shift;
        return *this;
    }

    // Optional: Mark as having bounding sphere
    EntityBuilder& withBoundingSphere(const glm::vec3& center, float radius) {
        boundCenter_ = center;
        boundRadius_ = radius;
        hasBounds_ = true;
        return *this;
    }

    // Build the entity - asserts if required fields are missing
    [[nodiscard]] Entity build() {
        // Assert that all required fields are set
        assert(mesh_ != nullptr && "EntityBuilder: mesh is required");
        assert(materialId_ != InvalidMaterialId && "EntityBuilder: materialId is required");
        assert(transform_.has_value() && "EntityBuilder: transform is required");

        Entity entity = world_.create();

        // Core components - always present
        world_.add<Transform>(entity, transform_.value());
        world_.add<MeshRef>(entity, mesh_);
        world_.add<MaterialRef>(entity, materialId_);

        // Shadow casting
        if (castsShadow_) {
            world_.add<CastsShadow>(entity);
        }

        // PBR properties - only add if non-default (sparse component pattern)
        if (hasCustomPBR_) {
            PBRProperties pbr;
            pbr.roughness = roughness_;
            pbr.metallic = metallic_;
            pbr.emissiveIntensity = emissiveIntensity_;
            pbr.emissiveColor = emissiveColor_;
            pbr.alphaTestThreshold = alphaTestThreshold_;
            world_.add<PBRProperties>(entity, pbr);
        }

        // Hue shift for NPCs
        if (hueShift_ != 0.0f) {
            world_.add<HueShift>(entity, hueShift_);
        }

        // Opacity for fade effects
        if (opacity_ != 1.0f) {
            world_.add<Opacity>(entity, opacity_);
        }

        // Tree-specific data
        if (treeInstanceIndex_ >= 0 || leafInstanceIndex_ >= 0) {
            TreeData treeData;
            treeData.leafInstanceIndex = leafInstanceIndex_;
            treeData.treeInstanceIndex = treeInstanceIndex_;
            treeData.leafTint = leafTint_;
            treeData.autumnHueShift = autumnHueShift_;
            world_.add<TreeData>(entity, treeData);
        }

        // Bounding sphere for culling
        if (hasBounds_) {
            world_.add<BoundingSphere>(entity, boundCenter_, boundRadius_);
        }

        // Mark as visible by default
        world_.add<Visible>(entity);

        return entity;
    }

    // Check if all required fields are set
    [[nodiscard]] bool isValid() const {
        return mesh_ != nullptr && materialId_ != InvalidMaterialId && transform_.has_value();
    }

private:
    World& world_;
    std::optional<glm::mat4> transform_;
    Mesh* mesh_ = nullptr;
    MaterialId materialId_ = InvalidMaterialId;

    // PBR properties
    float roughness_ = 0.5f;
    float metallic_ = 0.0f;
    float emissiveIntensity_ = 0.0f;
    glm::vec3 emissiveColor_ = glm::vec3(1.0f);
    float alphaTestThreshold_ = 0.0f;
    bool hasCustomPBR_ = false;

    bool castsShadow_ = true;
    float hueShift_ = 0.0f;
    float opacity_ = 1.0f;

    // Tree properties
    int treeInstanceIndex_ = -1;
    int leafInstanceIndex_ = -1;
    glm::vec3 leafTint_ = glm::vec3(1.0f);
    float autumnHueShift_ = 0.0f;

    // Bounds
    glm::vec3 boundCenter_ = glm::vec3(0.0f);
    float boundRadius_ = 1.0f;
    bool hasBounds_ = false;
};

} // namespace ecs
