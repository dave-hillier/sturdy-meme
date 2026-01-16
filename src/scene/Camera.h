#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <functional>
#include <entt/entt.hpp>
#include "SceneNode.h"

// Forward declaration for ECS registry
namespace ecs { class World; }

/**
 * Camera - First-person and third-person camera with SceneNode and ECS integration
 *
 * Supports following targets via:
 * - Direct position (setThirdPersonTarget)
 * - SceneNode pointer (legacy: setThirdPersonTargetNode)
 * - ECS entity with Transform/WorldTransform (preferred: setThirdPersonTargetEntity)
 *
 * Supports two modes:
 * - Free camera: Direct position/rotation control
 * - Third-person: Orbits around a target with smoothing
 */
class Camera {
public:
    Camera();

    void setAspectRatio(float aspect);

    // ========================================================================
    // Free camera movement
    // ========================================================================
    void setPosition(const glm::vec3& pos);
    void setYaw(float newYaw);
    void setPitch(float newPitch);
    void setRotation(float yaw, float pitch);
    void moveForward(float delta);
    void moveRight(float delta);
    void moveUp(float delta);
    void rotatePitch(float delta);
    void rotateYaw(float delta);

    // ========================================================================
    // Third-person camera controls
    // ========================================================================
    void setThirdPersonTarget(const glm::vec3& target);
    void setThirdPersonTargetNode(SceneNode* targetNode);  // Legacy: Follow a scene node
    void setThirdPersonTargetEntity(entt::entity target, entt::registry* registry);  // Preferred: Follow ECS entity
    void clearThirdPersonTargetEntity();  // Clear entity target
    void orbitYaw(float delta);
    void orbitPitch(float delta);
    void adjustDistance(float delta);
    void setDistance(float dist);
    float getDistance() const { return smoothedDistance_; }

    // Update third-person camera position based on target (with smoothing)
    void updateThirdPerson(float deltaTime);

    // Snap smoothed values to targets (call on mode switch)
    void resetSmoothing();

    // Initialize third-person camera from current free camera position
    void initializeThirdPersonFromCurrentPosition(const glm::vec3& target);

    // ========================================================================
    // Matrices and accessors
    // ========================================================================
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;

    glm::vec3 getPosition() const { return position_; }
    glm::vec3 getFront() const { return front_; }
    glm::vec3 getRight() const { return right_; }
    glm::vec3 getUp() const { return up_; }

    float getNearPlane() const { return nearPlane_; }
    float getFarPlane() const { return farPlane_; }
    float getYaw() const { return yaw_; }
    float getPitch() const { return pitch_; }
    float getFov() const { return currentFov_; }

    // Get rotation as quaternion (derived from yaw/pitch)
    glm::quat getRotation() const;

    // Get the third-person target position (for occlusion detection)
    glm::vec3 getThirdPersonTarget() const { return smoothedTarget_; }

    // ========================================================================
    // Dynamic FOV
    // ========================================================================
    void setTargetFov(float fov);

    // ========================================================================
    // Camera collision
    // ========================================================================
    void applyCollisionDistance(float collisionDistance);
    float getSmoothedDistance() const { return smoothedDistance_; }

    // ========================================================================
    // Scene graph integration
    // ========================================================================

    // Get the camera's scene node for parent-child relationships
    // Example: Attach camera to a vehicle node
    SceneNode& getNode() { return node_; }
    const SceneNode& getNode() const { return node_; }

    // Sync node transform from camera state (call after camera updates)
    void syncNodeTransform();

private:
    void updateVectors();

    // Scene node for hierarchy (optional parent attachment)
    SceneNode node_;

    // Core transform state
    glm::vec3 position_;
    glm::vec3 front_;
    glm::vec3 up_;
    glm::vec3 right_;
    glm::vec3 worldUp_;

    // Euler angles (degrees) - primary rotation for FPS-style control
    float yaw_;
    float pitch_;

    // Projection parameters
    float fov_;
    float aspectRatio_;
    float nearPlane_;
    float farPlane_;

    // Third-person camera settings
    glm::vec3 thirdPersonTarget_;
    std::optional<std::reference_wrapper<SceneNode>> thirdPersonTargetNode_;  // Legacy: follow a scene node
    entt::entity thirdPersonTargetEntity_{entt::null};  // Preferred: follow ECS entity
    entt::registry* targetEntityRegistry_{nullptr};     // Registry for entity lookup
    float thirdPersonDistance_;
    float thirdPersonMinDistance_;
    float thirdPersonMaxDistance_;

    // Smoothing state - interpolated values
    glm::vec3 smoothedTarget_;
    float smoothedYaw_;
    float smoothedPitch_;
    float smoothedDistance_;

    // Smoothing targets - input-driven
    float targetYaw_;
    float targetPitch_;
    float targetDistance_;

    // Smoothing speeds
    static constexpr float kPositionSmoothSpeed = 8.0f;
    static constexpr float kRotationSmoothSpeed = 12.0f;
    static constexpr float kDistanceSmoothSpeed = 6.0f;

    // Dynamic FOV
    float baseFov_;
    float currentFov_;
    float targetFov_;
    static constexpr float kFovSmoothSpeed = 4.0f;

    // Camera collision
    float collisionAdjustedDistance_ = -1.0f;  // -1 means no collision adjustment
};
