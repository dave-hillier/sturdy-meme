#pragma once

#include "interfaces/ISceneControl.h"

class SceneManager;
class VulkanContext;
class SceneBuilder;

/**
 * SceneControlSubsystem - Implements ISceneControl
 * Provides access to SceneBuilder and viewport dimensions.
 */
class SceneControlSubsystem : public ISceneControl {
public:
    SceneControlSubsystem(SceneManager& scene, VulkanContext& vulkanContext)
        : scene_(scene)
        , vulkanContext_(vulkanContext) {}

    SceneBuilder& getSceneBuilder() override;
    uint32_t getWidth() const override;
    uint32_t getHeight() const override;

private:
    SceneManager& scene_;
    VulkanContext& vulkanContext_;
};
