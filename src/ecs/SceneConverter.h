#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include "Components.h"
#include "ResourceRegistry.h"
#include "SceneGraphSystem.h"

// Forward declarations
struct Renderable;
class SceneBuilder;
class Mesh;

// Scene Converter - Converts SceneBuilder renderables to ECS entities
// This enables gradual migration from the vector-based system to full ECS
namespace SceneConvert {

// ============================================================================
// Conversion Options
// ============================================================================

struct ConvertOptions {
    bool createHierarchy = true;       // Add Hierarchy component for scene graph
    bool createBounds = true;          // Add AABBBounds from mesh bounds
    bool preserveSceneIndex = true;    // Add SceneRenderable with original index
    bool createEntityInfo = true;      // Add EntityInfo for scene graph display
    std::string namePrefix = "Object"; // Prefix for generated entity names
};

// ============================================================================
// Single Renderable Conversion
// ============================================================================

// Convert a single Renderable to an ECS entity
// Returns the created entity
inline entt::entity convertRenderable(
    entt::registry& registry,
    const Renderable& renderable,
    size_t sceneIndex,
    ResourceRegistry& resources,
    const ConvertOptions& options = ConvertOptions{})
{
    auto entity = registry.create();

    // Extract position from transform matrix
    glm::vec3 position = glm::vec3(renderable.transform[3]);

    // Extract yaw from transform (simplified - assumes Y-up rotation only)
    // For full rotation support, would need to decompose the matrix
    float yaw = 0.0f;
    if (glm::length(glm::vec2(renderable.transform[0][0], renderable.transform[0][2])) > 0.001f) {
        yaw = glm::degrees(atan2(renderable.transform[0][2], renderable.transform[0][0]));
    }

    // Add Transform component
    registry.emplace<Transform>(entity, Transform{position, yaw});

    // Add ModelMatrix with the full transform
    registry.emplace<ModelMatrix>(entity, ModelMatrix{renderable.transform});

    // Add MeshRenderer component
    MeshRenderer meshComp;

    // Try to find existing mesh handle or register new one
    if (renderable.mesh) {
        auto existing = resources.findMesh(std::to_string(reinterpret_cast<uintptr_t>(renderable.mesh)));
        if (existing) {
            meshComp.mesh = *existing;
        } else {
            meshComp.mesh = resources.registerMesh(
                const_cast<Mesh*>(renderable.mesh),
                std::to_string(reinterpret_cast<uintptr_t>(renderable.mesh)));
        }
    }

    // Register material
    meshComp.material = resources.registerMaterial(
        renderable.materialId.value,
        "material_" + std::to_string(sceneIndex));

    meshComp.castsShadow = renderable.castsShadow;
    meshComp.receiveShadow = true;
    meshComp.layer = RenderLayer::Default;

    registry.emplace<MeshRenderer>(entity, meshComp);

    // Add hierarchy components for scene graph
    if (options.createHierarchy) {
        Hierarchy hierarchy;
        hierarchy.localPosition = position;
        hierarchy.localScale = glm::vec3(1.0f);  // Would need matrix decomposition for scale
        hierarchy.localYaw = yaw;
        registry.emplace<Hierarchy>(entity, hierarchy);
        registry.emplace<WorldTransform>(entity);
    }

    // Add bounding box from mesh
    if (options.createBounds && renderable.mesh) {
        // Get bounds from mesh (would need access to Mesh::getBounds())
        AABBBounds bounds;
        bounds.min = glm::vec3(-0.5f);
        bounds.max = glm::vec3(0.5f);
        registry.emplace<AABBBounds>(entity, bounds);
    }

    // Preserve original scene index for backward compatibility
    if (options.preserveSceneIndex) {
        registry.emplace<SceneRenderable>(entity, SceneRenderable{sceneIndex});
    }

    // Add EntityInfo for scene graph display
    if (options.createEntityInfo) {
        EntityInfo info;
        info.name = options.namePrefix + "_" + std::to_string(sceneIndex);
        info.icon = "M";  // Mesh icon
        info.visible = true;
        info.locked = false;
        registry.emplace<EntityInfo>(entity, info);
    }

    // Handle emissive objects
    if (renderable.emissiveIntensity > 0.0f) {
        EmissiveLight emissive;
        emissive.color = renderable.emissiveColor;
        emissive.intensity = renderable.emissiveIntensity;
        registry.emplace<EmissiveLight>(entity, emissive);
    }

    return entity;
}

// ============================================================================
// Batch Conversion
// ============================================================================

// Convert all renderables from a vector to ECS entities
// Returns vector of created entities (same order as input)
inline std::vector<entt::entity> convertAllRenderables(
    entt::registry& registry,
    const std::vector<Renderable>& renderables,
    ResourceRegistry& resources,
    const ConvertOptions& options = ConvertOptions{})
{
    std::vector<entt::entity> entities;
    entities.reserve(renderables.size());

    for (size_t i = 0; i < renderables.size(); i++) {
        auto entity = convertRenderable(registry, renderables[i], i, resources, options);
        entities.push_back(entity);
    }

    return entities;
}

// ============================================================================
// Sync Functions
// ============================================================================

// Sync ECS transforms back to Renderable transforms
// Use this to keep the vector-based rendering in sync with ECS
inline void syncECSToRenderables(
    entt::registry& registry,
    std::vector<Renderable>& renderables)
{
    auto view = registry.view<SceneRenderable, ModelMatrix>();
    for (auto entity : view) {
        auto& sceneRef = view.get<SceneRenderable>(entity);
        auto& modelMatrix = view.get<ModelMatrix>(entity);

        if (sceneRef.renderableIndex < renderables.size()) {
            renderables[sceneRef.renderableIndex].transform = modelMatrix.matrix;
        }
    }
}

// Sync Renderable transforms to ECS
// Use this to update ECS from physics or animation
inline void syncRenderablesToECS(
    entt::registry& registry,
    const std::vector<Renderable>& renderables)
{
    auto view = registry.view<SceneRenderable, ModelMatrix>();
    for (auto entity : view) {
        auto& sceneRef = view.get<SceneRenderable>(entity);
        auto& modelMatrix = view.get<ModelMatrix>(entity);

        if (sceneRef.renderableIndex < renderables.size()) {
            modelMatrix.matrix = renderables[sceneRef.renderableIndex].transform;

            // Also update Transform component if present
            if (registry.all_of<Transform>(entity)) {
                auto& transform = registry.get<Transform>(entity);
                transform.position = glm::vec3(modelMatrix.matrix[3]);
            }
        }
    }
}

// ============================================================================
// Query Helpers
// ============================================================================

// Find ECS entity by scene index
inline entt::entity findBySceneIndex(entt::registry& registry, size_t sceneIndex) {
    auto view = registry.view<SceneRenderable>();
    for (auto entity : view) {
        if (view.get<SceneRenderable>(entity).renderableIndex == sceneIndex) {
            return entity;
        }
    }
    return entt::null;
}

// Get all entities that reference scene renderables
inline std::vector<entt::entity> getSceneEntities(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<SceneRenderable>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// ============================================================================
// Render List Generation
// ============================================================================

// Render item for batch rendering
struct RenderItem {
    entt::entity entity;
    MeshHandle mesh;
    MaterialHandle material;
    glm::mat4 transform;
    bool castsShadow;
    float distanceToCamera;  // For sorting
};

// Build sorted render list from ECS entities
// Sorts by material to minimize descriptor set switches
inline std::vector<RenderItem> buildRenderList(
    entt::registry& registry,
    const glm::vec3& cameraPos)
{
    std::vector<RenderItem> items;

    auto view = registry.view<MeshRenderer>();
    for (auto entity : view) {
        auto& mesh = view.get<MeshRenderer>(entity);
        if (mesh.mesh == InvalidMesh) continue;

        RenderItem item;
        item.entity = entity;
        item.mesh = mesh.mesh;
        item.material = mesh.material;
        item.castsShadow = mesh.castsShadow;

        // Get transform matrix
        if (registry.all_of<ModelMatrix>(entity)) {
            item.transform = registry.get<ModelMatrix>(entity).matrix;
        } else if (registry.all_of<WorldTransform>(entity)) {
            item.transform = registry.get<WorldTransform>(entity).matrix;
        } else if (registry.all_of<Transform>(entity)) {
            auto& t = registry.get<Transform>(entity);
            item.transform = glm::translate(glm::mat4(1.0f), t.position);
            item.transform = glm::rotate(item.transform, glm::radians(t.yaw), glm::vec3(0, 1, 0));
        } else {
            item.transform = glm::mat4(1.0f);
        }

        // Calculate distance for sorting
        glm::vec3 pos = glm::vec3(item.transform[3]);
        item.distanceToCamera = glm::length(pos - cameraPos);

        items.push_back(item);
    }

    // Sort by material first (minimize descriptor switches), then by distance
    std::sort(items.begin(), items.end(), [](const RenderItem& a, const RenderItem& b) {
        if (a.material != b.material) return a.material < b.material;
        return a.distanceToCamera < b.distanceToCamera;
    });

    return items;
}

// Build shadow caster list (subset of render list)
inline std::vector<RenderItem> buildShadowCasterList(
    entt::registry& registry,
    const glm::vec3& lightPos)
{
    std::vector<RenderItem> items;

    auto view = registry.view<MeshRenderer>();
    for (auto entity : view) {
        auto& mesh = view.get<MeshRenderer>(entity);
        if (mesh.mesh == InvalidMesh || !mesh.castsShadow) continue;

        RenderItem item;
        item.entity = entity;
        item.mesh = mesh.mesh;
        item.material = mesh.material;
        item.castsShadow = true;

        // Get transform matrix
        if (registry.all_of<ModelMatrix>(entity)) {
            item.transform = registry.get<ModelMatrix>(entity).matrix;
        } else if (registry.all_of<WorldTransform>(entity)) {
            item.transform = registry.get<WorldTransform>(entity).matrix;
        } else {
            item.transform = glm::mat4(1.0f);
        }

        glm::vec3 pos = glm::vec3(item.transform[3]);
        item.distanceToCamera = glm::length(pos - lightPos);

        items.push_back(item);
    }

    return items;
}

}  // namespace SceneConvert
