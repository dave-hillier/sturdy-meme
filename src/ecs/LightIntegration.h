#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include "Components.h"
#include "../lighting/Light.h"

// Builds a GPU-compatible light buffer from ECS light entities
// This integrates ECS lights with the existing Light.h system
class LightBufferBuilder {
public:
    // Build light buffer from ECS registry
    // Can optionally merge with additional lights from LightManager
    static uint32_t buildLightBuffer(
        entt::registry& registry,
        LightBuffer& buffer,
        const glm::vec3& cameraPos,
        const glm::vec3& cameraFront,
        const glm::mat4& viewProjMatrix,
        float cullRadius = 100.0f,
        const LightManager* additionalLights = nullptr)
    {
        struct LightCandidate {
            GPULight gpuLight;
            float effectiveWeight;
        };

        std::vector<LightCandidate> candidates;

        // Collect point lights from ECS
        auto pointLights = registry.view<Transform, PointLight, LightEnabled>();
        for (auto entity : pointLights) {
            auto& transform = pointLights.get<Transform>(entity);
            auto& light = pointLights.get<PointLight>(entity);

            // Get world position (use WorldTransform if entity has Hierarchy)
            glm::vec3 worldPos = transform.position;
            if (registry.all_of<WorldTransform>(entity)) {
                worldPos = registry.get<WorldTransform>(entity).getWorldPosition();
            }

            // Frustum culling
            if (!isSphereInFrustum(worldPos, light.radius, viewProjMatrix)) {
                continue;
            }

            float dist = glm::length(worldPos - cameraPos);
            if (dist > cullRadius + light.radius) continue;

            // Calculate weight
            glm::vec3 toLight = glm::normalize(worldPos - cameraPos);
            float angleFactor = glm::max(0.0f, glm::dot(toLight, cameraFront));
            angleFactor = 0.25f + 0.75f * angleFactor;
            float effectiveWeight = (light.priority * angleFactor) / (dist + 1.0f);

            // Convert to GPU format
            GPULight gpu{};
            gpu.positionAndType = glm::vec4(worldPos, 0.0f);  // 0 = point
            gpu.directionAndCone = glm::vec4(0.0f, -1.0f, 0.0f, 1.0f);
            gpu.colorAndIntensity = glm::vec4(light.color, light.intensity);

            int32_t shadowIndex = -1;
            if (registry.all_of<ShadowCaster>(entity)) {
                shadowIndex = registry.get<ShadowCaster>(entity).shadowMapIndex;
            }
            gpu.radiusAndInnerCone = glm::vec4(light.radius, 1.0f, static_cast<float>(shadowIndex), 0.0f);

            candidates.push_back({gpu, effectiveWeight});
        }

        // Collect spot lights from ECS
        auto spotLights = registry.view<Transform, SpotLight, LightEnabled>();
        for (auto entity : spotLights) {
            auto& transform = spotLights.get<Transform>(entity);
            auto& light = spotLights.get<SpotLight>(entity);

            // Get world position and rotation (use WorldTransform if entity has Hierarchy)
            glm::vec3 worldPos = transform.position;
            glm::quat worldRot = transform.rotation;
            if (registry.all_of<WorldTransform>(entity)) {
                const auto& worldTransform = registry.get<WorldTransform>(entity);
                worldPos = worldTransform.getWorldPosition();
                worldRot = worldTransform.getWorldRotation();
            }

            // Frustum culling
            if (!isSphereInFrustum(worldPos, light.radius, viewProjMatrix)) {
                continue;
            }

            float dist = glm::length(worldPos - cameraPos);
            if (dist > cullRadius + light.radius) continue;

            // Calculate weight
            glm::vec3 toLight = glm::normalize(worldPos - cameraPos);
            float angleFactor = glm::max(0.0f, glm::dot(toLight, cameraFront));
            angleFactor = 0.25f + 0.75f * angleFactor;
            float effectiveWeight = (light.priority * angleFactor) / (dist + 1.0f);

            // Get direction from world rotation
            glm::vec3 direction = worldRot * glm::vec3(0.0f, 0.0f, 1.0f);

            // Convert to GPU format
            GPULight gpu{};
            gpu.positionAndType = glm::vec4(worldPos, 1.0f);  // 1 = spot
            gpu.directionAndCone = glm::vec4(
                glm::normalize(direction),
                glm::cos(glm::radians(light.outerConeAngle))
            );
            gpu.colorAndIntensity = glm::vec4(light.color, light.intensity);

            int32_t shadowIndex = -1;
            if (registry.all_of<ShadowCaster>(entity)) {
                shadowIndex = registry.get<ShadowCaster>(entity).shadowMapIndex;
            }
            gpu.radiusAndInnerCone = glm::vec4(
                light.radius,
                glm::cos(glm::radians(light.innerConeAngle)),
                static_cast<float>(shadowIndex),
                0.0f
            );

            candidates.push_back({gpu, effectiveWeight});
        }

        // Optionally add lights from existing LightManager
        if (additionalLights) {
            for (size_t i = 0; i < additionalLights->getLightCount(); i++) {
                const Light& light = additionalLights->getLight(i);
                if (!light.enabled) continue;

                if (!isSphereInFrustum(light.position, light.radius, viewProjMatrix)) {
                    continue;
                }

                float dist = glm::length(light.position - cameraPos);
                if (dist > cullRadius + light.radius) continue;

                glm::vec3 toLight = glm::normalize(light.position - cameraPos);
                float angleFactor = glm::max(0.0f, glm::dot(toLight, cameraFront));
                angleFactor = 0.25f + 0.75f * angleFactor;
                float effectiveWeight = (light.priority * angleFactor) / (dist + 1.0f);

                candidates.push_back({light.toGPU(), effectiveWeight});
            }
        }

        // Sort by weight (descending)
        std::sort(candidates.begin(), candidates.end(),
            [](const LightCandidate& a, const LightCandidate& b) {
                return a.effectiveWeight > b.effectiveWeight;
            });

        // Copy to buffer
        uint32_t count = static_cast<uint32_t>(std::min(candidates.size(), static_cast<size_t>(MAX_LIGHTS)));
        buffer.lightCount = glm::uvec4(count, 0, 0, 0);

        for (uint32_t i = 0; i < count; i++) {
            buffer.lights[i] = candidates[i].gpuLight;
        }

        // Zero out unused slots
        for (uint32_t i = count; i < MAX_LIGHTS; i++) {
            buffer.lights[i] = GPULight{};
        }

        return count;
    }

