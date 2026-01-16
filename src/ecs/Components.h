#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entt.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include "PhysicsSystem.h"
#include "scene/RotationUtils.h"

// Core transform component - position, rotation, and scale for entities
// Supports both quaternion (full 3D) and yaw-only (Y-axis) rotation
// When used with Hierarchy component, this represents LOCAL transform (relative to parent)
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity quaternion
    glm::vec3 scale{1.0f};

    // ========================================================================
    // Factory methods
    // ========================================================================

    // Create with position only (identity rotation, unit scale)
    static Transform withPosition(const glm::vec3& pos) {
        Transform t;
        t.position = pos;
        return t;
    }

    // Create with position and yaw (degrees)
    static Transform withYaw(const glm::vec3& pos, float yawDegrees) {
        Transform t;
        t.position = pos;
        t.rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        return t;
    }

    // Create with position and quaternion rotation
    static Transform withRotation(const glm::vec3& pos, const glm::quat& rot) {
        Transform t;
        t.position = pos;
        t.rotation = rot;
        return t;
    }

    // Create with position, rotation, and scale
    static Transform withAll(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& s) {
        Transform t;
        t.position = pos;
        t.rotation = rot;
        t.scale = s;
        return t;
    }

    // Create with position and uniform scale
    static Transform withScale(const glm::vec3& pos, float uniformScale) {
        Transform t;
        t.position = pos;
        t.scale = glm::vec3(uniformScale);
        return t;
    }

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
    // Transform matrix (local)
    // ========================================================================

    // Get local transform matrix (T * R * S)
    glm::mat4 getMatrix() const {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * glm::mat4_cast(rotation);
        transform = glm::scale(transform, scale);
        return transform;
    }

    // Get local matrix with uniform scale override
    glm::mat4 getMatrixWithScale(float uniformScale) const {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * glm::mat4_cast(rotation);
        transform = glm::scale(transform, glm::vec3(uniformScale));
        return transform;
    }

    // ========================================================================
    // Scale accessors
    // ========================================================================

    void setScale(const glm::vec3& s) { scale = s; }
    void setScale(float uniformScale) { scale = glm::vec3(uniformScale); }
    const glm::vec3& getScale() const { return scale; }
};

// ============================================================================
// Hierarchy Component - Parent-child relationships for transforms
// ============================================================================

// Hierarchy component - defines parent-child relationships between entities
// When an entity has a Hierarchy, its Transform is treated as LOCAL (relative to parent)
// Entities without Hierarchy or with parent=null have Transform as WORLD transform
struct Hierarchy {
    entt::entity parent{entt::null};
    std::vector<entt::entity> children;

    // Depth in hierarchy (0 = root). Used for correct update ordering.
    uint32_t depth{0};

    // Factory for root entity (no parent)
    static Hierarchy root() {
        return Hierarchy{};
    }

    // Factory with parent
    static Hierarchy withParent(entt::entity p) {
        Hierarchy h;
        h.parent = p;
        return h;
    }

    bool hasParent() const { return parent != entt::null; }
    bool hasChildren() const { return !children.empty(); }
};

// World transform component - cached world-space matrix for entities with hierarchy
// Updated by the transform hierarchy system each frame
// For entities without Hierarchy, this is the same as Transform::getMatrix()
struct alignas(16) WorldTransform {
    glm::mat4 matrix{1.0f};

    // Decomposed world transform (extracted from matrix when needed)
    glm::vec3 getWorldPosition() const {
        return glm::vec3(matrix[3]);
    }

    glm::quat getWorldRotation() const {
        glm::vec3 scale;
        scale.x = glm::length(glm::vec3(matrix[0]));
        scale.y = glm::length(glm::vec3(matrix[1]));
        scale.z = glm::length(glm::vec3(matrix[2]));

        glm::mat3 rotMat(
            glm::vec3(matrix[0]) / scale.x,
            glm::vec3(matrix[1]) / scale.y,
            glm::vec3(matrix[2]) / scale.z
        );
        return glm::quat_cast(rotMat);
    }

    glm::vec3 getWorldScale() const {
        return glm::vec3(
            glm::length(glm::vec3(matrix[0])),
            glm::length(glm::vec3(matrix[1])),
            glm::length(glm::vec3(matrix[2]))
        );
    }

    // Direction vectors in world space
    glm::vec3 getForward() const {
        return glm::normalize(glm::vec3(matrix[2]));
    }

    glm::vec3 getRight() const {
        return glm::normalize(glm::vec3(matrix[0]));
    }

    glm::vec3 getUp() const {
        return glm::normalize(glm::vec3(matrix[1]));
    }
};

// Tag component to mark hierarchy as dirty (needs update)
struct HierarchyDirty {};

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
        float effectiveYaw = orientationLocked ? lockedYaw : transform.getYaw();
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
// Direction is derived from the entity's Transform rotation
// Default rotation points light downward (-Y axis)
struct SpotLight : LightBase {
    float innerConeAngle{30.0f};  // Degrees
    float outerConeAngle{45.0f};  // Degrees

    // Get light direction from transform
    static glm::vec3 getDirection(const Transform& transform) {
        return RotationUtils::directionFromRotation(transform.rotation);
    }

    // Create rotation quaternion to point light in a specific direction
    static glm::quat rotationFromDirection(const glm::vec3& direction) {
        return RotationUtils::rotationFromDirection(direction);
    }
};

// Tag for enabled lights (removed when light is disabled)
struct LightEnabled {};

// Shadow caster tag - lights that cast shadows
struct ShadowCaster {
    int32_t shadowMapIndex{-1};  // Index in shadow map array
};

// Light attachment - attaches light to another entity's transform
// DEPRECATED: Use Hierarchy component instead. Create light as child entity with
// offset as local position. The transform hierarchy system will compute world position.
// Example: world.createAttachedLight(parent, offset, color, intensity, radius);
struct [[deprecated("Use Hierarchy component instead")]] LightAttachment {
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
// alignas(16) required for SIMD operations on glm::mat4
struct alignas(16) ModelMatrix {
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
