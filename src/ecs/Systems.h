#pragma once

#include "World.h"
#include "Components.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>

namespace ecs {

// =============================================================================
// Frustum for culling operations
// =============================================================================

struct Frustum {
    // Plane: ax + by + cz + d = 0, stored as vec4(a, b, c, d)
    // Planes: left, right, bottom, top, near, far
    std::array<glm::vec4, 6> planes;

    // Extract frustum planes from view-projection matrix
    static Frustum fromViewProjection(const glm::mat4& vp) {
        Frustum f;
        // Left plane
        f.planes[0] = glm::vec4(
            vp[0][3] + vp[0][0],
            vp[1][3] + vp[1][0],
            vp[2][3] + vp[2][0],
            vp[3][3] + vp[3][0]
        );
        // Right plane
        f.planes[1] = glm::vec4(
            vp[0][3] - vp[0][0],
            vp[1][3] - vp[1][0],
            vp[2][3] - vp[2][0],
            vp[3][3] - vp[3][0]
        );
        // Bottom plane
        f.planes[2] = glm::vec4(
            vp[0][3] + vp[0][1],
            vp[1][3] + vp[1][1],
            vp[2][3] + vp[2][1],
            vp[3][3] + vp[3][1]
        );
        // Top plane
        f.planes[3] = glm::vec4(
            vp[0][3] - vp[0][1],
            vp[1][3] - vp[1][1],
            vp[2][3] - vp[2][1],
            vp[3][3] - vp[3][1]
        );
        // Near plane
        f.planes[4] = glm::vec4(
            vp[0][3] + vp[0][2],
            vp[1][3] + vp[1][2],
            vp[2][3] + vp[2][2],
            vp[3][3] + vp[3][2]
        );
        // Far plane
        f.planes[5] = glm::vec4(
            vp[0][3] - vp[0][2],
            vp[1][3] - vp[1][2],
            vp[2][3] - vp[2][2],
            vp[3][3] - vp[3][2]
        );

        // Normalize planes
        for (auto& plane : f.planes) {
            float length = glm::length(glm::vec3(plane));
            if (length > 0.0f) {
                plane /= length;
            }
        }
        return f;
    }

    // Test if sphere is inside or intersecting frustum
    [[nodiscard]] bool containsSphere(const glm::vec3& center, float radius) const {
        for (const auto& plane : planes) {
            float distance = glm::dot(glm::vec3(plane), center) + plane.w;
            if (distance < -radius) {
                return false;  // Completely outside
            }
        }
        return true;  // Inside or intersecting
    }

