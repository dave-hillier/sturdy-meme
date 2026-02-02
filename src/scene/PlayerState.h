#pragma once

#include "Transform.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

/**
 * PlayerTransform - Player-specific transform with degree-based yaw helpers
 *
 * Extends Transform with convenience methods for player-facing APIs
 * that use degrees rather than radians.
 */
struct PlayerTransform : Transform {
    using Transform::Transform;  // Inherit constructors

    // Create with position only (identity rotation)
    static PlayerTransform withPosition(const glm::vec3& pos) {
        PlayerTransform t;
        t.position = pos;
        return t;
    }

    // Create with position and yaw (degrees)
    static PlayerTransform withYaw(const glm::vec3& pos, float yawDegrees) {
        PlayerTransform t;
        t.position = pos;
        t.rotation = yRotation(glm::radians(yawDegrees));
        return t;
    }

    // Get yaw in degrees (extracted from quaternion)
    // Uses the proper formula that handles general quaternions (not just pure Y-rotations)
    float getYaw() const {
        // For a general quaternion, yaw (rotation around Y) is:
        // yaw = atan2(2*(w*y + x*z), 1 - 2*(x*x + y*y))
        // This formula handles any quaternion orientation correctly
        float yaw = std::atan2(
            2.0f * (rotation.w * rotation.y + rotation.x * rotation.z),
            1.0f - 2.0f * (rotation.x * rotation.x + rotation.y * rotation.y)
        );
        return glm::degrees(yaw);
    }

    // Set yaw in degrees
    void setYaw(float yawDegrees) {
        rotation = yRotation(glm::radians(yawDegrees));
    }

    // Alias for Transform::forward() - kept for API compatibility
    glm::vec3 getForward() const { return forward(); }

    // Alias for Transform::toMatrix() - kept for API compatibility
    glm::mat4 getMatrix() const { return toMatrix(); }
};

// Player-specific movement settings and state
struct PlayerMovement {
    static constexpr float CAPSULE_HEIGHT = 1.8f;
    static constexpr float CAPSULE_RADIUS = 0.3f;
    bool orientationLocked = false;
    float lockedYaw = 0.0f;

    glm::vec3 getFocusPoint(const glm::vec3& position) const {
        return position + glm::vec3(0.0f, CAPSULE_HEIGHT * 0.85f, 0.0f);
    }

    glm::mat4 getModelMatrix(const PlayerTransform& transform) const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, transform.position + glm::vec3(0.0f, CAPSULE_HEIGHT * 0.5f, 0.0f));
        float effectiveYaw = orientationLocked ? lockedYaw : transform.getYaw();
        model = glm::rotate(model, glm::radians(effectiveYaw), glm::vec3(0.0f, 1.0f, 0.0f));
        return model;
    }
};

// Simple player state container
struct PlayerState {
    PlayerTransform transform;
    PlayerMovement movement;
    bool grounded = false;
};
