#include "MotionMatchingController.h"
#include "Animation.h"
#include "GLTFLoader.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace MotionMatching {

void MotionMatchingController::initialize(const ControllerConfig& config) {
    config_ = config;
    trajectoryPredictor_.setConfig(config.trajectoryConfig);
    inertialBlender_.setConfig({config.defaultBlendDuration});
    matcher_.setDatabase(&database_);

    initialized_ = true;

    SDL_Log("MotionMatchingController: Initialized");
}

void MotionMatchingController::setSkeleton(const Skeleton& skeleton) {
    database_.initialize(skeleton, config_.featureConfig);
    featureExtractor_.initialize(skeleton, config_.featureConfig);

    // Initialize pose storage
    currentPose_.resize(skeleton.joints.size());
    previousPose_.resize(skeleton.joints.size());

    SDL_Log("MotionMatchingController: Skeleton set with %zu joints",
            skeleton.joints.size());
}

void MotionMatchingController::addClip(const AnimationClip* clip,
                                         const std::string& name,
                                         bool looping,
                                         const std::vector<std::string>& tags,
                                         float locomotionSpeed,
                                         float costBias) {
    database_.addClip(clip, name, looping, 30.0f, tags, locomotionSpeed, costBias);
}

void MotionMatchingController::buildDatabase(const DatabaseBuildOptions& options) {
    database_.build(options);

    // Start with first pose if available
    if (database_.getPoseCount() > 0) {
        const DatabasePose& firstPose = database_.getPose(0);
        playback_.clipIndex = firstPose.clipIndex;
        playback_.time = firstPose.time;
        playback_.matchedPoseIndex = 0;

        updatePose();
    }

    SDL_Log("MotionMatchingController: Database built with %zu poses",
            database_.getPoseCount());
}

void MotionMatchingController::update(const glm::vec3& position,
                                        const glm::vec3& facing,
                                        const glm::vec3& inputDirection,
                                        float inputMagnitude,
                                        float deltaTime) {
    if (!initialized_ || !database_.isBuilt()) {
        return;
    }

    // Update trajectory predictor
    trajectoryPredictor_.update(position, facing, inputDirection, inputMagnitude, deltaTime);

    // Update inertial blender
    if (config_.useInertialBlending) {
        inertialBlender_.update(deltaTime);
    }

    // Advance current playback
    advancePlayback(deltaTime);

    // Update pose from current playback
    updatePose();

    // Extract query features from current state
    extractQueryFeatures();

    // Update search timing
    timeSinceLastSearch_ += deltaTime;
    playback_.timeSinceMatch += deltaTime;

    // Update stats timing
    matchCountTimer_ += deltaTime;
    if (matchCountTimer_ >= 1.0f) {
        stats_.matchesThisSecond = matchCountThisSecond_;
        matchCountThisSecond_ = 0;
        matchCountTimer_ = 0.0f;
    }

    // Check if we need to search for a new pose
    bool shouldSearch = forceSearchNextUpdate_ ||
                        (timeSinceLastSearch_ >= config_.searchInterval);

    // Also search if trajectory has changed significantly
    if (!shouldSearch && database_.getPoseCount() > 0) {
        const DatabasePose& currentMatchedPose = database_.getPose(playback_.matchedPoseIndex);
        float currentCost = queryTrajectory_.computeCost(
            currentMatchedPose.trajectory,
            config_.featureConfig.trajectoryPositionWeight,
            config_.featureConfig.trajectoryVelocityWeight,
            config_.featureConfig.trajectoryFacingWeight
        );
        if (currentCost > config_.forceSearchThreshold) {
            shouldSearch = true;
        }
    }

    if (shouldSearch) {
        performSearch();
        forceSearchNextUpdate_ = false;
        timeSinceLastSearch_ = 0.0f;
    }
}

