#pragma once

#include <glm/glm.hpp>

class SceneBuilder;

/**
 * Interface for player-related controls.
 * Used by GuiPlayerTab to control player-specific settings.
 * Also provides player render state (position/velocity/radius) for systems
 * that need to interact with the player (grass displacement, snow footprints, etc.)
 */
class IPlayerControl {
public:
    virtual ~IPlayerControl() = default;

    // Scene builder access (for player cape)
    virtual SceneBuilder& getSceneBuilder() = 0;
    virtual const SceneBuilder& getSceneBuilder() const = 0;

    // Player render state for interaction systems
    virtual void setPlayerState(const glm::vec3& position, const glm::vec3& velocity, float radius) = 0;
    virtual const glm::vec3& getPlayerPosition() const = 0;
    virtual const glm::vec3& getPlayerVelocity() const = 0;
    virtual float getPlayerCapsuleRadius() const = 0;
};
