#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include "PhysicsSystem.h"

// Core transform component - position and rotation for entities
// Supports both quaternion (full 3D) and yaw-only (Y-axis) rotation
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity quaternion

    // ========================================================================
    // Quaternion-based rotation (preferred for full 3D rotation)
    // ========================================================================

    void setRotation(const glm::quat& rot) { rotation = rot; }
    const glm::quat& getRotation() const { return rotation; }

    // ========================================================================
    // Yaw-based rotation (backward compatibility for Y-axis only rotation)
    // ========================================================================

    // Get yaw in degrees (extracted from quaternion)
    float getYaw() const {
        glm::vec3 euler = glm::eulerAngles(rotation);
        return glm::degrees(euler.y);
    }

    // Set yaw in degrees (creates Y-axis rotation quaternion)
    void setYaw(float yawDegrees) {
        rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void normalizeYaw() {
        // Quaternions are already normalized, but we can re-normalize if needed
        rotation = glm::normalize(rotation);
    }

    // ========================================================================
    // Direction vectors
    // ========================================================================

    glm::vec3 getForward() const {
        return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    }

    glm::vec3 getRight() const {
        return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    }

    glm::vec3 getUp() const {
        return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    }

    // ========================================================================
    // Transform matrix
    // ========================================================================

    glm::mat4 getMatrix() const {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * glm::mat4_cast(rotation);
        return transform;
    }

    glm::mat4 getMatrixWithScale(float scale) const {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * glm::mat4_cast(rotation);
        transform = glm::scale(transform, glm::vec3(scale));
        return transform;
    }
};

// Velocity for physics-driven entities
struct Velocity {
    glm::vec3 linear{0.0f};
};

// Physics body reference - links entity to Jolt physics body
struct PhysicsBody {
    PhysicsBodyID id = INVALID_BODY_ID;
};

// Renderable reference - links entity to scene object index (during migration)
// Eventually this could hold mesh/texture references directly
struct RenderableRef {
    size_t sceneIndex = 0;
};

// Tag: marks entity as the player
struct PlayerTag {};

// Tag: marks entity as grounded (on floor/terrain)
struct Grounded {};

// Player-specific components
struct PlayerMovement {
    static constexpr float CAPSULE_HEIGHT = 1.8f;
    static constexpr float CAPSULE_RADIUS = 0.3f;
    bool orientationLocked = false;
    float lockedYaw = 0.0f;

    glm::vec3 getFocusPoint(const glm::vec3& position) const {
        return position + glm::vec3(0.0f, CAPSULE_HEIGHT * 0.85f, 0.0f);
    }

    glm::mat4 getModelMatrix(const Transform& transform) const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, transform.position + glm::vec3(0.0f, CAPSULE_HEIGHT * 0.5f, 0.0f));
        float effectiveYaw = orientationLocked ? lockedYaw : transform.yaw;
        model = glm::rotate(model, glm::radians(effectiveYaw), glm::vec3(0.0f, 1.0f, 0.0f));
        return model;
    }
};

// Dynamic scene object that has physics simulation
struct DynamicObject {};

// Emissive light source that follows an entity
struct EmissiveLight {
    glm::vec3 color{1.0f};
    float intensity{1.0f};
};

// ============================================================================
// Light Components (Extended)
// ============================================================================

// Base light component - common properties for all light types
struct LightBase {
    glm::vec3 color{1.0f};
    float intensity{1.0f};
    float radius{10.0f};  // Falloff radius
    bool castsShadows{true};
    float priority{1.0f};  // Higher = more important (less likely to be culled)
};

// Point light - omnidirectional light source
struct PointLight : LightBase {
    // Point lights only need the base properties
};

// Spot light - directional cone light
struct SpotLight : LightBase {
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float innerConeAngle{30.0f};  // Degrees
    float outerConeAngle{45.0f};  // Degrees
};

// Tag for enabled lights (removed when light is disabled)
struct LightEnabled {};

// Shadow caster tag - lights that cast shadows
struct ShadowCaster {
    int32_t shadowMapIndex{-1};  // Index in shadow map array
};

// Light attachment - attaches light to another entity's transform
struct LightAttachment {
    entt::entity parent{entt::null};
    glm::vec3 offset{0.0f};  // Offset from parent transform
};

// ============================================================================
// Dynamic Object Components (Extended)
// ============================================================================

// Tag for objects that sync transform from physics
struct PhysicsDriven {};

// Tag for objects that are rendered (links to scene renderables)
struct SceneRenderable {
    size_t renderableIndex{0};
};

// Bounding sphere for culling
struct BoundingSphere {
    float radius{1.0f};
};

// Model matrix cache - computed from transform for rendering
struct ModelMatrix {
    glm::mat4 matrix{1.0f};
};

// ============================================================================
// NPC/AI Components
// ============================================================================

// Tag for NPC entities
struct NPCTag {};

// Simple AI state component
struct AIState {
    enum class State {
        Idle,
        Patrol,
        Chase,
        Flee
    };
    State current{State::Idle};
    float stateTimer{0.0f};
};

// Patrol waypoint data
struct PatrolPath {
    std::vector<glm::vec3> waypoints;
    size_t currentWaypoint{0};
    bool loop{true};
    float waypointRadius{0.5f};  // How close to get before moving to next
};

// Movement speed settings for characters/NPCs
struct MovementSettings {
    float walkSpeed{2.0f};
    float runSpeed{5.0f};
    float turnSpeed{180.0f};  // Degrees per second
};

// Health component for damageable entities
struct Health {
    float current{100.0f};
    float maximum{100.0f};
    bool invulnerable{false};
};

// Name/identifier component
struct NameTag {
    std::string name;
};
