#pragma once

#include "Light.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include "scene/RotationUtils.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <random>

namespace ecs {
namespace light {

// =============================================================================
// Light Collection Result
// =============================================================================
// Result of collecting lights from ECS for building GPU buffer

struct CollectedLight {
    Light light;           // Light data in existing format
    Entity entity;         // Source entity for reference
    float distanceToCamera;
    float effectiveWeight; // For priority sorting
};

struct LightCollectionResult {
    std::vector<CollectedLight> pointLights;
    std::vector<CollectedLight> spotLights;
    std::vector<CollectedLight> directionalLights;

    [[nodiscard]] size_t totalCount() const {
        return pointLights.size() + spotLights.size() + directionalLights.size();
    }

    void clear() {
        pointLights.clear();
        spotLights.clear();
        directionalLights.clear();
    }
};

// =============================================================================
// Flicker Utility Functions
// =============================================================================
// Simple noise-based flickering without external dependencies

namespace detail {

// Simple hash-based pseudo-random for flicker
inline float hash(float n) {
    return glm::fract(std::sin(n) * 43758.5453123f);
}

// Value noise for smooth flickering
inline float valueNoise(float x) {
    float i = std::floor(x);
    float f = glm::fract(x);
    // Smoothstep interpolation
    float u = f * f * (3.0f - 2.0f * f);
    return glm::mix(hash(i), hash(i + 1.0f), u);
}

// Multi-octave noise for more natural flicker
inline float flickerNoise(float time, float speed, float scale) {
    float noise = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmplitude = 0.0f;

    // 3 octaves for natural-looking flicker
    for (int i = 0; i < 3; i++) {
        noise += valueNoise(time * speed * frequency * scale) * amplitude;
        maxAmplitude += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return noise / maxAmplitude;  // Normalize to [0, 1]
}

} // namespace detail

// =============================================================================
// Light System Functions
// =============================================================================

// Initialize flicker components with random phase offsets
// Call this when creating new light entities with flicker
inline void initializeFlickerPhase(LightFlickerComponent& flicker) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> dist(0.0f, 100.0f);
    flicker.phase = dist(gen);
}

// Update all flicker components
// deltaTime: time since last frame in seconds
inline void updateFlicker(World& world, float deltaTime) {
    float time = 0.0f;  // Could be passed in for global time sync

    // Update point lights with flicker
    for (auto [entity, pointLight, flicker] :
         world.view<PointLightComponent, LightFlickerComponent>().each()) {

        // Store base intensity on first update
        if (flicker.baseIntensity <= 0.0f) {
            flicker.baseIntensity = pointLight.properties.intensity;
        }

        // Advance phase
        flicker.phase += deltaTime;

        // Calculate noise-based modifier
        float noise = detail::flickerNoise(flicker.phase, flicker.flickerSpeed, flicker.noiseScale);

        // Map noise [0,1] to intensity modifier [1-flickerAmount, 1]
        flicker.currentModifier = 1.0f - flicker.flickerAmount * (1.0f - noise);

        // Apply to light intensity
        pointLight.properties.intensity = flicker.baseIntensity * flicker.currentModifier;
    }

    // Update spot lights with flicker
    for (auto [entity, spotLight, flicker] :
         world.view<SpotLightComponent, LightFlickerComponent>().each()) {

        if (flicker.baseIntensity <= 0.0f) {
            flicker.baseIntensity = spotLight.properties.intensity;
        }

        flicker.phase += deltaTime;
        float noise = detail::flickerNoise(flicker.phase, flicker.flickerSpeed, flicker.noiseScale);
        flicker.currentModifier = 1.0f - flicker.flickerAmount * (1.0f - noise);
        spotLight.properties.intensity = flicker.baseIntensity * flicker.currentModifier;
    }
}

// Extract direction from transform for spot/directional lights
inline glm::vec3 getDirectionFromTransform(const Transform& transform) {
    // Extract rotation from matrix and compute direction
    // Default direction is -Y (pointing down)
    glm::mat3 rotationMatrix = glm::mat3(transform.matrix);
    return glm::normalize(rotationMatrix * glm::vec3(0.0f, -1.0f, 0.0f));
}

// Convert ECS point light component to Light struct
inline Light pointLightToLight(const PointLightComponent& component,
                                const Transform& transform) {
    Light light;
    light.type = ::LightType::Point;
    light.position = transform.position();
    light.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Not used for point lights
    light.color = component.properties.color;
    light.intensity = component.properties.intensity;
    light.radius = component.radius;
    light.priority = component.properties.priority;
    light.castsShadows = component.properties.castsShadows;
    light.shadowMapIndex = component.properties.shadowMapIndex;
    light.enabled = component.properties.enabled;
    return light;
}

// Convert ECS spot light component to Light struct
inline Light spotLightToLight(const SpotLightComponent& component,
                               const Transform& transform) {
    Light light;
    light.type = ::LightType::Spot;
    light.position = transform.position();

    // Get rotation from transform matrix
    glm::mat3 rotMat = glm::mat3(transform.matrix);
    // Normalize columns to remove scale
    rotMat[0] = glm::normalize(rotMat[0]);
    rotMat[1] = glm::normalize(rotMat[1]);
    rotMat[2] = glm::normalize(rotMat[2]);
    light.rotation = glm::quat_cast(rotMat);

    light.color = component.properties.color;
    light.intensity = component.properties.intensity;
    light.radius = component.radius;
    light.innerConeAngle = component.innerConeAngle;
    light.outerConeAngle = component.outerConeAngle;
    light.priority = component.properties.priority;
    light.castsShadows = component.properties.castsShadows;
    light.shadowMapIndex = component.properties.shadowMapIndex;
    light.enabled = component.properties.enabled;
    return light;
}

// Convert ECS directional light component to Light struct
inline Light directionalLightToLight(const DirectionalLightComponent& component,
                                      const Transform& transform) {
    Light light;
    light.type = ::LightType::Directional;
    light.position = glm::vec3(0.0f);  // Position doesn't matter

    // Get rotation from transform matrix
    glm::mat3 rotMat = glm::mat3(transform.matrix);
    rotMat[0] = glm::normalize(rotMat[0]);
    rotMat[1] = glm::normalize(rotMat[1]);
    rotMat[2] = glm::normalize(rotMat[2]);
    light.rotation = glm::quat_cast(rotMat);

    light.color = component.properties.color;
    light.intensity = component.properties.intensity;
    light.radius = 0.0f;  // Infinite range
    light.priority = component.properties.priority;
    light.castsShadows = component.properties.castsShadows;
    light.shadowMapIndex = component.properties.shadowMapIndex;
    light.enabled = component.properties.enabled;
    return light;
}

// Collect all lights from ECS into a result structure
// cameraPos: for distance-based sorting
// cameraFront: for view-direction weighting
inline LightCollectionResult collectLights(const World& world,
                                            const glm::vec3& cameraPos,
                                            const glm::vec3& cameraFront) {
    LightCollectionResult result;

    // Collect point lights
    for (auto [entity, pointLight, transform] :
         world.view<PointLightComponent, Transform>().each()) {

        if (!pointLight.properties.enabled) continue;

        CollectedLight collected;
        collected.entity = entity;
        collected.light = pointLightToLight(pointLight, transform);
        collected.distanceToCamera = glm::length(transform.position() - cameraPos);

        // Calculate effective weight for prioritization
        glm::vec3 toLight = glm::normalize(transform.position() - cameraPos);
        float angleFactor = glm::max(0.0f, glm::dot(toLight, cameraFront));
        angleFactor = 0.25f + 0.75f * angleFactor;
        collected.effectiveWeight = (pointLight.properties.priority * angleFactor) /
                                    (collected.distanceToCamera + 1.0f);

        result.pointLights.push_back(collected);
    }

    // Collect spot lights
    for (auto [entity, spotLight, transform] :
         world.view<SpotLightComponent, Transform>().each()) {

        if (!spotLight.properties.enabled) continue;

        CollectedLight collected;
        collected.entity = entity;
        collected.light = spotLightToLight(spotLight, transform);
        collected.distanceToCamera = glm::length(transform.position() - cameraPos);

        glm::vec3 toLight = glm::normalize(transform.position() - cameraPos);
        float angleFactor = glm::max(0.0f, glm::dot(toLight, cameraFront));
        angleFactor = 0.25f + 0.75f * angleFactor;
        collected.effectiveWeight = (spotLight.properties.priority * angleFactor) /
                                    (collected.distanceToCamera + 1.0f);

        result.spotLights.push_back(collected);
    }

    // Collect directional lights (always high priority, no distance culling)
    for (auto [entity, dirLight, transform] :
         world.view<DirectionalLightComponent, Transform>().each()) {

        if (!dirLight.properties.enabled) continue;

        CollectedLight collected;
        collected.entity = entity;
        collected.light = directionalLightToLight(dirLight, transform);
        collected.distanceToCamera = 0.0f;  // Always closest
        collected.effectiveWeight = dirLight.properties.priority;

        result.directionalLights.push_back(collected);
    }

    return result;
}

// Build GPU light buffer from ECS lights with culling and prioritization
// viewProjMatrix: for frustum culling
// cullRadius: maximum distance from camera
// Returns number of lights written to buffer
inline uint32_t buildLightBuffer(const World& world,
                                  LightBuffer& buffer,
                                  const glm::vec3& cameraPos,
                                  const glm::vec3& cameraFront,
                                  const glm::mat4& viewProjMatrix,
                                  float cullRadius = 100.0f) {

    // Collect all lights
    LightCollectionResult collected = collectLights(world, cameraPos, cameraFront);

    // Merge all lights into a single list for sorting
    std::vector<CollectedLight> allLights;
    allLights.reserve(collected.totalCount());

    // Directional lights first (always included)
    for (auto& light : collected.directionalLights) {
        allLights.push_back(light);
    }

    // Point lights with frustum culling
    for (auto& light : collected.pointLights) {
        if (light.distanceToCamera > cullRadius + light.light.radius) continue;
        if (!isSphereInFrustum(light.light.position, light.light.radius, viewProjMatrix)) continue;
        allLights.push_back(light);
    }

    // Spot lights with frustum culling
    for (auto& light : collected.spotLights) {
        if (light.distanceToCamera > cullRadius + light.light.radius) continue;
        if (!isSphereInFrustum(light.light.position, light.light.radius, viewProjMatrix)) continue;
        allLights.push_back(light);
    }

    // Sort by effective weight (descending)
    std::sort(allLights.begin(), allLights.end(),
        [](const CollectedLight& a, const CollectedLight& b) {
            return a.effectiveWeight > b.effectiveWeight;
        });

    // Write to buffer (up to MAX_LIGHTS)
    uint32_t count = static_cast<uint32_t>(std::min(allLights.size(), static_cast<size_t>(MAX_LIGHTS)));
    buffer.lightCount = glm::uvec4(count, 0, 0, 0);

    for (uint32_t i = 0; i < count; i++) {
        buffer.lights[i] = allLights[i].light.toGPU();
    }

    // Zero out unused slots
    for (uint32_t i = count; i < MAX_LIGHTS; i++) {
        buffer.lights[i] = GPULight{};
    }

    return count;
}

// =============================================================================
// Light Entity Creation Helpers
// =============================================================================
// Helper functions to create light entities with proper components

// Create a point light entity with BoundingSphere for visibility culling
inline Entity createPointLight(World& world,
                                const glm::vec3& position,
                                const glm::vec3& color = glm::vec3(1.0f),
                                float intensity = 1.0f,
                                float radius = 10.0f) {
    Entity entity = world.create();
    world.add<Transform>(entity, Transform::fromPosition(position));
    world.add<PointLightComponent>(entity, color, intensity, radius);
    world.add<LightSourceTag>(entity);
    // BoundingSphere for visibility culling - center at origin (local), radius from light
    world.add<BoundingSphere>(entity, glm::vec3(0.0f), radius);
    return entity;
}

// Create a point light entity with flicker
inline Entity createFlickeringPointLight(World& world,
                                          const glm::vec3& position,
                                          const PointLightComponent& light,
                                          const LightFlickerComponent& flicker) {
    Entity entity = world.create();
    world.add<Transform>(entity, Transform::fromPosition(position));
    world.add<PointLightComponent>(entity, light);

    LightFlickerComponent flickerCopy = flicker;
    flickerCopy.baseIntensity = light.properties.intensity;
    initializeFlickerPhase(flickerCopy);
    world.add<LightFlickerComponent>(entity, flickerCopy);

    world.add<LightSourceTag>(entity);
    // BoundingSphere for visibility culling
    world.add<BoundingSphere>(entity, glm::vec3(0.0f), light.radius);
    return entity;
}

// Create a torch light (point light + flicker)
inline Entity createTorch(World& world, const glm::vec3& position, float intensity = 2.0f) {
    return createFlickeringPointLight(world, position,
        PointLightComponent::torch(intensity),
        LightFlickerComponent::torch());
}

// Create a spot light entity with BoundingSphere for visibility culling
inline Entity createSpotLight(World& world,
                               const glm::vec3& position,
                               const glm::vec3& direction,
                               const glm::vec3& color = glm::vec3(1.0f),
                               float intensity = 1.0f,
                               float radius = 15.0f,
                               float innerAngle = 30.0f,
                               float outerAngle = 45.0f) {
    Entity entity = world.create();

    glm::quat rotation = RotationUtils::rotationFromDirection(direction);
    world.add<Transform>(entity, Transform::fromPositionRotation(position, rotation));
    world.add<SpotLightComponent>(entity, color, intensity, radius, innerAngle, outerAngle);
    world.add<LightSourceTag>(entity);
    // BoundingSphere for visibility culling - uses light radius
    world.add<BoundingSphere>(entity, glm::vec3(0.0f), radius);
    return entity;
}

// Create a directional light entity (sun/moon)
inline Entity createDirectionalLight(World& world,
                                      const glm::vec3& direction,
                                      const glm::vec3& color = glm::vec3(1.0f),
                                      float intensity = 1.0f) {
    Entity entity = world.create();

    glm::quat rotation = RotationUtils::rotationFromDirection(direction);
    world.add<Transform>(entity, Transform::fromPositionRotation(glm::vec3(0.0f), rotation));
    world.add<DirectionalLightComponent>(entity, color, intensity);
    world.add<LightSourceTag>(entity);
    return entity;
}

// Create sun light with typical parameters
inline Entity createSun(World& world,
                         const glm::vec3& direction = glm::vec3(-0.3f, -0.8f, -0.5f),
                         float intensity = 1.0f) {
    Entity entity = world.create();

    glm::quat rotation = RotationUtils::rotationFromDirection(glm::normalize(direction));
    world.add<Transform>(entity, Transform::fromPositionRotation(glm::vec3(0.0f), rotation));
    world.add<DirectionalLightComponent>(entity, DirectionalLightComponent::sun(intensity));
    world.add<LightSourceTag>(entity);
    return entity;
}

// =============================================================================
// Child Light Entity Creation Helpers
// =============================================================================
// Create light entities as children of other entities using ECS hierarchy.
// The light's world Transform is computed from parent * LocalTransform,
// so the light automatically follows the parent entity.

// Create a point light as a child of an existing entity
// localOffset: position relative to parent (default: same position)
inline Entity createChildPointLight(World& world,
                                     Entity parent,
                                     const glm::vec3& color = glm::vec3(1.0f),
                                     float intensity = 1.0f,
                                     float radius = 10.0f,
                                     const glm::vec3& localOffset = glm::vec3(0.0f)) {
    Entity entity = world.create();
    world.add<Transform>(entity);  // World transform computed by hierarchy system
    world.add<LocalTransform>(entity, localOffset, glm::quat(1,0,0,0), glm::vec3(1));
    world.add<Parent>(entity, parent);
    world.add<PointLightComponent>(entity, color, intensity, radius);
    world.add<LightSourceTag>(entity);
    world.add<BoundingSphere>(entity, glm::vec3(0.0f), radius);

    // Add to parent's Children list
    if (world.has<Children>(parent)) {
        world.get<Children>(parent).add(entity);
    } else {
        Children children;
        children.add(entity);
        world.add<Children>(parent, children);
    }

    // Set hierarchy depth
    uint16_t parentDepth = 0;
    if (world.has<HierarchyDepth>(parent)) {
        parentDepth = world.get<HierarchyDepth>(parent).depth;
    }
    world.add<HierarchyDepth>(entity, static_cast<uint16_t>(parentDepth + 1));

    return entity;
}

// Create a flickering point light as a child of an existing entity
inline Entity createChildFlickeringPointLight(World& world,
                                               Entity parent,
                                               const PointLightComponent& light,
                                               const LightFlickerComponent& flicker,
                                               const glm::vec3& localOffset = glm::vec3(0.0f)) {
    Entity entity = world.create();
    world.add<Transform>(entity);
    world.add<LocalTransform>(entity, localOffset, glm::quat(1,0,0,0), glm::vec3(1));
    world.add<Parent>(entity, parent);
    world.add<PointLightComponent>(entity, light);

    LightFlickerComponent flickerCopy = flicker;
    flickerCopy.baseIntensity = light.properties.intensity;
    initializeFlickerPhase(flickerCopy);
    world.add<LightFlickerComponent>(entity, flickerCopy);

    world.add<LightSourceTag>(entity);
    world.add<BoundingSphere>(entity, glm::vec3(0.0f), light.radius);

    // Add to parent's Children list
    if (world.has<Children>(parent)) {
        world.get<Children>(parent).add(entity);
    } else {
        Children children;
        children.add(entity);
        world.add<Children>(parent, children);
    }

    uint16_t parentDepth = 0;
    if (world.has<HierarchyDepth>(parent)) {
        parentDepth = world.get<HierarchyDepth>(parent).depth;
    }
    world.add<HierarchyDepth>(entity, static_cast<uint16_t>(parentDepth + 1));

    return entity;
}

// Create a torch light (point light + flicker) as a child of an existing entity
inline Entity createChildTorch(World& world,
                                Entity parent,
                                float intensity = 2.0f,
                                const glm::vec3& localOffset = glm::vec3(0.0f)) {
    return createChildFlickeringPointLight(world, parent,
        PointLightComponent::torch(intensity),
        LightFlickerComponent::torch(),
        localOffset);
}

// Create a spot light as a child of an existing entity
inline Entity createChildSpotLight(World& world,
                                    Entity parent,
                                    const glm::vec3& direction,
                                    const glm::vec3& color = glm::vec3(1.0f),
                                    float intensity = 1.0f,
                                    float radius = 15.0f,
                                    float innerAngle = 30.0f,
                                    float outerAngle = 45.0f,
                                    const glm::vec3& localOffset = glm::vec3(0.0f)) {
    Entity entity = world.create();

    glm::quat rotation = RotationUtils::rotationFromDirection(direction);
    world.add<Transform>(entity);
    world.add<LocalTransform>(entity, localOffset, rotation, glm::vec3(1));
    world.add<Parent>(entity, parent);
    world.add<SpotLightComponent>(entity, color, intensity, radius, innerAngle, outerAngle);
    world.add<LightSourceTag>(entity);
    world.add<BoundingSphere>(entity, glm::vec3(0.0f), radius);

    if (world.has<Children>(parent)) {
        world.get<Children>(parent).add(entity);
    } else {
        Children children;
        children.add(entity);
        world.add<Children>(parent, children);
    }

    uint16_t parentDepth = 0;
    if (world.has<HierarchyDepth>(parent)) {
        parentDepth = world.get<HierarchyDepth>(parent).depth;
    }
    world.add<HierarchyDepth>(entity, static_cast<uint16_t>(parentDepth + 1));

    return entity;
}

// =============================================================================
// Light Query Helpers
// =============================================================================

// Get total number of light entities
inline size_t getLightCount(const World& world) {
    size_t count = 0;
    for ([[maybe_unused]] auto entity : world.view<LightSourceTag>()) {
        count++;
    }
    return count;
}

// Get statistics about lights
struct LightStats {
    size_t pointLights = 0;
    size_t spotLights = 0;
    size_t directionalLights = 0;
    size_t flickeringLights = 0;
    size_t totalLights = 0;
    size_t enabledLights = 0;
};

inline LightStats getLightStats(const World& world) {
    LightStats stats;

    for (auto [entity, light] : world.view<PointLightComponent>().each()) {
        stats.pointLights++;
        stats.totalLights++;
        if (light.properties.enabled) stats.enabledLights++;
    }

    for (auto [entity, light] : world.view<SpotLightComponent>().each()) {
        stats.spotLights++;
        stats.totalLights++;
        if (light.properties.enabled) stats.enabledLights++;
    }

    for (auto [entity, light] : world.view<DirectionalLightComponent>().each()) {
        stats.directionalLights++;
        stats.totalLights++;
        if (light.properties.enabled) stats.enabledLights++;
    }

    for ([[maybe_unused]] auto entity : world.view<LightFlickerComponent>()) {
        stats.flickeringLights++;
    }

    return stats;
}

// Find the nearest light entity to a position
inline Entity findNearestLight(const World& world, const glm::vec3& position) {
    Entity nearest = NullEntity;
    float nearestDist = std::numeric_limits<float>::max();

    // Check point lights
    for (auto [entity, light, transform] : world.view<PointLightComponent, Transform>().each()) {
        float dist = glm::length(transform.position() - position);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = entity;
        }
    }