    // Test if AABB is inside or intersecting frustum
    [[nodiscard]] bool containsAABB(const glm::vec3& min, const glm::vec3& max) const {
        for (const auto& plane : planes) {
            glm::vec3 p = min;
            if (plane.x >= 0) p.x = max.x;
            if (plane.y >= 0) p.y = max.y;
            if (plane.z >= 0) p.z = max.z;

            if (glm::dot(glm::vec3(plane), p) + plane.w < 0) {
                return false;  // Completely outside
            }
        }
        return true;  // Inside or intersecting
    }
};

// =============================================================================
// Visibility Culling System
// =============================================================================
// Updates the Visible tag component based on frustum culling.
// Entities must have Transform and BoundingSphere/BoundingBox.

namespace systems {

// =============================================================================
// External Transform Source System
// =============================================================================
// Updates transforms for entities driven by external sources (physics, bones, etc.)
// Must be called BEFORE updateWorldTransforms() so hierarchy can build on these.

// Update transforms from external sources (pointers to matrices managed elsewhere)
inline void updateExternalTransforms(World& world) {
    for (auto [entity, source, transform] :
         world.view<ExternalTransformSource, Transform>().each()) {
        if (source.valid()) {
            transform.matrix = *source.sourceMatrix;
        }
    }
}

// Update transforms for bone-attached entities
// Requires the global bone transforms array computed from the skeleton
// entityWorldTransform is the character's world transform
// globalBoneTransforms is the array of bone transforms in model space
inline void updateBoneAttachments(World& world,
                                   const glm::mat4& entityWorldTransform,
                                   const std::vector<glm::mat4>& globalBoneTransforms) {
    for (auto [entity, attachment, transform] :
         world.view<BoneAttachment, Transform>().each()) {
        if (!attachment.valid()) continue;

        size_t boneIdx = static_cast<size_t>(attachment.boneIndex);
        if (boneIdx >= globalBoneTransforms.size()) continue;

        // World transform = entity world * bone transform * local offset
        glm::mat4 boneWorld = entityWorldTransform * globalBoneTransforms[boneIdx];
        transform.matrix = boneWorld * attachment.localOffset;
    }
}

// Variant that also applies LocalTransform offset if present
inline void updateBoneAttachmentsWithLocalOffset(World& world,
                                                   const glm::mat4& entityWorldTransform,
                                                   const std::vector<glm::mat4>& globalBoneTransforms) {
    for (auto [entity, attachment, transform] :
         world.view<BoneAttachment, Transform>().each()) {
        if (!attachment.valid()) continue;

        size_t boneIdx = static_cast<size_t>(attachment.boneIndex);
        if (boneIdx >= globalBoneTransforms.size()) continue;

        // World transform = entity world * bone transform * attachment offset
        glm::mat4 boneWorld = entityWorldTransform * globalBoneTransforms[boneIdx];
        glm::mat4 baseTransform = boneWorld * attachment.localOffset;

        // Apply additional LocalTransform if present
        if (world.has<LocalTransform>(entity)) {
            const auto& local = world.get<LocalTransform>(entity);
            transform.matrix = baseTransform * local.toMatrix();
        } else {
            transform.matrix = baseTransform;
        }
    }
}

// =============================================================================
// Hierarchical Transform System
// =============================================================================
// Updates world Transform components from LocalTransform and Parent hierarchy.
// Must be called before visibility culling or any system that reads Transform.

// Update world transforms for all entities with LocalTransform + Parent
// Processes in depth order (roots first, then children)
inline void updateWorldTransforms(World& world) {
    // First pass: Update root entities (LocalTransform but no Parent)
    // These entities use LocalTransform directly as their world transform
    for (auto [entity, local, transform] :
         world.view<LocalTransform, Transform>(entt::exclude<Parent>).each()) {
        transform.matrix = local.toMatrix();
    }

    // Second pass: Update entities with parents
    // We need to process in depth order, so collect and sort by depth
    struct EntityDepth {
        Entity entity;
        uint16_t depth;
    };
    std::vector<EntityDepth> hierarchyEntities;

    for (auto [entity, local, parent] :
         world.view<LocalTransform, Parent>().each()) {
        uint16_t depth = 1;
        if (world.has<HierarchyDepth>(entity)) {
            depth = world.get<HierarchyDepth>(entity).depth;
        }
        hierarchyEntities.push_back({entity, depth});
    }

    // Sort by depth (parents processed before children)
    std::sort(hierarchyEntities.begin(), hierarchyEntities.end(),
              [](const EntityDepth& a, const EntityDepth& b) {
                  return a.depth < b.depth;
              });

    // Process in depth order
    for (const auto& ed : hierarchyEntities) {
        Entity entity = ed.entity;

        if (!world.has<LocalTransform>(entity) || !world.has<Parent>(entity)) {
            continue;
        }

        const auto& local = world.get<LocalTransform>(entity);
        const auto& parent = world.get<Parent>(entity);

        // Get parent's world transform
        glm::mat4 parentWorld = glm::mat4(1.0f);
        if (parent.valid() && world.valid(parent.entity) &&
            world.has<Transform>(parent.entity)) {
            parentWorld = world.get<Transform>(parent.entity).matrix;
        }

        // Compute world transform: parent * local
        if (world.has<Transform>(entity)) {
            world.get<Transform>(entity).matrix = parentWorld * local.toMatrix();
        }
    }
}

// Compute and cache hierarchy depths for efficient sorting
// Call this after hierarchy changes (attach/detach operations)
inline void updateHierarchyDepths(World& world) {
    // First, set depth 0 for all root entities (no parent)
    for (auto [entity, local] :
         world.view<LocalTransform>(entt::exclude<Parent>).each()) {
        if (world.has<HierarchyDepth>(entity)) {
            world.get<HierarchyDepth>(entity).depth = 0;
        } else {
            world.add<HierarchyDepth>(entity, uint16_t(0));
        }
    }

    // Iteratively compute depths for children
    // Keep iterating until no changes (handles arbitrary depth hierarchies)
    bool changed = true;
    uint16_t maxIterations = 100;  // Safety limit
    uint16_t iteration = 0;

    while (changed && iteration < maxIterations) {
        changed = false;
        iteration++;

        for (auto [entity, parent] : world.view<Parent>().each()) {
            if (!parent.valid() || !world.valid(parent.entity)) {
                continue;
            }

            // Get parent's depth
            uint16_t parentDepth = 0;
            if (world.has<HierarchyDepth>(parent.entity)) {
                parentDepth = world.get<HierarchyDepth>(parent.entity).depth;
            }

            uint16_t expectedDepth = parentDepth + 1;

            if (world.has<HierarchyDepth>(entity)) {
                if (world.get<HierarchyDepth>(entity).depth != expectedDepth) {
                    world.get<HierarchyDepth>(entity).depth = expectedDepth;
                    changed = true;
                }
            } else {
                world.add<HierarchyDepth>(entity, expectedDepth);
                changed = true;
            }
        }
    }
}

// =============================================================================
// Hierarchy Management Helpers
// =============================================================================

// Attach a child entity to a parent entity
// Sets up Parent component and optionally adds to parent's Children list
inline void attachToParent(World& world, Entity child, Entity parent) {
    // Add or update Parent component on child
    if (world.has<Parent>(child)) {
        // Detach from old parent first
        Entity oldParent = world.get<Parent>(child).entity;
        if (oldParent != NullEntity && world.valid(oldParent) &&
            world.has<Children>(oldParent)) {
            world.get<Children>(oldParent).remove(child);
        }
        world.get<Parent>(child).entity = parent;
    } else {
        world.add<Parent>(child, parent);
    }

    // Ensure child has LocalTransform (if it only had world Transform)
    if (!world.has<LocalTransform>(child)) {
        // Initialize LocalTransform as identity - caller should set appropriately
        world.add<LocalTransform>(child);
    }

    // Ensure child has Transform component for world space result
    if (!world.has<Transform>(child)) {
        world.add<Transform>(child);
    }

    // Add to parent's Children list (if parent has one)
    if (world.has<Children>(parent)) {
        world.get<Children>(parent).add(child);
    }

    // Update hierarchy depths
    updateHierarchyDepths(world);
}

// Detach an entity from its parent (becomes a root)
inline void detachFromParent(World& world, Entity child) {
    if (!world.has<Parent>(child)) {
        return;  // Already a root
    }

    const auto& parentComp = world.get<Parent>(child);
    Entity parent = parentComp.entity;

    // Remove from parent's Children list
    if (parent != NullEntity && world.valid(parent) && world.has<Children>(parent)) {
        world.get<Children>(parent).remove(child);
    }

    // Remove Parent component
    world.remove<Parent>(child);

    // Convert current world transform to local transform (now relative to world origin)
    if (world.has<Transform>(child) && world.has<LocalTransform>(child)) {
        // The current world transform becomes the local transform
        // (simplified - doesn't decompose TRS, just copies matrix)
        const auto& worldTransform = world.get<Transform>(child);
        auto& local = world.get<LocalTransform>(child);
        local.position = worldTransform.position();
        // Note: rotation and scale would need proper decomposition for accuracy
    }

    // Update hierarchy depths
    updateHierarchyDepths(world);
}

// =============================================================================
// Visibility Culling System
// =============================================================================

// CPU-based frustum culling using bounding spheres
// Adds/removes Visible tag component based on frustum test
inline void updateVisibility(World& world, const Frustum& frustum) {
    // Process entities with bounding spheres
    for (auto [entity, transform, bounds] :
         world.view<Transform, BoundingSphere>().each()) {

        // Transform bounds center to world space
        glm::vec3 worldCenter = glm::vec3(transform.matrix * glm::vec4(bounds.center, 1.0f));

        // Approximate scale from transform matrix for radius
        float scaleX = glm::length(glm::vec3(transform.matrix[0]));
        float scaleY = glm::length(glm::vec3(transform.matrix[1]));
        float scaleZ = glm::length(glm::vec3(transform.matrix[2]));
        float maxScale = glm::max(scaleX, glm::max(scaleY, scaleZ));
        float worldRadius = bounds.radius * maxScale;

        if (frustum.containsSphere(worldCenter, worldRadius)) {
            if (!world.has<Visible>(entity)) {
                world.add<Visible>(entity);
            }
        } else {
            if (world.has<Visible>(entity)) {
                world.remove<Visible>(entity);
            }
        }
    }

    // Process entities with bounding boxes (that don't have spheres)
    for (auto [entity, transform, bounds] :
         world.view<Transform, BoundingBox>(entt::exclude<BoundingSphere>).each()) {

        // Transform AABB to world space (approximate - uses transformed corners)
        glm::vec3 corners[8] = {
            glm::vec3(bounds.min.x, bounds.min.y, bounds.min.z),
            glm::vec3(bounds.max.x, bounds.min.y, bounds.min.z),
            glm::vec3(bounds.min.x, bounds.max.y, bounds.min.z),
            glm::vec3(bounds.max.x, bounds.max.y, bounds.min.z),
            glm::vec3(bounds.min.x, bounds.min.y, bounds.max.z),
            glm::vec3(bounds.max.x, bounds.min.y, bounds.max.z),
            glm::vec3(bounds.min.x, bounds.max.y, bounds.max.z),
            glm::vec3(bounds.max.x, bounds.max.y, bounds.max.z)
        };

        glm::vec3 worldMin(std::numeric_limits<float>::max());
        glm::vec3 worldMax(std::numeric_limits<float>::lowest());

        for (const auto& corner : corners) {
            glm::vec3 worldCorner = glm::vec3(transform.matrix * glm::vec4(corner, 1.0f));
            worldMin = glm::min(worldMin, worldCorner);
            worldMax = glm::max(worldMax, worldCorner);
        }

        if (frustum.containsAABB(worldMin, worldMax)) {
            if (!world.has<Visible>(entity)) {
                world.add<Visible>(entity);
            }
        } else {
            if (world.has<Visible>(entity)) {
                world.remove<Visible>(entity);
            }
        }
    }
}

// LOD system - updates LOD levels based on distance from camera
inline void updateLOD(World& world, const glm::vec3& cameraPos) {
    for (auto [entity, transform, lod] : world.view<Transform, LODController>().each()) {
        float dist = glm::distance(cameraPos, transform.position());

        // Determine LOD level
        uint8_t newLevel;
        if (dist < lod.thresholds[0]) {
            newLevel = 0;  // High detail
        } else if (dist < lod.thresholds[1]) {
            newLevel = 1;  // Medium detail
        } else {
            newLevel = 2;  // Low detail
        }

        lod.currentLevel = newLevel;

        // Update interval based on LOD (distant objects update less frequently)
        switch (newLevel) {
            case 0: lod.updateInterval = 1; break;    // Every frame
            case 1: lod.updateInterval = 4; break;    // Every 4 frames
            case 2: lod.updateInterval = 16; break;   // Every 16 frames
            default: lod.updateInterval = 1; break;
        }
    }
}

// Physics sync system - updates transforms from physics bodies
// Note: Requires PhysicsSystem pointer to be passed in
template<typename PhysicsSystem>
void syncPhysicsTransforms(World& world, PhysicsSystem& physics) {
    for (auto [entity, body, transform] : world.view<PhysicsBody, Transform>().each()) {
        if (body.valid()) {
            transform.matrix = physics.getBodyTransform(body.bodyId);
        }
    }
}

// =============================================================================
// NPC Animation Systems
// =============================================================================
// Systems for NPC animation state management and LOD-based updates.

// Update NPC LOD levels based on camera distance
inline void updateNPCLODLevels(World& world, const glm::vec3& cameraPos) {
    for (auto [entity, transform, lodCtrl] :
         world.view<Transform, NPCLODController>().each()) {
        glm::vec3 npcPos = transform.position();
        float distance = glm::distance(cameraPos, npcPos);

        NPCLODLevel newLevel;
        if (distance < NPCLODController::DISTANCE_REAL) {
            newLevel = NPCLODLevel::Real;
        } else if (distance < NPCLODController::DISTANCE_BULK) {
            newLevel = NPCLODLevel::Bulk;
        } else {
            newLevel = NPCLODLevel::Virtual;
        }

        // Reset frame counter on LOD change
        if (lodCtrl.level != newLevel) {
            lodCtrl.framesSinceUpdate = 0;
        }

        lodCtrl.level = newLevel;
    }
}

// Increment NPC frame counters (call once per frame)
inline void tickNPCFrameCounters(World& world) {
    for (auto [entity, lodCtrl] : world.view<NPCLODController>().each()) {
        lodCtrl.framesSinceUpdate++;
    }
}

// Get NPC entities that should update animation this frame
// Returns entities where lodCtrl.shouldUpdate() is true
inline std::vector<Entity> getNPCsToUpdate(World& world) {
    std::vector<Entity> toUpdate;
    for (auto [entity, lodCtrl] : world.view<NPCLODController>().each()) {
        if (lodCtrl.shouldUpdate()) {
            toUpdate.push_back(entity);
        }
    }
    return toUpdate;
}

// Reset frame counter for NPCs that were updated
inline void resetUpdatedNPCCounters(World& world, const std::vector<Entity>& updatedEntities) {
    for (Entity entity : updatedEntities) {
        if (world.valid(entity) && world.has<NPCLODController>(entity)) {
            world.get<NPCLODController>(entity).framesSinceUpdate = 0;
        }
    }
}

// Get NPC statistics for debugging
struct NPCLODStats {
    size_t realCount = 0;    // NPCs at highest quality
    size_t bulkCount = 0;    // NPCs at medium quality
    size_t virtualCount = 0; // NPCs at lowest quality
    size_t totalCount = 0;
};

inline NPCLODStats getNPCLODStats(const World& world) {
    NPCLODStats stats;
    for (auto [entity, lodCtrl] : world.view<NPCLODController>().each()) {
        stats.totalCount++;
        switch (lodCtrl.level) {
            case NPCLODLevel::Real: stats.realCount++; break;
            case NPCLODLevel::Bulk: stats.bulkCount++; break;
            case NPCLODLevel::Virtual: stats.virtualCount++; break;
        }
    }
    return stats;
}

} // namespace systems

// =============================================================================
// Render Batching Helpers
// =============================================================================
// Helpers for efficient batch rendering based on ECS queries.

namespace render {

// Statistics for visibility culling
struct CullStats {
    size_t totalEntities = 0;
    size_t visibleEntities = 0;
    size_t culledEntities = 0;

    [[nodiscard]] float visibilityRatio() const {
        return totalEntities > 0 ?
            static_cast<float>(visibleEntities) / static_cast<float>(totalEntities) : 0.0f;
    }
};

// Count visible vs total entities for profiling
inline CullStats getCullStats(const World& world) {
    CullStats stats;

    // Count entities with transforms (renderable)
    for ([[maybe_unused]] auto entity : world.view<Transform>()) {
        stats.totalEntities++;
    }

    // Count visible entities
    for ([[maybe_unused]] auto entity : world.view<Transform, Visible>()) {
        stats.visibleEntities++;
    }

    stats.culledEntities = stats.totalEntities - stats.visibleEntities;
    return stats;
}

// Batch key for grouping draw calls
struct BatchKey {
    const void* mesh;      // Mesh pointer for comparison
    MaterialId materialId;

    bool operator==(const BatchKey& other) const {
        return mesh == other.mesh && materialId == other.materialId;
    }

    bool operator<(const BatchKey& other) const {
        if (mesh != other.mesh) return mesh < other.mesh;
        return materialId < other.materialId;
    }
};

} // namespace render

} // namespace ecs
