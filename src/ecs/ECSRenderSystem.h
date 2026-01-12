#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Components.h"
#include "LightIntegration.h"

// ECS Render System - Bridge between ECS entities and the rendering pipeline
// Provides queries for lights, cameras, and renderables from the ECS registry
namespace ECSRender {

// ============================================================================
// Light System
// ============================================================================

// Build light buffer directly from ECS registry
// This is the primary method for ECS-based light rendering
inline uint32_t buildLightBuffer(
    entt::registry& registry,
    LightBuffer& buffer,
    const glm::vec3& cameraPos,
    const glm::vec3& cameraFront,
    const glm::mat4& viewProjMatrix,
    float cullRadius = 100.0f)
{
    return LightBufferBuilder::buildLightBuffer(
        registry, buffer, cameraPos, cameraFront, viewProjMatrix, cullRadius, nullptr);
}

// Count enabled lights in registry
inline uint32_t countEnabledLights(entt::registry& registry) {
    uint32_t count = 0;
    count += static_cast<uint32_t>(registry.view<Transform, PointLight, LightEnabled>().size());
    count += static_cast<uint32_t>(registry.view<Transform, SpotLight, LightEnabled>().size());
    return count;
}

// Get all point light entities
inline auto getPointLights(entt::registry& registry) {
    return registry.view<Transform, PointLight, LightEnabled>();
}

// Get all spot light entities
inline auto getSpotLights(entt::registry& registry) {
    return registry.view<Transform, SpotLight, LightEnabled>();
}

// Get shadow casting lights
inline std::vector<entt::entity> getShadowCasters(entt::registry& registry) {
    std::vector<entt::entity> casters;

    auto pointView = registry.view<Transform, PointLight, LightEnabled, ShadowCaster>();
    for (auto entity : pointView) {
        casters.push_back(entity);
    }

    auto spotView = registry.view<Transform, SpotLight, LightEnabled, ShadowCaster>();
    for (auto entity : spotView) {
        casters.push_back(entity);
    }

    return casters;
}

// ============================================================================
// Camera System
// ============================================================================

// Find the main camera entity
inline entt::entity findMainCamera(entt::registry& registry) {
    auto view = registry.view<Transform, CameraComponent, MainCamera>();
    if (view.begin() != view.end()) {
        return *view.begin();
    }
    return entt::null;
}

// Find all camera entities sorted by priority (highest first)
inline std::vector<entt::entity> getCamerasByPriority(entt::registry& registry) {
    std::vector<std::pair<entt::entity, int>> cameras;

    auto view = registry.view<Transform, CameraComponent>();
    for (auto entity : view) {
        auto& cam = view.get<CameraComponent>(entity);
        cameras.push_back({entity, cam.priority});
    }

    std::sort(cameras.begin(), cameras.end(),
              [](auto& a, auto& b) { return a.second > b.second; });

    std::vector<entt::entity> result;
    result.reserve(cameras.size());
    for (auto& [entity, priority] : cameras) {
        result.push_back(entity);
    }
    return result;
}

// Build view matrix from ECS camera entity
inline glm::mat4 buildViewMatrix(entt::registry& registry, entt::entity camera) {
    if (!registry.valid(camera)) return glm::mat4(1.0f);

    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;

    // Get position from WorldTransform if available, otherwise Transform
    if (registry.all_of<WorldTransform>(camera)) {
        auto& world = registry.get<WorldTransform>(camera);
        position = world.position;
        yaw = world.yaw;
    } else if (registry.all_of<Transform>(camera)) {
        auto& transform = registry.get<Transform>(camera);
        position = transform.position;
        yaw = transform.yaw;
    }

    // Calculate view direction from yaw
    float yawRad = glm::radians(yaw);
    glm::vec3 front = glm::normalize(glm::vec3(
        sin(yawRad),
        0.0f,
        cos(yawRad)
    ));
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

    return glm::lookAt(position, position + front, up);
}

// Build projection matrix from ECS camera entity
inline glm::mat4 buildProjectionMatrix(entt::registry& registry, entt::entity camera, float aspectRatio) {
    if (!registry.valid(camera) || !registry.all_of<CameraComponent>(camera)) {
        // Return default projection
        return glm::perspective(glm::radians(60.0f), aspectRatio, 0.1f, 1000.0f);
    }

    auto& cam = registry.get<CameraComponent>(camera);
    glm::mat4 proj = glm::perspective(
        glm::radians(cam.fov),
        aspectRatio,
        cam.nearPlane,
        cam.farPlane
    );
    proj[1][1] *= -1;  // Vulkan Y-flip
    return proj;
}

// Get camera position from entity
inline glm::vec3 getCameraPosition(entt::registry& registry, entt::entity camera) {
    if (!registry.valid(camera)) return glm::vec3(0.0f);

    if (registry.all_of<WorldTransform>(camera)) {
        return registry.get<WorldTransform>(camera).position;
    }
    if (registry.all_of<Transform>(camera)) {
        return registry.get<Transform>(camera).position;
    }
    return glm::vec3(0.0f);
}

// Get camera forward direction from entity
inline glm::vec3 getCameraFront(entt::registry& registry, entt::entity camera) {
    if (!registry.valid(camera)) return glm::vec3(0.0f, 0.0f, 1.0f);

    float yaw = 0.0f;
    if (registry.all_of<WorldTransform>(camera)) {
        yaw = registry.get<WorldTransform>(camera).yaw;
    } else if (registry.all_of<Transform>(camera)) {
        yaw = registry.get<Transform>(camera).yaw;
    }

    float yawRad = glm::radians(yaw);
    return glm::normalize(glm::vec3(sin(yawRad), 0.0f, cos(yawRad)));
}

// ============================================================================
// Renderable System
// ============================================================================

// Get all mesh renderable entities
inline auto getMeshRenderables(entt::registry& registry) {
    return registry.view<WorldTransform, MeshRenderer>();
}

// Get all visible mesh renderables (with visibility check)
inline std::vector<entt::entity> getVisibleMeshRenderables(entt::registry& registry) {
    std::vector<entt::entity> result;

    auto view = registry.view<WorldTransform, MeshRenderer>();
    for (auto entity : view) {
        // Check EntityInfo visibility if present
        if (registry.all_of<EntityInfo>(entity)) {
            if (!registry.get<EntityInfo>(entity).visible) {
                continue;
            }
        }
        result.push_back(entity);
    }
    return result;
}

// Get mesh renderables that cast shadows
inline std::vector<entt::entity> getShadowCastingMeshes(entt::registry& registry) {
    std::vector<entt::entity> result;

    auto view = registry.view<WorldTransform, MeshRenderer>();
    for (auto entity : view) {
        auto& mesh = view.get<MeshRenderer>(entity);
        if (mesh.castsShadow) {
            result.push_back(entity);
        }
    }
    return result;
}

// Frustum cull mesh renderables
inline std::vector<entt::entity> frustumCullMeshes(
    entt::registry& registry,
    const glm::mat4& viewProjMatrix)
{
    std::vector<entt::entity> visible;

    auto view = registry.view<WorldTransform, MeshRenderer, AABBBounds>();
    for (auto entity : view) {
        auto& world = view.get<WorldTransform>(entity);
        auto& bounds = view.get<AABBBounds>(entity);

        // Calculate world-space bounding sphere from AABB
        glm::vec3 center = world.position + bounds.center();
        float radius = glm::length(bounds.extents());

        if (isSphereInFrustum(center, radius, viewProjMatrix)) {
            visible.push_back(entity);

            // Mark as visible for next frame queries
            if (!registry.all_of<WasVisible>(entity)) {
                registry.emplace<WasVisible>(entity);
            }
        } else {
            // Mark as not visible
            if (registry.all_of<WasVisible>(entity)) {
                registry.remove<WasVisible>(entity);
            }
        }
    }

    return visible;
}

// ============================================================================
// LOD System
// ============================================================================

// Update LOD levels based on camera distance
inline void updateLODLevels(entt::registry& registry, const glm::vec3& cameraPos) {
    auto view = registry.view<WorldTransform, LODGroup>();
    for (auto entity : view) {
        auto& world = view.get<WorldTransform>(entity);
        auto& lod = view.get<LODGroup>(entity);

        float distance = glm::length(world.position - cameraPos);

        // Find appropriate LOD level
        int newLOD = 0;
        for (size_t i = 0; i < lod.switchDistances.size(); i++) {
            if (distance > lod.switchDistances[i]) {
                newLOD = static_cast<int>(i) + 1;
            }
        }

        // Clamp to available meshes
        newLOD = std::min(newLOD, static_cast<int>(lod.lodMeshes.size()) - 1);
        newLOD = std::max(newLOD, 0);

        lod.currentLOD = newLOD;
    }
}

// Get current LOD mesh for an entity
inline MeshHandle getCurrentLODMesh(entt::registry& registry, entt::entity entity) {
    if (!registry.all_of<LODGroup>(entity)) {
        // No LOD, check for regular mesh renderer
        if (registry.all_of<MeshRenderer>(entity)) {
            return registry.get<MeshRenderer>(entity).mesh;
        }
        return InvalidMesh;
    }

    auto& lod = registry.get<LODGroup>(entity);
    if (lod.currentLOD >= 0 && lod.currentLOD < static_cast<int>(lod.lodMeshes.size())) {
        return lod.lodMeshes[lod.currentLOD];
    }
    return InvalidMesh;
}

// ============================================================================
// Render Layer Filtering
// ============================================================================

// Get entities matching a render layer mask
inline std::vector<entt::entity> getEntitiesByLayer(
    entt::registry& registry,
    uint32_t layerMask)
{
    std::vector<entt::entity> result;

    auto view = registry.view<MeshRenderer>();
    for (auto entity : view) {
        auto& mesh = view.get<MeshRenderer>(entity);
        if (static_cast<uint32_t>(mesh.layer) & layerMask) {
            result.push_back(entity);
        }
    }
    return result;
}

// ============================================================================
// World Transform Updates
// ============================================================================

// Update all dirty world transforms (should be called once per frame)
inline void updateWorldTransforms(entt::registry& registry) {
    auto view = registry.view<WorldTransform>();
    for (auto entity : view) {
        auto& world = view.get<WorldTransform>(entity);
        if (!world.dirty) continue;

        // Get parent matrix if hierarchical
        glm::mat4 parentMatrix = glm::mat4(1.0f);
        if (registry.all_of<Hierarchy>(entity)) {
            auto& hierarchy = registry.get<Hierarchy>(entity);
            if (hierarchy.parent != entt::null && registry.valid(hierarchy.parent)) {
                // Ensure parent is updated first (recursive dependency)
                if (registry.all_of<WorldTransform>(hierarchy.parent)) {
                    auto& parentWorld = registry.get<WorldTransform>(hierarchy.parent);
                    if (parentWorld.dirty) {
                        // This would need recursion or topological sort for deep hierarchies
                        // For now, assume parent was already updated
                    }
                    parentMatrix = parentWorld.matrix;
                }
            }

            // Build local matrix
            glm::mat4 localMatrix = glm::mat4(1.0f);
            localMatrix = glm::translate(localMatrix, hierarchy.localPosition);
            localMatrix = glm::rotate(localMatrix, glm::radians(hierarchy.localYaw), glm::vec3(0, 1, 0));
            localMatrix = glm::scale(localMatrix, hierarchy.localScale);

            world.matrix = parentMatrix * localMatrix;
            world.position = glm::vec3(world.matrix[3]);
            world.scale = hierarchy.localScale;  // Simplified
            world.yaw = hierarchy.localYaw;
        } else if (registry.all_of<Transform>(entity)) {
            // Non-hierarchical entity - use Transform directly
            auto& transform = registry.get<Transform>(entity);
            world.matrix = glm::mat4(1.0f);
            world.matrix = glm::translate(world.matrix, transform.position);
            world.matrix = glm::rotate(world.matrix, glm::radians(transform.yaw), glm::vec3(0, 1, 0));
            world.position = transform.position;
            world.yaw = transform.yaw;
            world.scale = glm::vec3(1.0f);
        }

        world.dirty = false;
    }
}

}  // namespace ECSRender