void MotionMatchingController::performSearch() {
    // Generate query trajectory (in world space) - keep this for visualization
    queryTrajectory_ = trajectoryPredictor_.generateTrajectory();

    // Create a local-space copy for matching
    // Database trajectories are in animation-local space where forward is Z+
    // We need to rotate the query so the character's facing direction becomes Z+
    Trajectory localTrajectory = queryTrajectory_;

    glm::vec3 facing = trajectoryPredictor_.getCurrentFacing();
    if (glm::length(facing) > 0.01f) {
        facing = glm::normalize(facing);

        // Build rotation from world to local: rotate so facing -> Z+
        // facing.x = sin(angle), facing.z = cos(angle) where angle is rotation around Y
        float angle = std::atan2(facing.x, facing.z);
        float cosA = std::cos(-angle);  // Negative to rotate TO local
        float sinA = std::sin(-angle);

        for (size_t i = 0; i < localTrajectory.sampleCount; ++i) {
            TrajectorySample& s = localTrajectory.samples[i];

            // Rotate position around Y axis (3D Y-axis rotation, not 2D rotation)
            glm::vec3 pos = s.position;
            s.position.x = pos.x * cosA + pos.z * sinA;
            s.position.z = -pos.x * sinA + pos.z * cosA;

            // Rotate velocity
            glm::vec3 vel = s.velocity;
            s.velocity.x = vel.x * cosA + vel.z * sinA;
            s.velocity.z = -vel.x * sinA + vel.z * cosA;

            // Rotate facing direction
            glm::vec3 fac = s.facing;
            s.facing.x = fac.x * cosA + fac.z * sinA;
            s.facing.z = -fac.x * sinA + fac.z * cosA;
        }
    }

    // Set search options for continuity (Unreal-style continuing pose bias)
    SearchOptions options = config_.searchOptions;
    options.currentPoseIndex = playback_.matchedPoseIndex;
    options.currentClipIndex = playback_.clipIndex;  // For continuing pose cost bias

    // Configure strafe mode options
    options.strafeMode = strafeMode_;
    if (strafeMode_) {
        options.desiredFacing = desiredFacing_;
        options.desiredMovement = trajectoryPredictor_.getCurrentVelocity();
        // Increase heading weight in strafe mode
        options.headingWeight = config_.featureConfig.headingWeight > 0.0f ?
            config_.featureConfig.headingWeight * 2.0f : 1.5f;

        // Only require strafe tag when movement is predominantly sideways
        // Use local-space velocity to determine movement direction relative to facing
        // The localTrajectory has already been transformed so facing = Z+
        if (localTrajectory.sampleCount > 0) {
            // Find a future sample to get predicted velocity
            glm::vec3 localVel(0.0f);
            for (size_t i = 0; i < localTrajectory.sampleCount; ++i) {
                if (localTrajectory.samples[i].timeOffset > 0.0f) {
                    localVel = localTrajectory.samples[i].velocity;
                    break;
                }
            }

            float speed = glm::length(localVel);
            float sidewaysSpeed = std::abs(localVel.x);  // X = sideways in local space
            float forwardSpeed = std::abs(localVel.z);   // Z = forward in local space

            // Only use strafe animations when moving predominantly sideways
            // Forward/backward movement uses regular walk animations
            if (speed > 0.5f && sidewaysSpeed > forwardSpeed * 0.7f) {
                options.requiredTags.push_back("strafe");
            }
        }
    }

    // Perform search with local-space trajectory
    MatchResult match = matcher_.findBestMatch(localTrajectory, queryPose_, options);

    if (match.isValid()) {
        // Check if this is a different clip
        bool isDifferentClip = (match.pose->clipIndex != playback_.clipIndex);

        // Get current clip info
        const DatabaseClip& currentClip = database_.getClip(playback_.clipIndex);

        // For looping clips we're already playing, don't allow same-clip time jumps
        // Just let the animation play through naturally - only switch when going to different clip
        bool shouldTransition = false;

        if (isDifferentClip) {
            // Switching to different clip - allow if cost is better or we've been here a while
            shouldTransition = (match.cost < stats_.lastMatchCost * 0.8f ||
                               playback_.timeSinceMatch > 0.5f);
        }
        // For same clip: only transition if we're NOT in a looping clip
        // Non-looping clips (like jumps) may need time jumps for responsiveness
        else if (!currentClip.looping) {
            float timeDiff = std::abs(match.pose->time - playback_.time);
            bool isSignificantTimeJump = timeDiff > 0.2f;
            if (isSignificantTimeJump && match.cost < stats_.lastMatchCost * 0.5f) {
                shouldTransition = true;
            }
        }
        // For looping clips in same clip: never jump, let it play naturally

        // Force transition after 1.0s only if it's to a different clip
        bool forceTransition = (playback_.timeSinceMatch > 1.0f) && isDifferentClip;

        if (shouldTransition || forceTransition) {
            transitionToPose(match);
            ++matchCountThisSecond_;

            if (config_.onPoseMatched) {
                config_.onPoseMatched(match);
            }
        }

        // Update stats
        stats_.lastMatchCost = match.cost;
        stats_.lastTrajectoryCost = match.trajectoryCost;
        stats_.lastPoseCost = match.poseCost;
        stats_.posesSearched = database_.getPoseCount();
    }
}

void MotionMatchingController::transitionToPose(const MatchResult& match) {
    // Store previous pose for blending
    previousPose_ = currentPose_;

    // Update playback state
    playback_.clipIndex = match.pose->clipIndex;
    playback_.time = match.pose->time;
    playback_.normalizedTime = match.pose->normalizedTime;
    playback_.matchedPoseIndex = match.poseIndex;
    playback_.timeSinceMatch = 0.0f;

    // Update stats
    stats_.currentClipName = match.clip->name;
    stats_.currentClipTime = match.pose->time;

    // Update the current pose to the new target
    updatePose();

    // Start inertial blend if enabled
    if (config_.useInertialBlending && !previousPose_.empty() && !currentPose_.empty()) {
        // Use full skeletal inertialization for smoother transitions
        // Note: We don't have per-bone velocities tracked, so pass empty vectors
        // The blender will assume zero velocity, which is reasonable for animation transitions
        inertialBlender_.startSkeletalBlend(previousPose_, currentPose_);
    }
}

