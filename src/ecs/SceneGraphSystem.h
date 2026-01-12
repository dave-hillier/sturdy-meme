#pragma once

#include "Components.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

// Scene Graph System - manages entity hierarchy and world transforms
// Provides Unity-like parent/child relationships with transform propagation

namespace SceneGraph {

// ============================================================================
// Entity Creation with Hierarchy Support
// ============================================================================

// Create a new entity with scene graph components
inline entt::entity createEntity(entt::registry& registry,
                                  const std::string& name = "Entity",
                                  entt::entity parent = entt::null) {
    auto entity = registry.create();

    // Add scene graph components
    registry.emplace<EntityInfo>(entity, EntityInfo{name, "E", true, false, 0});
    registry.emplace<Hierarchy>(entity);
    registry.emplace<WorldTransform>(entity);

    // Set parent if specified
    if (parent != entt::null && registry.valid(parent)) {
        setParent(registry, entity, parent);
    }

    return entity;
}

// ============================================================================
// Hierarchy Management
// ============================================================================

// Set parent for an entity (handles both adding and removing from parent's children)
inline void setParent(entt::registry& registry, entt::entity child, entt::entity newParent) {
    if (!registry.valid(child)) return;

    // Ensure child has hierarchy component
    if (!registry.all_of<Hierarchy>(child)) {
        registry.emplace<Hierarchy>(child);
    }

    auto& childHierarchy = registry.get<Hierarchy>(child);
    entt::entity oldParent = childHierarchy.parent;

    // Remove from old parent's children list
    if (oldParent != entt::null && registry.valid(oldParent)) {
        if (registry.all_of<Hierarchy>(oldParent)) {
            auto& oldParentHierarchy = registry.get<Hierarchy>(oldParent);
            auto& children = oldParentHierarchy.children;
            children.erase(std::remove(children.begin(), children.end(), child), children.end());
        }
    }

    // Set new parent
    childHierarchy.parent = newParent;

    // Add to new parent's children list
    if (newParent != entt::null && registry.valid(newParent)) {
        if (!registry.all_of<Hierarchy>(newParent)) {
            registry.emplace<Hierarchy>(newParent);
        }
        auto& newParentHierarchy = registry.get<Hierarchy>(newParent);
        newParentHierarchy.children.push_back(child);
    }

    // Mark world transform as dirty
    markTransformDirty(registry, child);
}

// Remove parent (make entity a root)
inline void removeParent(entt::registry& registry, entt::entity child) {
    setParent(registry, child, entt::null);
}

// Add a child to an entity
inline void addChild(entt::registry& registry, entt::entity parent, entt::entity child) {
    setParent(registry, child, parent);
}

// Remove a child from an entity
inline void removeChild(entt::registry& registry, entt::entity parent, entt::entity child) {
    if (!registry.valid(parent) || !registry.valid(child)) return;
    if (!registry.all_of<Hierarchy>(child)) return;

    auto& childHierarchy = registry.get<Hierarchy>(child);
    if (childHierarchy.parent == parent) {
        setParent(registry, child, entt::null);
    }
}

// Check if entity is ancestor of another
inline bool isAncestorOf(entt::registry& registry, entt::entity potentialAncestor, entt::entity entity) {
    if (!registry.valid(entity) || !registry.all_of<Hierarchy>(entity)) return false;

    entt::entity current = registry.get<Hierarchy>(entity).parent;
    while (current != entt::null && registry.valid(current)) {
        if (current == potentialAncestor) return true;
        if (!registry.all_of<Hierarchy>(current)) break;
        current = registry.get<Hierarchy>(current).parent;
    }
    return false;
}

// Get all root entities (no parent)
inline std::vector<entt::entity> getRootEntities(entt::registry& registry) {
    std::vector<entt::entity> roots;
    auto view = registry.view<Hierarchy>();
    for (auto entity : view) {
        const auto& hierarchy = view.get<Hierarchy>(entity);
        if (hierarchy.parent == entt::null) {
            roots.push_back(entity);
        }
    }
    return roots;
}

// Get depth in hierarchy (0 = root)
inline int getDepth(entt::registry& registry, entt::entity entity) {
    int depth = 0;
    if (!registry.valid(entity) || !registry.all_of<Hierarchy>(entity)) return depth;

    entt::entity current = registry.get<Hierarchy>(entity).parent;
    while (current != entt::null && registry.valid(current)) {
        depth++;
        if (!registry.all_of<Hierarchy>(current)) break;
        current = registry.get<Hierarchy>(current).parent;
    }
    return depth;
}

// ============================================================================
// Transform System
// ============================================================================

// Mark entity and all descendants as needing transform update
inline void markTransformDirty(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity)) return;

    if (registry.all_of<WorldTransform>(entity)) {
        registry.get<WorldTransform>(entity).dirty = true;
    }

    if (registry.all_of<Hierarchy>(entity)) {
        const auto& hierarchy = registry.get<Hierarchy>(entity);
        for (auto child : hierarchy.children) {
            markTransformDirty(registry, child);
        }
    }
}

