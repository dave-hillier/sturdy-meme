#pragma once

#include "MotionDatabase.h"
#include "MotionMatchingTrajectory.h"
#include "AnimationBlend.h"
#include <memory>
#include <functional>

// Forward declarations
struct Skeleton;
struct AnimationClip;

namespace MotionMatching {

// Current playback state
struct PlaybackState {
    size_t clipIndex = 0;           // Current clip being played
    float time = 0.0f;              // Current time in clip
    float normalizedTime = 0.0f;    // 0-1 position in clip
    bool isPlaying = true;

    size_t matchedPoseIndex = 0;    // Last matched pose index
    float timeSinceMatch = 0.0f;    // Time since last pose match
    float playbackSpeedScale = 1.0f; // Current speed scaling for stride matching (debug)
};

// Statistics for debugging
struct MotionMatchingStats {
    float lastMatchCost = 0.0f;
    float lastTrajectoryCost = 0.0f;
    float lastPoseCost = 0.0f;
    float lastHeadingCost = 0.0f;
    float lastBiasCost = 0.0f;
    size_t matchesThisSecond = 0;
    size_t posesSearched = 0;
    std::string currentClipName;
    float currentClipTime = 0.0f;
};

// Policy for when to transition between clips
struct TransitionPolicy {
    float minDwellTime = 0.3f;          // Minimum time in current clip before allowing transition
    float costImprovementRatio = 0.8f;  // New match must be this fraction of current cost to trigger
    float forceTransitionTime = 1.0f;   // Force search for new clip after this long
    float sameClipMinTimeDiff = 0.2f;   // Minimum time jump for same-clip transitions (non-looping only)
    float sameClipCostRatio = 0.5f;     // Stricter ratio for same-clip jumps
};

// Configuration for the controller
struct ControllerConfig {
    // Search timing
    float searchInterval = 0.1f;      // How often to search (seconds)
    float forceSearchThreshold = 2.0f; // Cost threshold to force immediate search

    // Blending
    float defaultBlendDuration = 0.2f;
    bool useInertialBlending = true;

    // Transition
    TransitionPolicy transitionPolicy;

    // Trajectory
    TrajectoryPredictor::Config trajectoryConfig;

    // Feature extraction
    FeatureConfig featureConfig = FeatureConfig::locomotion();

    // Search options
    SearchOptions searchOptions;

    // Callbacks
    std::function<void(const MatchResult&)> onPoseMatched;
};

// Main motion matching controller
// Handles the complete motion matching loop:
// 1. Updates trajectory from input
// 2. Searches for best matching pose
// 3. Blends to selected pose
// 4. Applies animation to skeleton
class MotionMatchingController {
public:
    MotionMatchingController() = default;
    ~MotionMatchingController() = default;

    // Non-copyable
    MotionMatchingController(const MotionMatchingController&) = delete;
    MotionMatchingController& operator=(const MotionMatchingController&) = delete;

    // Initialize with configuration
    void initialize(const ControllerConfig& config);

    // Set the skeleton (must be done before building database)
    void setSkeleton(const Skeleton& skeleton);

    // Add animation clips to the database
    // locomotionSpeed: Override root velocity for in-place animations (0 = use extracted)
    // costBias: Negative = prefer this clip, positive = avoid (0 = neutral)
    void addClip(const AnimationClip* clip,
                 const std::string& name,
                 bool looping = true,
                 const std::vector<std::string>& tags = {},
                 float locomotionSpeed = 0.0f,
                 float costBias = 0.0f);

    // Build the motion database (call after adding all clips).
    // If cachePath is non-empty, uses it for caching computed data.
    void buildDatabase(const DatabaseBuildOptions& options = DatabaseBuildOptions{},
                       const std::filesystem::path& cachePath = {});

    // Update the controller
    // position: current character world position
    // facing: current character facing direction (Y=0)
    // inputDirection: desired movement direction from input (Y=0)
    // inputMagnitude: 0-1 how much movement is desired
    // deltaTime: frame time
    void update(const glm::vec3& position,
                const glm::vec3& facing,
                const glm::vec3& inputDirection,
                float inputMagnitude,
                float deltaTime);

    // Apply current animation state to skeleton
    void applyToSkeleton(Skeleton& skeleton) const;

    // Get current pose as SkeletonPose
    void getCurrentPose(SkeletonPose& outPose) const;

    // Force a search on next update
    void forceSearch() { forceSearchNextUpdate_ = true; }

    // Get the Y-axis rotation delta extracted from the root bone this frame.
    // For walk/run clips this is near-zero. For turn-in-place clips, this
    // represents the animation-driven rotation that should be fed into the
    // character controller's facing direction.
    float getExtractedRootYawDelta() const { return extractedRootYawDelta_; }

    // Set required tags for search
    void setRequiredTags(const std::vector<std::string>& tags);

    // Set excluded tags for search
    void setExcludedTags(const std::vector<std::string>& tags);

    // Strafe mode (Unreal-style orientation lock)
    void setStrafeMode(bool enabled);
    bool isStrafeMode() const { return strafeMode_; }

    // Set desired facing direction (for strafe mode - locked to camera direction)
    void setDesiredFacing(const glm::vec3& facing);

    // Get/set continuing pose cost bias
    void setContinuingPoseCostBias(float bias) { config_.searchOptions.continuingPoseCostBias = bias; }
    float getContinuingPoseCostBias() const { return config_.searchOptions.continuingPoseCostBias; }

    // Getters
    const MotionDatabase& getDatabase() const { return database_; }
    const PlaybackState& getPlaybackState() const { return playback_; }
    const MotionMatchingStats& getStats() const { return stats_; }
    const TrajectoryPredictor& getTrajectoryPredictor() const { return trajectoryPredictor_; }
    const InertialBlender& getInertialBlender() const { return inertialBlender_; }

    bool isInitialized() const { return initialized_; }
    bool isDatabaseBuilt() const { return database_.isBuilt(); }

    // Debug: Get the last matched trajectory for visualization
    const Trajectory& getLastMatchedTrajectory() const;

    // Debug: Get the query trajectory for visualization
    const Trajectory& getQueryTrajectory() const { return queryTrajectory_; }

private:
    ControllerConfig config_;
    MotionDatabase database_;
    MotionMatcher matcher_;
    TrajectoryPredictor trajectoryPredictor_;
    InertialBlender inertialBlender_;
    FeatureExtractor featureExtractor_;

    // Current state
    PlaybackState playback_;
    MotionMatchingStats stats_;

    // Cached data
    Trajectory queryTrajectory_;
    PoseFeatures queryPose_;
    SkeletonPose currentPose_;
    SkeletonPose previousPose_;

    // Timing
    float timeSinceLastSearch_ = 0.0f;
    float matchCountTimer_ = 0.0f;
    size_t matchCountThisSecond_ = 0;

    // Flags
    bool initialized_ = false;
    bool forceSearchNextUpdate_ = false;

    // Root yaw extraction
    float extractedRootYawDelta_ = 0.0f;

    // Per-bone velocity tracking for inertial blending
    SkeletonPose prevPrevPose_;          // Pose from two frames ago
    float prevDeltaTime_ = 0.0f;        // Delta time from previous frame

    // Strafe mode (Unreal-style)
    bool strafeMode_ = false;
    glm::vec3 desiredFacing_{0.0f, 0.0f, 1.0f};  // Locked facing direction in strafe mode

    // Internal methods
    void performSearch();
    void transitionToPose(const MatchResult& match);
    void advancePlayback(float deltaTime);
    void updatePose();
    void extractQueryFeatures();
};

} // namespace MotionMatching
