#include "FootPhaseTracker.h"
#include "Animation.h"
#include "GLTFLoader.h"
#include "../ik/IKSolver.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

bool FootPhaseTracker::analyzeAnimation(const AnimationClip& clip, const Skeleton& skeleton,
                                         const std::string& leftFootBone, const std::string& rightFootBone) {
    int32_t leftIndex = skeleton.findJointIndex(leftFootBone);
    int32_t rightIndex = skeleton.findJointIndex(rightFootBone);

    if (leftIndex < 0 || rightIndex < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "FootPhaseTracker: Could not find foot bones '%s' and/or '%s'",
                    leftFootBone.c_str(), rightFootBone.c_str());
        return false;
    }

    leftTiming_ = analyzeFootCurve(clip, leftIndex, skeleton);
    rightTiming_ = analyzeFootCurve(clip, rightIndex, skeleton);
    hasAnalyzedAnimation_ = true;

    SDL_Log("FootPhaseTracker: Analyzed '%s'", clip.name.c_str());
    SDL_Log("  Left foot: contact=%.2f lift=%.2f stance=%.2f swing=%.2f",
            leftTiming_.contactTime, leftTiming_.liftTime,
            leftTiming_.stanceDuration, leftTiming_.swingDuration);
    SDL_Log("  Right foot: contact=%.2f lift=%.2f stance=%.2f swing=%.2f",
            rightTiming_.contactTime, rightTiming_.liftTime,
            rightTiming_.stanceDuration, rightTiming_.swingDuration);

    return true;
}

FootContactTiming FootPhaseTracker::analyzeFootCurve(const AnimationClip& clip, int32_t footBoneIndex,
                                                      const Skeleton& skeleton) {
    FootContactTiming timing;

    if (clip.duration <= 0.0f) {
        return timing;
    }

    // Sample foot height throughout the animation
    constexpr int NUM_SAMPLES = 64;
    std::vector<float> heights(NUM_SAMPLES);
    float minHeight = std::numeric_limits<float>::max();
    float maxHeight = std::numeric_limits<float>::lowest();

    for (int i = 0; i < NUM_SAMPLES; ++i) {
        float t = (static_cast<float>(i) / NUM_SAMPLES) * clip.duration;
        float h = sampleFootHeight(clip, footBoneIndex, skeleton, t);
        heights[i] = h;
        minHeight = std::min(minHeight, h);
        maxHeight = std::max(maxHeight, h);
    }

    // Dynamic threshold: ground contact is when foot is near minimum height
    float heightRange = maxHeight - minHeight;
    if (heightRange < 0.001f) {
        // Foot doesn't move vertically - assume always grounded
        timing.contactTime = 0.0f;
        timing.liftTime = 1.0f;
        timing.stanceDuration = 1.0f;
        timing.swingDuration = 0.0f;
        return timing;
    }

    // Contact threshold: 20% above minimum
    float groundThreshold = minHeight + heightRange * 0.2f;

    // Find contact and lift times by scanning for threshold crossings
    bool wasGrounded = heights[0] <= groundThreshold;
    float contactTime = wasGrounded ? 0.0f : -1.0f;
    float liftTime = -1.0f;

    for (int i = 1; i < NUM_SAMPLES; ++i) {
        bool isGrounded = heights[i] <= groundThreshold;

        if (!wasGrounded && isGrounded && contactTime < 0.0f) {
            // Foot just touched ground
            contactTime = static_cast<float>(i) / NUM_SAMPLES;
        } else if (wasGrounded && !isGrounded && liftTime < 0.0f) {
            // Foot just lifted
            liftTime = static_cast<float>(i) / NUM_SAMPLES;
        }

        wasGrounded = isGrounded;
    }

    // Handle cases where contact/lift weren't found
    if (contactTime < 0.0f) contactTime = 0.0f;
    if (liftTime < 0.0f) liftTime = 1.0f;

    // Ensure contact comes before lift (handle wrap-around)
    if (liftTime < contactTime) {
        // Contact happens later in cycle, lift earlier
        // Stance spans from contact through end of cycle and into lift
        timing.stanceDuration = (1.0f - contactTime) + liftTime;
        timing.swingDuration = contactTime - liftTime;
    } else {
        timing.stanceDuration = liftTime - contactTime;
        timing.swingDuration = 1.0f - timing.stanceDuration;
    }

    timing.contactTime = contactTime;
    timing.liftTime = liftTime;

    return timing;
}