    // Sync ECS lights to LightManager (for systems that still use LightManager directly)
    static void syncToLightManager(entt::registry& registry, LightManager& manager) {
        manager.clear();

        // Add point lights
        auto pointLights = registry.view<Transform, PointLight, LightEnabled>();
        for (auto entity : pointLights) {
            auto& transform = pointLights.get<Transform>(entity);
            auto& ecsLight = pointLights.get<PointLight>(entity);

            // Get world position (use WorldTransform if entity has Hierarchy)
            glm::vec3 worldPos = transform.position;
            if (registry.all_of<WorldTransform>(entity)) {
                worldPos = registry.get<WorldTransform>(entity).getWorldPosition();
            }

            Light light;
            light.type = LightType::Point;
            light.position = worldPos;
            light.color = ecsLight.color;
            light.intensity = ecsLight.intensity;
            light.radius = ecsLight.radius;
            light.castsShadows = ecsLight.castsShadows;
            light.priority = ecsLight.priority;
            light.enabled = true;

            if (registry.all_of<ShadowCaster>(entity)) {
                light.shadowMapIndex = registry.get<ShadowCaster>(entity).shadowMapIndex;
            }

            manager.addLight(light);
        }

        // Add spot lights
        auto spotLights = registry.view<Transform, SpotLight, LightEnabled>();
        for (auto entity : spotLights) {
            auto& transform = spotLights.get<Transform>(entity);
            auto& ecsLight = spotLights.get<SpotLight>(entity);

            // Get world position and rotation (use WorldTransform if entity has Hierarchy)
            glm::vec3 worldPos = transform.position;
            glm::quat worldRot = transform.rotation;
            if (registry.all_of<WorldTransform>(entity)) {
                const auto& worldTransform = registry.get<WorldTransform>(entity);
                worldPos = worldTransform.getWorldPosition();
                worldRot = worldTransform.getWorldRotation();
            }

            Light light;
            light.type = LightType::Spot;
            light.position = worldPos;
            light.rotation = worldRot;
            light.color = ecsLight.color;
            light.intensity = ecsLight.intensity;
            light.radius = ecsLight.radius;
            light.innerConeAngle = ecsLight.innerConeAngle;
            light.outerConeAngle = ecsLight.outerConeAngle;
            light.castsShadows = ecsLight.castsShadows;
            light.priority = ecsLight.priority;
            light.enabled = true;

            if (registry.all_of<ShadowCaster>(entity)) {
                light.shadowMapIndex = registry.get<ShadowCaster>(entity).shadowMapIndex;
            }

            manager.addLight(light);
        }
    }
};
