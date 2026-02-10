#pragma once

#include <cstdint>
#include <glm/glm.hpp>

class IPlayerControl;
class Camera;

// Character facing mode - determines what direction the character faces
enum class FacingMode {
    FollowMovement,  // Character turns to face movement direction (default)
    FollowCamera,    // Character faces camera direction (strafe mode)
    FollowTarget     // Character faces a target position (lock-on)
};

// Player settings for GUI control
struct PlayerSettings {
    // Cape
    bool capeEnabled = false;
    bool showCapeColliders = false;

    // Weapons
    bool showSword = true;        // Show sword in right hand
    bool showShield = true;       // Show shield on left arm

    // Weapons debug
    bool showWeaponAxes = false;  // Show RGB axis indicators on hand bones

    // LOD debug
    bool showLODOverlay = false;  // Show LOD level as screen text
    bool forceLODLevel = false;   // Override automatic LOD selection
    uint32_t forcedLOD = 0;       // Forced LOD level when override is enabled

    // Motion Matching debug
    bool motionMatchingEnabled = true;   // Use motion matching instead of state machine
    bool showMotionMatchingTrajectory = false;  // Show predicted and matched trajectory
    bool showMotionMatchingFeatures = false;   // Show feature bone positions
    bool showMotionMatchingStats = false;      // Show match cost statistics

    // Character facing control
    FacingMode facingMode = FacingMode::FollowMovement;  // What direction character faces
    glm::vec3 targetPosition{0.0f};   // Target position for FollowTarget mode
    bool hasTarget = false;           // Whether a target has been placed
    bool thirdPersonCamera = false;   // Third-person camera mode (for strafe testing)
};

namespace GuiPlayerTab {
    void render(IPlayerControl& playerControl, PlayerSettings& settings);

    // Render motion matching debug overlay (trajectory visualization)
    void renderMotionMatchingOverlay(IPlayerControl& playerControl, const Camera& camera,
                                      const PlayerSettings& settings);
}
