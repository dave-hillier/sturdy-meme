#include "PlayerControlSubsystem.h"
#include "SceneManager.h"

SceneBuilder& PlayerControlSubsystem::getSceneBuilder() {
    return scene_.getSceneBuilder();
}

const SceneBuilder& PlayerControlSubsystem::getSceneBuilder() const {
    return scene_.getSceneBuilder();
}
