#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>

struct AnimationClip;
struct Skeleton;
struct FootPlacementIK;  // Forward declared to avoid circular include with IKSolver.h

// Foot contact event names
namespace FootEvents {
    constexpr const char* LEFT_FOOT_DOWN = "left_foot_down";
    constexpr const char* LEFT_FOOT_UP = "left_foot_up";
    constexpr const char* RIGHT_FOOT_DOWN = "right_foot_down";
    constexpr const char* RIGHT_FOOT_UP = "right_foot_up";
}

// Phase of foot during locomotion cycle
enum class FootPhase {
    Swing,      // Foot in air, moving forward
    Contact,    // Foot just touched ground (heel strike)
    Stance,     // Foot planted, supporting weight
    PushOff     // Foot lifting off (toe push)
};

// Data for a single foot's phase tracking
struct FootPhaseData {
    FootPhase phase = FootPhase::Stance;
    float phaseProgress = 0.0f;     // 0-1 progress within current phase
    float lastContactTime = 0.0f;   // Animation time of last contact
    float lastLiftTime = 0.0f;      // Animation time of last lift
    bool isGrounded = true;         // Currently on ground

    // Contact prediction (during swing phase)
    glm::vec3 predictedContactPos = glm::vec3(0.0f);
    float predictedContactTime = 0.0f;

    // Locked position (during stance phase)
    glm::vec3 lockedPosition = glm::vec3(0.0f);
    bool hasLockedPosition = false;
};

// Detected foot contact timing from animation analysis
struct FootContactTiming {
    float contactTime = 0.0f;   // When foot touches ground (normalized 0-1)
    float liftTime = 0.0f;      // When foot leaves ground (normalized 0-1)
    float stanceDuration = 0.0f; // How long foot is planted
    float swingDuration = 0.0f;  // How long foot is in air
};

// Analyzes animations and tracks foot phases during playback
class FootPhaseTracker {
public:
    FootPhaseTracker() = default;

    // Analyze an animation clip to detect foot contact timings
    // Returns true if contacts were successfully detected
    bool analyzeAnimation(const AnimationClip& clip, const Skeleton& skeleton,
                         const std::string& leftFootBone, const std::string& rightFootBone);

    // Update foot phases based on current animation time
    void update(float normalizedTime, float deltaTime,
                const glm::vec3& leftFootWorldPos, const glm::vec3& rightFootWorldPos,
                const glm::mat4& characterTransform);

    // Get current phase data for each foot
    const FootPhaseData& getLeftFoot() const { return leftFoot_; }
    const FootPhaseData& getRightFoot() const { return rightFoot_; }

    // Get detected contact timings (from animation analysis)
    const FootContactTiming& getLeftTiming() const { return leftTiming_; }
    const FootContactTiming& getRightTiming() const { return rightTiming_; }

    // Check if animation has been analyzed
    bool hasTimingData() const { return hasAnalyzedAnimation_; }

    // Get IK weight for foot based on phase (0 = full animation, 1 = full IK)
    float getIKWeight(bool isLeftFoot) const;

    // Get lock blend for foot based on phase (0 = no lock, 1 = full lock)
    float getLockBlend(bool isLeftFoot) const;

    // Synchronise tracker phase data into the IK struct (single source of truth).
    // Eliminates duplicate state between FootPhaseData and FootPlacementIK (bug #17).
    void applyToFootIK(bool isLeftFoot, FootPlacementIK& foot) const;

    // Reset tracker state
    void reset();

    // Configuration
    void setContactThreshold(float threshold) { contactThreshold_ = threshold; }
    void setContactBlendDuration(float duration) { contactBlendDuration_ = duration; }
    void setLiftBlendDuration(float duration) { liftBlendDuration_ = duration; }

private:
    // Detected timings from animation
    FootContactTiming leftTiming_;
    FootContactTiming rightTiming_;
    bool hasAnalyzedAnimation_ = false;

    // Current phase state
    FootPhaseData leftFoot_;
    FootPhaseData rightFoot_;

    // Previous normalized time for detecting wraps
    float prevNormalizedTime_ = 0.0f;

    // Configuration
    float contactThreshold_ = 0.02f;     // Height threshold for ground contact (meters)
    float contactBlendDuration_ = 0.05f; // Blend time into stance (normalized)
    float liftBlendDuration_ = 0.08f;    // Blend time into swing (normalized)

    // Analyze foot Y position curve to find contact/lift times
    FootContactTiming analyzeFootCurve(const AnimationClip& clip, int32_t footBoneIndex,
                                        const Skeleton& skeleton);

    // Update phase for a single foot
    void updateFootPhase(FootPhaseData& foot, const FootContactTiming& timing,
                        float normalizedTime, float deltaTime,
                        const glm::vec3& footWorldPos, bool wrapped);

    // Calculate foot height at a given animation time
    float sampleFootHeight(const AnimationClip& clip, int32_t footBoneIndex,
                          const Skeleton& skeleton, float time);
};
