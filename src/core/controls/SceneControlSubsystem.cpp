#include "SceneControlSubsystem.h"
#include "SceneManager.h"
#include "VulkanContext.h"

SceneBuilder& SceneControlSubsystem::getSceneBuilder() {
    return scene_.getSceneBuilder();
}

uint32_t SceneControlSubsystem::getWidth() const {
    return vulkanContext_.getWidth();
}

uint32_t SceneControlSubsystem::getHeight() const {
    return vulkanContext_.getHeight();
}
