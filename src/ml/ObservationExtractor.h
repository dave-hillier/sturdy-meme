#pragma once

#include "CharacterConfig.h"
#include "Tensor.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <array>

struct Skeleton;
class CharacterController;

namespace physics {
    class RagdollInstance;
}

namespace ml {

// Extracts per-frame observations from the engine's Skeleton and CharacterController.
//
// Per-frame observation vector layout (AMP-style):
//   [0]        root height (1)
//   [1..6]     root rotation, heading-invariant 6D representation (6)
//   [7..9]     local root velocity in heading frame (3)
//   [10..12]   local root angular velocity (3)
//   [13..13+N] DOF positions — joint angles for each mapped DOF (N)
//   [13+N..13+2N] DOF velocities — angular velocity per DOF (N)
//   [13+2N..]  key body positions in root-relative heading frame (K*3)
//
// The extractor maintains a ring buffer of recent frames for temporal stacking
// (used by the encoder and discriminator).
class ObservationExtractor {
public:
    static constexpr int MAX_OBS_HISTORY = 16;

    ObservationExtractor() = default;
    explicit ObservationExtractor(const CharacterConfig& config);

    // Extract one frame of observations from the current character state.
    // Call once per simulation step.
    void extractFrame(const Skeleton& skeleton,
                      const CharacterController& controller,
                      float deltaTime);

    // Extract one frame of observations from a ragdoll instance.
    // Uses physics body positions/velocities instead of CharacterController.
    // Provides exact angular velocities from physics rather than finite differences.
    void extractFrameFromRagdoll(const Skeleton& skeleton,
                                  const physics::RagdollInstance& ragdoll,
                                  float deltaTime);

    // Get the most recent single-frame observation as a Tensor.
    Tensor getCurrentObs() const;

    // Get temporally stacked observations (for policy input).
    // Returns a flat tensor of size: numSteps * observationDim
    Tensor getStackedObs(int numSteps) const;

    // Get stacked observations for the encoder (wider window).
    Tensor getEncoderObs() const;

    // Get stacked observations for the policy.
    Tensor getPolicyObs() const;

    // Get the observation dimension per frame.
    int frameDim() const { return config_.observationDim; }

    // Reset history (call on teleport/spawn).
    void reset();

    // Get config
    const CharacterConfig& config() const { return config_; }

private:
    CharacterConfig config_;

    // Ring buffer of observation frames
    std::array<std::vector<float>, MAX_OBS_HISTORY> history_;
    int historyIndex_ = 0;
    int historyCount_ = 0;

    // Previous frame state for velocity computation
    std::vector<float> prevDOFPositions_;
    glm::quat prevRootRotation_{1.0f, 0.0f, 0.0f, 0.0f};
    bool hasPreviousFrame_ = false;

    // Helpers
    void extractRootFeatures(const Skeleton& skeleton,
                             const CharacterController& controller,
                             float deltaTime,
                             std::vector<float>& obs);

    void extractDOFFeatures(const Skeleton& skeleton,
                            float deltaTime,
                            std::vector<float>& obs);

    void extractKeyBodyFeatures(const Skeleton& skeleton,
                                std::vector<float>& obs);

    // Ragdoll-specific feature extractors
    void extractRootFeaturesFromRagdoll(const Skeleton& skeleton,
                                         const physics::RagdollInstance& ragdoll,
                                         float deltaTime,
                                         std::vector<float>& obs);

    void extractDOFFeaturesFromRagdoll(const Skeleton& skeleton,
                                        const physics::RagdollInstance& ragdoll,
                                        float deltaTime,
                                        std::vector<float>& obs);

    // Convert quaternion to heading-invariant 6D representation
    // (tan-normalized: first two columns of rotation matrix)
    static void quatToTanNorm6D(const glm::quat& q, float out[6]);

    // Get the heading (yaw) angle from a quaternion
    static float getHeadingAngle(const glm::quat& q);

    // Remove heading from a quaternion (keep only pitch/roll)
    static glm::quat removeHeading(const glm::quat& q);

    // Decompose joint local transform into Euler angles (XYZ order)
    static glm::vec3 matrixToEulerXYZ(const glm::mat4& m);
};

} // namespace ml