// Set local position and mark dirty
inline void setLocalPosition(entt::registry& registry, entt::entity entity, const glm::vec3& pos) {
    if (!registry.valid(entity) || !registry.all_of<Hierarchy>(entity)) return;
    registry.get<Hierarchy>(entity).localPosition = pos;
    markTransformDirty(registry, entity);
}

// Set local scale and mark dirty
inline void setLocalScale(entt::registry& registry, entt::entity entity, const glm::vec3& scale) {
    if (!registry.valid(entity) || !registry.all_of<Hierarchy>(entity)) return;
    registry.get<Hierarchy>(entity).localScale = scale;
    markTransformDirty(registry, entity);
}

// Set local rotation and mark dirty
inline void setLocalYaw(entt::registry& registry, entt::entity entity, float yaw) {
    if (!registry.valid(entity) || !registry.all_of<Hierarchy>(entity)) return;
    registry.get<Hierarchy>(entity).localYaw = yaw;
    markTransformDirty(registry, entity);
}

// Compute local transform matrix
inline glm::mat4 computeLocalMatrix(const Hierarchy& hierarchy) {
    glm::mat4 matrix = glm::mat4(1.0f);
    matrix = glm::translate(matrix, hierarchy.localPosition);
    matrix = glm::rotate(matrix, glm::radians(hierarchy.localYaw), glm::vec3(0.0f, 1.0f, 0.0f));
    matrix = glm::scale(matrix, hierarchy.localScale);
    return matrix;
}

// Update world transform for a single entity (recursive, updates parent first if needed)
inline void updateWorldTransform(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity)) return;
    if (!registry.all_of<WorldTransform>(entity)) {
        registry.emplace<WorldTransform>(entity);
    }

    auto& worldTransform = registry.get<WorldTransform>(entity);
    if (!worldTransform.dirty) return;

    glm::mat4 parentMatrix = glm::mat4(1.0f);

    // Get parent's world transform
    if (registry.all_of<Hierarchy>(entity)) {
        const auto& hierarchy = registry.get<Hierarchy>(entity);
        if (hierarchy.parent != entt::null && registry.valid(hierarchy.parent)) {
            updateWorldTransform(registry, hierarchy.parent);  // Ensure parent is updated
            if (registry.all_of<WorldTransform>(hierarchy.parent)) {
                parentMatrix = registry.get<WorldTransform>(hierarchy.parent).matrix;
            }
        }

        // Compute world matrix
        glm::mat4 localMatrix = computeLocalMatrix(hierarchy);
        worldTransform.matrix = parentMatrix * localMatrix;

        // Extract world position and scale from matrix
        worldTransform.position = glm::vec3(worldTransform.matrix[3]);
        worldTransform.scale = hierarchy.localScale;  // Simplified - doesn't account for parent scale properly
        worldTransform.yaw = hierarchy.localYaw;  // Simplified
    } else {
        // No hierarchy - use identity
        worldTransform.matrix = glm::mat4(1.0f);
        worldTransform.position = glm::vec3(0.0f);
        worldTransform.scale = glm::vec3(1.0f);
        worldTransform.yaw = 0.0f;
    }

    worldTransform.dirty = false;
}