float FootPhaseTracker::sampleFootHeight(const AnimationClip& clip, int32_t footBoneIndex,
                                          const Skeleton& skeleton, float time) {
    // Create a temporary skeleton copy for sampling
    Skeleton tempSkeleton = skeleton;

    // Reset to bind pose using inverseBindMatrix.
    // Previously the loop body was empty (no-op), so foot height was sampled relative to
    // whatever skeleton state existed at analysis time (bug #15).
    // inverseBindMatrix stores the inverse of each joint's global bind pose, so
    // globalBindPose[i] = inverse(inverseBindMatrix[i]).
    // Local bind transform = inverse(parent_global_bind) * global_bind.
    // Joints are assumed to be ordered parent-before-child (guaranteed by GLTF/FBX loaders).
    {
        const size_t n = tempSkeleton.joints.size();
        std::vector<glm::mat4> globalBind(n);
        for (size_t i = 0; i < n; ++i) {
            globalBind[i] = glm::inverse(tempSkeleton.joints[i].inverseBindMatrix);
        }
        for (size_t i = 0; i < n; ++i) {
            int32_t parent = tempSkeleton.joints[i].parentIndex;
            if (parent < 0) {
                tempSkeleton.joints[i].localTransform = globalBind[i];
            } else {
                tempSkeleton.joints[i].localTransform =
                    glm::inverse(globalBind[parent]) * globalBind[i];
            }
        }
    }

    // Sample animation at this time
    clip.sample(time, tempSkeleton, false);

    // Compute global transforms
    std::vector<glm::mat4> globalTransforms;
    tempSkeleton.computeGlobalTransforms(globalTransforms);

    // Return world Y position of foot bone
    if (footBoneIndex >= 0 && footBoneIndex < static_cast<int32_t>(globalTransforms.size())) {
        return globalTransforms[footBoneIndex][3][1]; // Y component of translation
    }

    return 0.0f;
}

void FootPhaseTracker::update(float normalizedTime, float deltaTime,
                               const glm::vec3& leftFootWorldPos, const glm::vec3& rightFootWorldPos,
                               const glm::mat4& characterTransform) {
    // Detect animation wrap
    bool wrapped = normalizedTime < prevNormalizedTime_ - 0.5f;
    prevNormalizedTime_ = normalizedTime;

    // Update each foot's phase
    updateFootPhase(leftFoot_, leftTiming_, normalizedTime, deltaTime, leftFootWorldPos, wrapped);
    updateFootPhase(rightFoot_, rightTiming_, normalizedTime, deltaTime, rightFootWorldPos, wrapped);
}

