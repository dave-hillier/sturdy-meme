#pragma once

#include "interfaces/IPlayerControl.h"

class SceneManager;
class SceneBuilder;

/**
 * PlayerControlSubsystem - Implements IPlayerControl
 * Provides access to SceneBuilder for player-related controls.
 */
class PlayerControlSubsystem : public IPlayerControl {
public:
    explicit PlayerControlSubsystem(SceneManager& scene)
        : scene_(scene) {}

    SceneBuilder& getSceneBuilder() override;
    const SceneBuilder& getSceneBuilder() const override;

private:
    SceneManager& scene_;
};
