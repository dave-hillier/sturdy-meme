#pragma once

#include "MotionMatchingFeature.h"
#include "MotionMatchingTrajectory.h"
#include "MotionMatchingKDTree.h"
#include "Animation.h"
#include "AnimationBlend.h"
#include "GLTFLoader.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <filesystem>

namespace MotionMatching {

// A single indexed pose in the database
struct DatabasePose {
    // Source information
    size_t clipIndex = 0;             // Which animation clip
    float time = 0.0f;                // Time in the clip
    float normalizedTime = 0.0f;      // 0-1 position in clip

    // Pre-computed features
    PoseFeatures poseFeatures;
    Trajectory trajectory;

    // Optional cost bias (negative = prefer, positive = avoid)
    float costBias = 0.0f;

    // Flags for filtering
    bool isLoopBoundary = false;      // Near start/end of looping clip
    bool canTransitionFrom = true;    // Can we transition from this pose?
    bool canTransitionTo = true;      // Can we transition to this pose?

    // Tags for filtering (e.g., "locomotion", "combat", "idle")
    std::vector<std::string> tags;

    bool hasTag(const std::string& tag) const {
        for (const auto& t : tags) {
            if (t == tag) return true;
        }
        return false;
    }
};

// Animation clip metadata
struct DatabaseClip {
    std::string name;
    const AnimationClip* clip = nullptr;
    float duration = 0.0f;
    bool looping = true;

    // Indexing parameters
    float sampleRate = 30.0f;         // Samples per second
    size_t startPoseIndex = 0;        // First pose in database
    size_t poseCount = 0;             // Number of poses from this clip

    // Cost modifiers
    float costBias = 0.0f;            // Global bias for this clip

    // Locomotion speed (m/s) for in-place animations
    // If > 0, this overrides extracted root velocity for trajectory matching
    // This is critical for Mixamo and other in-place animation formats
    float locomotionSpeed = 0.0f;

    // Tags applied to all poses from this clip
    std::vector<std::string> tags;

    // Stride length (meters per full animation cycle) - computed during build.
    // For root-motion clips: total XZ root displacement over one cycle.
    // For in-place clips with locomotionSpeed > 0: locomotionSpeed * duration.
    float strideLength = 0.0f;
};

// Database building options
struct DatabaseBuildOptions {
    float defaultSampleRate = 30.0f;  // Default samples per second
    float minPoseInterval = 0.0f;     // Minimum time between poses (for pruning)
    float loopBoundaryMargin = 0.1f;  // Time margin at loop boundaries
    bool pruneStaticPoses = true;     // Remove poses with near-zero motion
    float staticThreshold = 0.01f;    // Velocity threshold for static detection
    bool buildKDTree = true;          // Build KD-tree for accelerated search
};

// Main database class
class MotionDatabase {
public:
    MotionDatabase() = default;

    // Initialize with skeleton and feature config
    void initialize(const Skeleton& skeleton, const FeatureConfig& config);

    // Add an animation clip to the database
    // Returns the clip index
    // locomotionSpeed: Override root velocity for in-place animations (0 = use extracted)
    // costBias: Negative = prefer this clip, positive = avoid (0 = neutral)
    size_t addClip(const AnimationClip* clip,
                   const std::string& name,
                   bool looping = true,
                   float sampleRate = 30.0f,
                   const std::vector<std::string>& tags = {},
                   float locomotionSpeed = 0.0f,
                   float costBias = 0.0f);

    // Build the database (index all poses).
    // If cachePath is non-empty, tries to load from cache first and saves after build.
    void build(const DatabaseBuildOptions& options = DatabaseBuildOptions{},
               const std::filesystem::path& cachePath = {});

    // Query methods
    size_t getPoseCount() const { return poses_.size(); }
    size_t getClipCount() const { return clips_.size(); }

    const DatabasePose& getPose(size_t index) const { return poses_[index]; }
    const DatabaseClip& getClip(size_t index) const { return clips_[index]; }

    // Get all poses from a specific clip
    std::vector<const DatabasePose*> getPosesFromClip(size_t clipIndex) const;

    // Get poses matching a tag
    std::vector<const DatabasePose*> getPosesWithTag(const std::string& tag) const;

    // Get the skeleton
    const Skeleton& getSkeleton() const { return skeleton_; }

    // Get the feature extractor
    const FeatureExtractor& getFeatureExtractor() const { return featureExtractor_; }

    // Check if database is built
    bool isBuilt() const { return built_; }

    // Get normalization data (computed during build)
    const FeatureNormalization& getNormalization() const { return normalization_; }

    // Get the KD-tree for accelerated search
    const MotionKDTree& getKDTree() const { return kdTree_; }
    bool hasKDTree() const { return kdTree_.isBuilt(); }

    // Convert a pose to KD-tree point (for query)
    KDPoint poseToKDPoint(const Trajectory& trajectory,
                          const PoseFeatures& pose) const;

