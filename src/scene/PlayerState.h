#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// Core transform component - position and rotation
// Supports both quaternion (full 3D) and yaw-only (Y-axis) rotation
struct PlayerTransform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity quaternion

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
        t.rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        return t;
    }

    // Get yaw in degrees (extracted from quaternion)
    float getYaw() const {
        glm::vec3 euler = glm::eulerAngles(rotation);
        return glm::degrees(euler.y);
    }

    // Set yaw in degrees (creates Y-axis rotation quaternion)
    void setYaw(float yawDegrees) {
        rotation = glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::vec3 getForward() const {
        return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    }

    glm::mat4 getMatrix() const {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * glm::mat4_cast(rotation);
        return transform;
    }
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
