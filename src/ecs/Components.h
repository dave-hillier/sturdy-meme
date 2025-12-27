#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>

namespace ecs {

// Transform component - position and rotation in world space
struct Transform {
    glm::vec3 position{0.0f};
    float yaw{0.0f};  // Horizontal rotation in degrees

    glm::vec3 getForward() const {
        float radYaw = glm::radians(yaw);
        return glm::vec3(sin(radYaw), 0.0f, cos(radYaw));
    }

    glm::vec3 getRight() const {
        float radYaw = glm::radians(yaw + 90.0f);
        return glm::vec3(sin(radYaw), 0.0f, cos(radYaw));
    }

    void normalizeYaw() {
        while (yaw > 360.0f) yaw -= 360.0f;
        while (yaw < 0.0f) yaw += 360.0f;
    }
};

// Velocity component - linear velocity for physics
struct Velocity {
    glm::vec3 linear{0.0f};
};

// Gravity component - marks entity as affected by gravity
struct Gravity {
    float acceleration{9.8f};
    float groundLevel{0.0f};  // Default ground level (can be overridden by terrain)
    bool useTerrainHeight{true};  // Whether to query terrain for ground level
};

// Grounded component - present when entity is on ground
struct Grounded {};

// Capsule collider for character collision
struct CapsuleCollider {
    float height{1.8f};
    float radius{0.3f};
};

// Player controller input state
struct PlayerController {
    float moveForward{0.0f};   // -1 to 1
    float moveRight{0.0f};     // -1 to 1
    float yawDelta{0.0f};      // Rotation input
    bool jumpRequested{false};
    float jumpVelocity{5.0f};

    // Orientation lock (strafe mode)
    bool orientationLocked{false};
    float lockedYaw{0.0f};
};

// Camera focus point for third-person camera
struct CameraTarget {
    float eyeHeightRatio{0.85f};  // Height ratio for eye level

    glm::vec3 getFocusPoint(const Transform& transform, float capsuleHeight) const {
        return transform.position + glm::vec3(0.0f, capsuleHeight * eyeHeightRatio, 0.0f);
    }
};

// Reference to a renderable object in SceneBuilder
struct RenderableRef {
    size_t sceneIndex{0};
    bool visible{true};
};

// Reference to a physics body
struct PhysicsBodyRef {
    uint32_t bodyId{0xFFFFFFFF};  // INVALID_BODY_ID
    bool syncsFromPhysics{true};   // If true, transform is updated from physics
};

// Tag component for player entity
struct PlayerTag {};

// Model matrix cache - computed from transform for rendering
struct ModelMatrix {
    glm::mat4 matrix{1.0f};

    void update(const Transform& transform, const CapsuleCollider& capsule,
                bool orientationLocked, float lockedYaw) {
        matrix = glm::mat4(1.0f);
        // Capsule center is at ground level, offset up by half height
        matrix = glm::translate(matrix, transform.position +
                               glm::vec3(0.0f, capsule.height * 0.5f, 0.0f));
        float effectiveYaw = orientationLocked ? lockedYaw : transform.yaw;
        matrix = glm::rotate(matrix, glm::radians(effectiveYaw), glm::vec3(0.0f, 1.0f, 0.0f));
    }
};

} // namespace ecs