void FootPhaseTracker::updateFootPhase(FootPhaseData& foot, const FootContactTiming& timing,
                                        float normalizedTime, float deltaTime,
                                        const glm::vec3& footWorldPos, bool wrapped) {
    if (!hasAnalyzedAnimation_) {
        // No timing data - use simple height-based detection
        // (This is a fallback for when animation hasn't been analyzed)
        return;
    }

    FootPhase prevPhase = foot.phase;

    // Determine current phase based on normalized time
    // Handle wrap-around case where stance spans cycle boundary
    bool inStance;
    if (timing.liftTime < timing.contactTime) {
        // Stance spans from contactTime to end and from start to liftTime
        inStance = normalizedTime >= timing.contactTime || normalizedTime < timing.liftTime;
    } else {
        // Normal case: stance is between contact and lift
        inStance = normalizedTime >= timing.contactTime && normalizedTime < timing.liftTime;
    }

    // Calculate phase progress
    if (inStance) {
        // In stance phase - further subdivide into contact, stance, push-off
        float stanceProgress;
        if (timing.liftTime < timing.contactTime) {
            // Wrap-around case
            if (normalizedTime >= timing.contactTime) {
                stanceProgress = (normalizedTime - timing.contactTime) / timing.stanceDuration;
            } else {
                stanceProgress = ((1.0f - timing.contactTime) + normalizedTime) / timing.stanceDuration;
            }
        } else {
            stanceProgress = (normalizedTime - timing.contactTime) / timing.stanceDuration;
        }
        stanceProgress = glm::clamp(stanceProgress, 0.0f, 1.0f);

        // First 10% of stance is contact phase
        // Last 15% of stance is push-off phase
        // Middle is stance phase
        if (stanceProgress < 0.10f) {
            foot.phase = FootPhase::Contact;
            foot.phaseProgress = stanceProgress / 0.10f;
        } else if (stanceProgress > 0.85f) {
            foot.phase = FootPhase::PushOff;
            foot.phaseProgress = (stanceProgress - 0.85f) / 0.15f;
        } else {
            foot.phase = FootPhase::Stance;
            foot.phaseProgress = (stanceProgress - 0.10f) / 0.75f;
        }

        foot.isGrounded = true;

        // Lock position at start of stance
        if (prevPhase == FootPhase::Swing || prevPhase == FootPhase::Contact) {
            if (foot.phase == FootPhase::Contact && !foot.hasLockedPosition) {
                foot.lockedPosition = footWorldPos;
                foot.hasLockedPosition = true;
                foot.lastContactTime = normalizedTime;
            }
        }
    } else {
        // In swing phase
        foot.phase = FootPhase::Swing;
        foot.isGrounded = false;
        foot.hasLockedPosition = false;

        // Calculate swing progress
        float swingProgress;
        if (timing.liftTime < timing.contactTime) {
            // Swing is between liftTime and contactTime
            swingProgress = (normalizedTime - timing.liftTime) / timing.swingDuration;
        } else {
            // Wrap-around case for swing
            if (normalizedTime >= timing.liftTime) {
                swingProgress = (normalizedTime - timing.liftTime) / timing.swingDuration;
            } else {
                swingProgress = ((1.0f - timing.liftTime) + normalizedTime) / timing.swingDuration;
            }
        }
        foot.phaseProgress = glm::clamp(swingProgress, 0.0f, 1.0f);

        if (prevPhase != FootPhase::Swing) {
            foot.lastLiftTime = normalizedTime;
        }
    }
}

float FootPhaseTracker::getIKWeight(bool isLeftFoot) const {
    const FootPhaseData& foot = isLeftFoot ? leftFoot_ : rightFoot_;

    switch (foot.phase) {
        case FootPhase::Swing:
            // During swing, reduce IK to let animation play
            // But ramp up at end of swing for smooth contact
            if (foot.phaseProgress > 0.7f) {
                return (foot.phaseProgress - 0.7f) / 0.3f; // 0 to 1 in last 30%
            }
            return 0.0f;

        case FootPhase::Contact:
            // Blend IK in during contact
            return foot.phaseProgress;

        case FootPhase::Stance:
            // Full IK during stance
            return 1.0f;

        case FootPhase::PushOff:
            // Blend IK out during push-off
            return 1.0f - foot.phaseProgress;
    }

    return 0.0f;
}

float FootPhaseTracker::getLockBlend(bool isLeftFoot) const {
    const FootPhaseData& foot = isLeftFoot ? leftFoot_ : rightFoot_;

    switch (foot.phase) {
        case FootPhase::Swing:
            return 0.0f; // Never lock during swing

        case FootPhase::Contact:
            // Ramp up lock during contact
            return foot.phaseProgress;

        case FootPhase::Stance:
            // Full lock during stance
            return 1.0f;

        case FootPhase::PushOff:
            // Ramp down lock during push-off
            return 1.0f - foot.phaseProgress * 0.5f; // Partial unlock for toe pivot
    }

    return 0.0f;
}

void FootPhaseTracker::reset() {
    leftFoot_ = FootPhaseData();
    rightFoot_ = FootPhaseData();
    prevNormalizedTime_ = 0.0f;
}

void FootPhaseTracker::applyToFootIK(bool isLeftFoot, FootPlacementIK& foot) const {
    const FootPhaseData& data = isLeftFoot ? leftFoot_ : rightFoot_;

    // Push phase and progress directly into the IK struct so there is a single
    // source of truth (previously both FootPhaseData and FootPlacementIK maintained
    // independent copies of phase/progress/lockBlend – bug #17).
    foot.currentPhase = data.phase;
    foot.phaseProgress = data.phaseProgress;
    foot.lockBlend = getLockBlend(isLeftFoot);

    // When the tracker says the foot should be unlocked (swing), clear the IK lock
    // so it doesn't hold stale world-space position data.
    if (data.phase == FootPhase::Swing) {
        foot.isLocked = false;
    }
}
