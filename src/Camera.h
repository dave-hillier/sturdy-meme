#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera();

    void setAspectRatio(float aspect);

    // Free camera movement
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
    float getDistance() const { return thirdPersonDistance; }

    // Update third-person camera position based on target
    void updateThirdPerson();

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::vec3 getPosition() const { return position; }
    float getNearPlane() const { return nearPlane; }
    float getFarPlane() const { return farPlane; }

    // Get the yaw for player rotation in third-person mode
    float getYaw() const { return yaw; }

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
};