    // Check spot lights
    for (auto [entity, light, transform] : world.view<SpotLightComponent, Transform>().each()) {
        float dist = glm::length(transform.position() - position);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = entity;
        }
    }

    return nearest;
}

// =============================================================================
// Visibility-Based Light Culling (Phase 5.2)
// =============================================================================
// Uses the ECS visibility system (Visible tag) for efficient light culling.
// Light entities with BoundingSphere participate in the same frustum culling
// as renderable entities.

// Statistics for light visibility culling
struct LightCullStats {
    size_t totalLights = 0;
    size_t visibleLights = 0;
    size_t culledLights = 0;
    size_t directionalLights = 0;  // Always visible

    [[nodiscard]] float visibilityRatio() const {
        return totalLights > 0 ?
            static_cast<float>(visibleLights) / static_cast<float>(totalLights) : 0.0f;
    }
};

// Get light visibility statistics
inline LightCullStats getLightCullStats(const World& world) {
    LightCullStats stats;

    // Count point lights
    for (auto [entity, light] : world.view<PointLightComponent>().each()) {
        if (!light.properties.enabled) continue;
        stats.totalLights++;
        if (world.has<Visible>(entity)) {
            stats.visibleLights++;
        } else {
            stats.culledLights++;
        }
    }

    // Count spot lights
    for (auto [entity, light] : world.view<SpotLightComponent>().each()) {
        if (!light.properties.enabled) continue;
        stats.totalLights++;
        if (world.has<Visible>(entity)) {
            stats.visibleLights++;
        } else {
            stats.culledLights++;
        }
    }

    // Count directional lights (always visible)
    for (auto [entity, light] : world.view<DirectionalLightComponent>().each()) {
        if (!light.properties.enabled) continue;
        stats.directionalLights++;
        stats.totalLights++;
        stats.visibleLights++;  // Directional lights are always visible
    }

    return stats;
}