void MotionMatchingController::advancePlayback(float deltaTime) {
    if (!playback_.isPlaying || database_.getClipCount() == 0) {
        return;
    }

    const DatabaseClip& clip = database_.getClip(playback_.clipIndex);
    if (!clip.clip) {
        return;
    }

    // Advance time
    playback_.time += deltaTime;

    // Handle looping
    if (clip.looping) {
        while (playback_.time >= clip.duration) {
            playback_.time -= clip.duration;
        }
    } else {
        playback_.time = std::min(playback_.time, clip.duration);
    }

    // Update normalized time
    if (clip.duration > 0.0f) {
        playback_.normalizedTime = playback_.time / clip.duration;
    }

    // Update stats
    stats_.currentClipTime = playback_.time;
}

void MotionMatchingController::updatePose() {
    if (database_.getClipCount() == 0) {
        return;
    }

    const DatabaseClip& clip = database_.getClip(playback_.clipIndex);
    if (!clip.clip) {
        return;
    }

    // Sample animation at current time
    Skeleton tempSkeleton = database_.getSkeleton();
    clip.clip->sample(playback_.time, tempSkeleton, true);

    // Convert to SkeletonPose
    for (size_t i = 0; i < tempSkeleton.joints.size() && i < currentPose_.size(); ++i) {
        currentPose_[i] = BonePose::fromMatrix(
            tempSkeleton.joints[i].localTransform,
            tempSkeleton.joints[i].preRotation
        );
    }
}

void MotionMatchingController::extractQueryFeatures() {
    if (database_.getClipCount() == 0) {
        return;
    }

    const DatabaseClip& clip = database_.getClip(playback_.clipIndex);
    if (!clip.clip) {
        return;
    }

    // Extract features from current pose
    queryPose_ = featureExtractor_.extractFromClip(
        *clip.clip,
        database_.getSkeleton(),
        playback_.time
    );

    // Update root velocity from trajectory predictor
    queryPose_.rootVelocity = trajectoryPredictor_.getCurrentVelocity();

    // Update angular velocity from trajectory predictor
    // This is critical for matching turn animations - the query needs to reflect
    // the player's actual turning rate, not just what the current animation shows
    queryPose_.rootAngularVelocity = trajectoryPredictor_.getCurrentAngularVelocity();
}

void MotionMatchingController::applyToSkeleton(Skeleton& skeleton) const {
    if (currentPose_.empty()) {
        return;
    }

    // Copy current pose for potential modification
    SkeletonPose blendedPose = currentPose_;

    // Apply inertial blending if active
    if (config_.useInertialBlending && inertialBlender_.isBlending()) {
        inertialBlender_.applyToPose(blendedPose);
    }

    // Apply blended pose to skeleton
    for (size_t i = 0; i < skeleton.joints.size() && i < blendedPose.size(); ++i) {
        skeleton.joints[i].localTransform = blendedPose[i].toMatrix(
            skeleton.joints[i].preRotation
        );
    }
}

void MotionMatchingController::getCurrentPose(SkeletonPose& outPose) const {
    outPose = currentPose_;

    // Apply full skeletal inertial blending
    if (config_.useInertialBlending && inertialBlender_.isBlending() && !outPose.empty()) {
        inertialBlender_.applyToPose(outPose);
    }
}

void MotionMatchingController::setRequiredTags(const std::vector<std::string>& tags) {
    config_.searchOptions.requiredTags = tags;
}

void MotionMatchingController::setExcludedTags(const std::vector<std::string>& tags) {
    config_.searchOptions.excludedTags = tags;
}

void MotionMatchingController::setStrafeMode(bool enabled) {
    // Only update if value actually changed
    if (strafeMode_ == enabled) {
        return;
    }

    strafeMode_ = enabled;

    // Update feature extractor strafe mode for heading extraction
    featureExtractor_.setStrafeMode(enabled);

    // Update trajectory predictor strafe mode
    trajectoryPredictor_.setStrafeMode(enabled);

    // Force a search to update matching based on new mode
    forceSearchNextUpdate_ = true;

    SDL_Log("MotionMatchingController: Strafe mode %s", enabled ? "enabled" : "disabled");
}

void MotionMatchingController::setDesiredFacing(const glm::vec3& facing) {
    desiredFacing_ = facing;
    // Also update trajectory predictor's strafe facing
    trajectoryPredictor_.setStrafeFacing(facing);
}

const Trajectory& MotionMatchingController::getLastMatchedTrajectory() const {
    static Trajectory empty;

    if (database_.getPoseCount() == 0 ||
        playback_.matchedPoseIndex >= database_.getPoseCount()) {
        return empty;
    }

    return database_.getPose(playback_.matchedPoseIndex).trajectory;
}

} // namespace MotionMatching
