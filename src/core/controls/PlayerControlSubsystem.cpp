#include "PlayerControlSubsystem.h"
#include "SceneManager.h"

SceneBuilder& PlayerControlSubsystem::getSceneBuilder() {
    return scene_.getSceneBuilder();
}

const SceneBuilder& PlayerControlSubsystem::getSceneBuilder() const {
    return scene_.getSceneBuilder();
}

void PlayerControlSubsystem::setPlayerState(const glm::vec3& position, const glm::vec3& velocity, float radius) {
    playerPosition_ = position;
    playerVelocity_ = velocity;
    playerCapsuleRadius_ = radius;
}