// Collect only visible lights (using Visible tag from visibility system)
// This is more efficient than frustum testing each light individually
inline LightCollectionResult collectVisibleLights(const World& world,
                                                   const glm::vec3& cameraPos,
                                                   const glm::vec3& cameraFront) {
    LightCollectionResult result;

    // Collect visible point lights (must have Visible tag)
    for (auto [entity, pointLight, transform] :
         world.view<PointLightComponent, Transform, Visible>().each()) {

        if (!pointLight.properties.enabled) continue;

        CollectedLight collected;
        collected.entity = entity;
        collected.light = pointLightToLight(pointLight, transform);
        collected.distanceToCamera = glm::length(transform.position() - cameraPos);

        glm::vec3 toLight = glm::normalize(transform.position() - cameraPos);
        float angleFactor = glm::max(0.0f, glm::dot(toLight, cameraFront));
        angleFactor = 0.25f + 0.75f * angleFactor;
        collected.effectiveWeight = (pointLight.properties.priority * angleFactor) /
                                    (collected.distanceToCamera + 1.0f);

        result.pointLights.push_back(collected);
    }

    // Collect visible spot lights (must have Visible tag)
    for (auto [entity, spotLight, transform] :
         world.view<SpotLightComponent, Transform, Visible>().each()) {

        if (!spotLight.properties.enabled) continue;

        CollectedLight collected;
        collected.entity = entity;
        collected.light = spotLightToLight(spotLight, transform);
        collected.distanceToCamera = glm::length(transform.position() - cameraPos);

        glm::vec3 toLight = glm::normalize(transform.position() - cameraPos);
        float angleFactor = glm::max(0.0f, glm::dot(toLight, cameraFront));
        angleFactor = 0.25f + 0.75f * angleFactor;
        collected.effectiveWeight = (spotLight.properties.priority * angleFactor) /
                                    (collected.distanceToCamera + 1.0f);

        result.spotLights.push_back(collected);
    }

    // Directional lights are always included (no frustum culling needed)
    for (auto [entity, dirLight, transform] :
         world.view<DirectionalLightComponent, Transform>().each()) {

        if (!dirLight.properties.enabled) continue;

        CollectedLight collected;
        collected.entity = entity;
        collected.light = directionalLightToLight(dirLight, transform);
        collected.distanceToCamera = 0.0f;
        collected.effectiveWeight = dirLight.properties.priority;

        result.directionalLights.push_back(collected);
    }

    return result;
}