// Update all world transforms (call once per frame)
inline void updateAllWorldTransforms(entt::registry& registry) {
    auto view = registry.view<WorldTransform>();
    for (auto entity : view) {
        updateWorldTransform(registry, entity);
    }
}

// ============================================================================
// Selection Management
// ============================================================================

// Select an entity
inline void selectEntity(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity)) return;
    if (!registry.all_of<Selected>(entity)) {
        registry.emplace<Selected>(entity);
    }
}

// Deselect an entity
inline void deselectEntity(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity)) return;
    if (registry.all_of<Selected>(entity)) {
        registry.remove<Selected>(entity);
    }
}

// Clear all selections
inline void clearSelection(entt::registry& registry) {
    auto view = registry.view<Selected>();
    for (auto entity : view) {
        registry.remove<Selected>(entity);
    }
}

// Get selected entities
inline std::vector<entt::entity> getSelectedEntities(entt::registry& registry) {
    std::vector<entt::entity> selected;
    auto view = registry.view<Selected>();
    for (auto entity : view) {
        selected.push_back(entity);
    }
    return selected;
}

// Check if entity is selected
inline bool isSelected(entt::registry& registry, entt::entity entity) {
    return registry.valid(entity) && registry.all_of<Selected>(entity);
}

// ============================================================================
// Entity Deletion
// ============================================================================

// Delete entity and all its children recursively
inline void deleteEntity(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity)) return;

    // First, recursively delete all children
    if (registry.all_of<Hierarchy>(entity)) {
        auto& hierarchy = registry.get<Hierarchy>(entity);
        // Copy children vector since we're modifying it
        auto children = hierarchy.children;
        for (auto child : children) {
            deleteEntity(registry, child);
        }

        // Remove from parent's children list
        if (hierarchy.parent != entt::null && registry.valid(hierarchy.parent)) {
            if (registry.all_of<Hierarchy>(hierarchy.parent)) {
                auto& parentHierarchy = registry.get<Hierarchy>(hierarchy.parent);
                auto& parentChildren = parentHierarchy.children;
                parentChildren.erase(
                    std::remove(parentChildren.begin(), parentChildren.end(), entity),
                    parentChildren.end());
            }
        }
    }

    // Finally destroy the entity
    registry.destroy(entity);
}

// ============================================================================
// Utility Functions
// ============================================================================

// Get entity display name
inline std::string getEntityName(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity)) return "<Invalid>";

    if (registry.all_of<EntityInfo>(entity)) {
        return registry.get<EntityInfo>(entity).name;
    }
    if (registry.all_of<NameTag>(entity)) {
        return registry.get<NameTag>(entity).name;
    }

    // Generate name from entity ID
    return "Entity_" + std::to_string(static_cast<uint32_t>(entity));
}

// Get icon for entity based on components
inline std::string getEntityIcon(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity)) return "?";

    // Check for specific component types and return appropriate icon
    if (registry.all_of<PlayerTag>(entity)) return "P";
    if (registry.all_of<PointLight>(entity)) return "L";
    if (registry.all_of<SpotLight>(entity)) return "S";
    if (registry.all_of<NPCTag>(entity)) return "N";
    if (registry.all_of<PhysicsBody>(entity)) return "R";  // Rigidbody
    if (registry.all_of<RenderableRef>(entity) || registry.all_of<SceneRenderable>(entity)) return "M";  // Mesh

    if (registry.all_of<EntityInfo>(entity)) {
        return registry.get<EntityInfo>(entity).icon;
    }

    return "E";  // Default entity
}

// Count total entities in hierarchy
inline size_t countEntitiesInHierarchy(entt::registry& registry) {
    return registry.view<Hierarchy>().size();
}

}  // namespace SceneGraph