    // Clear all data
    void clear();

    // Cache support - saves/loads pre-computed poses, normalization, and KD-tree
    // to avoid expensive feature extraction on subsequent loads.
    // The fingerprint is computed from clip metadata + config to detect staleness.
    bool saveCache(const std::filesystem::path& cachePath) const;
    bool loadCache(const std::filesystem::path& cachePath);

    // Compute a fingerprint string from current clip metadata and config.
    // Used to validate cache freshness.
    std::string computeFingerprint(const DatabaseBuildOptions& options) const;

    // Statistics
    struct Stats {
        size_t totalPoses = 0;
        size_t totalClips = 0;
        size_t prunedPoses = 0;
        float totalDuration = 0.0f;
    };
    Stats getStats() const;

private:
    Skeleton skeleton_;
    FeatureExtractor featureExtractor_;
    FeatureConfig config_;

    std::vector<DatabaseClip> clips_;
    std::vector<DatabasePose> poses_;
    FeatureNormalization normalization_;
    MotionKDTree kdTree_;

    bool initialized_ = false;
    bool built_ = false;
    std::string fingerprint_;  // Cache validation key

    // Index a single clip
    void indexClip(size_t clipIndex, const DatabaseBuildOptions& options);

    // Check if a pose should be pruned
    bool shouldPrunePose(const DatabasePose& pose, const DatabaseBuildOptions& options) const;

    // Compute normalization statistics from all poses
    void computeNormalization();

    // Build the KD-tree from all poses
    void buildKDTree();
};

// Search result from motion matching
struct MatchResult {
    size_t poseIndex = 0;             // Index in database
    float cost = 0.0f;                // Total matching cost
    float trajectoryCost = 0.0f;      // Trajectory component
    float poseCost = 0.0f;            // Pose component
    float headingCost = 0.0f;         // Heading/strafe component
    float biasCost = 0.0f;            // Continuing/looping bias applied

    const DatabasePose* pose = nullptr;
    const DatabaseClip* clip = nullptr;

    bool isValid() const { return pose != nullptr; }
};

// Search options
struct SearchOptions {
    // Weights for cost components
    float trajectoryWeight = 1.0f;
    float poseWeight = 1.0f;
    float headingWeight = 0.0f;              // Weight for heading channel (strafe)

    // Filtering
    std::vector<std::string> requiredTags;   // Pose must have all these tags
    std::vector<std::string> excludedTags;   // Pose must not have these tags
    bool allowLoopBoundaries = true;         // Allow poses near loop boundaries

    // Current pose info (for continuity)
    size_t currentPoseIndex = SIZE_MAX;      // SIZE_MAX = no current pose
    size_t currentClipIndex = SIZE_MAX;      // Current clip index for bias
    float minTimeSinceLastSelect = 0.1f;     // Minimum time before reselecting same pose

    // Continuing Pose Cost Bias (Unreal-style)
    // Negative = prefer continuing current animation (more stable)
    // Positive = switch animations more readily
    float continuingPoseCostBias = -0.3f;

    // Looping animation bias
    float loopingCostBias = -0.1f;

    // Strafe mode
    bool strafeMode = false;                 // When true, use strafe-oriented matching
    float strafeFacingWeight = 2.0f;         // Extra weight on facing during strafe
    glm::vec3 desiredFacing{0.0f, 0.0f, 1.0f}; // Desired facing direction (strafe target)
    glm::vec3 desiredMovement{0.0f};         // Desired movement direction

    // Performance - KD-tree acceleration
    bool useKDTree = true;                   // Use KD-tree for accelerated search
    size_t kdTreeCandidates = 64;            // Number of KD-tree candidates to evaluate
    size_t maxCandidates = 0;                // 0 = no limit (brute force, ignored if KD-tree used)
};

// Motion matcher - performs the search
class MotionMatcher {
public:
    MotionMatcher() = default;

    // Set the database to search
    void setDatabase(const MotionDatabase* database) { database_ = database; }

    // Find the best matching pose
    MatchResult findBestMatch(const Trajectory& queryTrajectory,
                               const PoseFeatures& queryPose,
                               const SearchOptions& options = SearchOptions{}) const;

    // Find top N matches
    std::vector<MatchResult> findTopMatches(const Trajectory& queryTrajectory,
                                             const PoseFeatures& queryPose,
                                             size_t count,
                                             const SearchOptions& options = SearchOptions{}) const;

    // Compute cost for a specific pose
    float computeCost(size_t poseIndex,
                      const Trajectory& queryTrajectory,
                      const PoseFeatures& queryPose,
                      const SearchOptions& options) const;

private:
    const MotionDatabase* database_ = nullptr;

    // Check if pose passes filters
    bool passesFilters(const DatabasePose& pose, const SearchOptions& options) const;
};

} // namespace MotionMatching
