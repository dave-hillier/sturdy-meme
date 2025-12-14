#pragma once

#include <glm/glm.hpp>

class Renderer;
class Camera;

// IK debug settings for GUI control
struct IKDebugSettings {
    bool showSkeleton = false;
    bool showIKTargets = false;
    bool showFootPlacement = false;

    // IK feature enables
    bool lookAtEnabled = false;
    bool footPlacementEnabled = true;
    bool straddleEnabled = false;

    // Look-at target mode
    enum class LookAtMode { Fixed, Camera, Mouse };
    LookAtMode lookAtMode = LookAtMode::Camera;
    glm::vec3 fixedLookAtTarget = glm::vec3(0, 1.5f, 5.0f);

    // Foot placement
    float groundOffset = 0.0f;
};

namespace GuiIKTab {
    void render(Renderer& renderer, const Camera& camera, IKDebugSettings& settings);
    void renderSkeletonOverlay(Renderer& renderer, const Camera& camera, const IKDebugSettings& settings, bool showCapeColliders);
}