// Build GPU light buffer from visibility-culled lights
// Assumes updateVisibility() has been called to set Visible tags on light entities
// Returns number of lights written to buffer
inline uint32_t buildVisibleLightBuffer(const World& world,
                                         LightBuffer& buffer,
                                         const glm::vec3& cameraPos,
                                         const glm::vec3& cameraFront) {

    // Collect only visible lights
    LightCollectionResult collected = collectVisibleLights(world, cameraPos, cameraFront);

    // Merge all lights for sorting
    std::vector<CollectedLight> allLights;
    allLights.reserve(collected.totalCount());

    // Directional lights first
    for (auto& light : collected.directionalLights) {
        allLights.push_back(light);
    }

    // Point lights
    for (auto& light : collected.pointLights) {
        allLights.push_back(light);
    }

    // Spot lights
    for (auto& light : collected.spotLights) {
        allLights.push_back(light);
    }

    // Sort by effective weight
    std::sort(allLights.begin(), allLights.end(),
        [](const CollectedLight& a, const CollectedLight& b) {
            return a.effectiveWeight > b.effectiveWeight;
        });

    // Write to buffer
    uint32_t count = static_cast<uint32_t>(std::min(allLights.size(), static_cast<size_t>(MAX_LIGHTS)));
    buffer.lightCount = glm::uvec4(count, 0, 0, 0);

    for (uint32_t i = 0; i < count; i++) {
        buffer.lights[i] = allLights[i].light.toGPU();
    }

    // Zero unused slots
    for (uint32_t i = count; i < MAX_LIGHTS; i++) {
        buffer.lights[i] = GPULight{};
    }

    return count;
}

