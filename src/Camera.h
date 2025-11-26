#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    Camera();

    void setAspectRatio(float aspect);

    void moveForward(float delta);
    void moveRight(float delta);
    void moveUp(float delta);
    void rotatePitch(float delta);
    void rotateYaw(float delta);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::vec3 getPosition() const { return position; }
    float getNearPlane() const { return nearPlane; }
    float getFarPlane() const { return farPlane; }

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
};
