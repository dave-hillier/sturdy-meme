#pragma once

#include "interfaces/IPlayerControl.h"
#include <glm/glm.hpp>

class SceneManager;
class SceneBuilder;

/**
 * PlayerControlSubsystem - Implements IPlayerControl
 * Provides access to SceneBuilder for player-related controls.
 * Owns player render state (position/velocity/radius) for interaction systems.
 */
class PlayerControlSubsystem : public IPlayerControl {
public:
    explicit PlayerControlSubsystem(SceneManager& scene)
        : scene_(scene) {}

    SceneBuilder& getSceneBuilder() override;
    const SceneBuilder& getSceneBuilder() const override;

    // Player render state
    void setPlayerState(const glm::vec3& position, const glm::vec3& velocity, float radius) override;
    const glm::vec3& getPlayerPosition() const override { return playerPosition_; }
    const glm::vec3& getPlayerVelocity() const override { return playerVelocity_; }
    float getPlayerCapsuleRadius() const override { return playerCapsuleRadius_; }

private:
    SceneManager& scene_;

    // Player render state for interaction systems (grass displacement, snow, leaves, etc.)
    glm::vec3 playerPosition_ = glm::vec3(0.0f);
    glm::vec3 playerVelocity_ = glm::vec3(0.0f);
    float playerCapsuleRadius_ = 0.3f;  // Default capsule radius
};
