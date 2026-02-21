#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace training {

// A single frame of reference motion data used to initialize/reset a character.
// Captures the root transform and per-joint state needed to place a ragdoll
// into a known pose from a motion clip.
struct MotionFrame {
    glm::vec3 rootPosition{0.0f, 1.0f, 0.0f};
    glm::quat rootRotation{1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<glm::quat> jointRotations;  // Local rotations per joint
    std::vector<glm::vec3> jointPositions;   // Global positions per joint (for key body init)
};

} // namespace training
