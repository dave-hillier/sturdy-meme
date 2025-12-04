#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera();

    void setAspectRatio(float aspect);

    // Free camera movement
    void setPosition(const glm::vec3& pos);
    void setYaw(float newYaw);
    void setPitch(float newPitch);
    void moveForward(float delta);
    void moveRight(float delta);
    void moveUp(float delta);
    void rotatePitch(float delta);
    void rotateYaw(float delta);

    // Third-person camera controls
    void setThirdPersonTarget(const glm::vec3& target);
    void orbitYaw(float delta);
    void orbitPitch(float delta);
    void adjustDistance(float delta);
    void setDistance(float dist);
    float getDistance() const { return smoothedDistance; }

    // Update third-person camera position based on target (with smoothing)
    void updateThirdPerson(float deltaTime);

    // Snap smoothed values to targets (call on mode switch)
    void resetSmoothing();

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::vec3 getPosition() const { return position; }
    glm::vec3 getFront() const { return front; }
    float getNearPlane() const { return nearPlane; }
    float getFarPlane() const { return farPlane; }

    // Get the yaw for player rotation in third-person mode
    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }

    // Get current FOV (for dynamic FOV feature)
    float getFov() const { return currentFov; }

    // Set target FOV for dynamic FOV
    void setTargetFov(float fov);

    // Get the third-person target position (for occlusion detection)
    glm::vec3 getThirdPersonTarget() const { return smoothedTarget; }

    // Camera collision - adjust distance to avoid clipping through geometry
    void applyCollisionDistance(float collisionDistance);
    float getSmoothedDistance() const { return smoothedDistance; }

private:
    void updateVectors();

    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    float yaw;
    float pitch;
    float fov;
    float aspectRatio;
    float nearPlane;
    float farPlane;

    // Third-person camera settings
    glm::vec3 thirdPersonTarget;
    float thirdPersonDistance;
    float thirdPersonMinDistance;
    float thirdPersonMaxDistance;

    // Smoothing state - interpolated values
    glm::vec3 smoothedTarget;
    float smoothedYaw;
    float smoothedPitch;
    float smoothedDistance;

    // Smoothing targets - input-driven
    float targetYaw;
    float targetPitch;
    float targetDistance;

    // Smoothing speeds
    static constexpr float positionSmoothSpeed = 8.0f;
    static constexpr float rotationSmoothSpeed = 12.0f;
    static constexpr float distanceSmoothSpeed = 6.0f;

    // Dynamic FOV
    float baseFov;
    float currentFov;
    float targetFov;
    static constexpr float fovSmoothSpeed = 4.0f;

    // Camera collision
    float collisionAdjustedDistance = -1.0f;  // -1 means no collision adjustment
};