// =============================================================================
// Light SSBO Management (Phase 5.2)
// =============================================================================
// Functions for managing the GPU SSBO that holds visible light data.

// Light buffer for GPU upload - wraps LightBuffer with additional metadata
struct LightBufferData {
    LightBuffer buffer;
    uint32_t activeCount = 0;
    bool dirty = true;  // Needs re-upload to GPU

    void markDirty() { dirty = true; }
    void clearDirty() { dirty = false; }
};

// Update the light buffer from visible ECS lights
// Call this after updateVisibility() each frame
inline void updateLightBufferFromECS(const World& world,
                                      LightBufferData& bufferData,
                                      const glm::vec3& cameraPos,
                                      const glm::vec3& cameraFront) {
    bufferData.activeCount = buildVisibleLightBuffer(world, bufferData.buffer,
                                                      cameraPos, cameraFront);
    bufferData.markDirty();
}

// Alternative: Update with manual frustum culling (if visibility system not used for lights)
inline void updateLightBufferFromECSWithCulling(const World& world,
                                                 LightBufferData& bufferData,
                                                 const glm::vec3& cameraPos,
                                                 const glm::vec3& cameraFront,
                                                 const glm::mat4& viewProjMatrix,
                                                 float cullRadius = 100.0f) {
    bufferData.activeCount = buildLightBuffer(world, bufferData.buffer,
                                               cameraPos, cameraFront,
                                               viewProjMatrix, cullRadius);
    bufferData.markDirty();
}

} // namespace light
} // namespace ecs
