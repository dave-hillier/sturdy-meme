#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <array>

// Forward declarations
struct Skeleton;
struct AnimationClip;
struct SkeletonPose;

namespace MotionMatching {

// ============================================================================
// Pose Search Schema (Unreal-style configuration)
// ============================================================================
// Data preprocessing mode for feature normalization (like Unreal's Data Preprocessor)
enum class DataPreprocessor {
    None,               // No preprocessing
    Normalize,          // Normalize against mean
    NormalizeByDeviation // Normalize against standard deviation (default)
};

// Heading axis for orientation channel (like Unreal's Heading Axis)
enum class HeadingAxis {
    X,      // Right/Left
    Y,      // Up/Down (rarely used)
    Z       // Forward/Back (default)
};

// Component stripping for heading queries
enum class ComponentStrip {
    None,   // Use full 3D heading
    StripY, // Horizontal only (most common)
    StripXZ // Vertical only
};

// Input pose mode for queries (like Unreal's Input Query Pose)
enum class InputQueryPoseMode {
    CharacterPose,          // Use current character pose
    ContinuingPose,         // Use continuing animation pose
    InterpolatedContinuing  // Interpolate between character and continuing
};

// Channel types for schema configuration
enum class ChannelType {
    Trajectory,     // Character movement trajectory
    Pose,           // Bone positions and velocities
    Heading,        // Bone orientation/facing direction
    Velocity,       // Movement speed
    Phase           // Animation phase (foot cycle, etc.)
};

// Individual channel configuration (like Unreal's Schema Channels)
struct SchemaChannel {
    std::string name;
    ChannelType type = ChannelType::Trajectory;
    float weight = 1.0f;
    bool enabled = true;

    // Trajectory channel settings
    std::vector<float> sampleTimes;  // Time offsets for trajectory samples

    // Pose channel settings
    std::vector<std::string> boneNames;  // Bones to track

    // Heading channel settings (for strafing/orientation)
    HeadingAxis headingAxis = HeadingAxis::Z;
    ComponentStrip componentStrip = ComponentStrip::StripY;
    std::string headingBoneName = "Hips";  // Bone to query heading from

    // Velocity channel settings
    bool useGlobalSpace = false;  // true = world space, false = character space

    // Default constructors for common channel types
    static SchemaChannel trajectoryChannel() {
        SchemaChannel ch;
        ch.name = "Trajectory";
        ch.type = ChannelType::Trajectory;
        ch.weight = 2.0f;  // Higher weight for locomotion type selection
        ch.sampleTimes = {-0.2f, -0.1f, 0.1f, 0.2f, 0.4f, 0.6f};
        return ch;
    }

    static SchemaChannel poseChannel() {
        SchemaChannel ch;
        ch.name = "Pose";
        ch.type = ChannelType::Pose;
        ch.weight = 1.0f;
        ch.boneNames = {"LeftFoot", "RightFoot", "Hips"};
        return ch;
    }

    static SchemaChannel headingChannel() {
        SchemaChannel ch;
        ch.name = "Heading";
        ch.type = ChannelType::Heading;
        ch.weight = 1.5f;  // Important for strafing
        ch.headingAxis = HeadingAxis::Z;
        ch.componentStrip = ComponentStrip::StripY;
        ch.headingBoneName = "Hips";
        return ch;
    }

    static SchemaChannel velocityChannel() {
        SchemaChannel ch;
        ch.name = "Velocity";
        ch.type = ChannelType::Velocity;
        ch.weight = 0.5f;
        ch.useGlobalSpace = false;
        return ch;
    }
};

// Pose Search Schema - like Unreal's Pose Search Schema asset
struct PoseSearchSchema {
    std::string name = "Default";

    // Channels
    std::vector<SchemaChannel> channels;

    // Data preprocessing
    DataPreprocessor preprocessor = DataPreprocessor::NormalizeByDeviation;

    // Input query configuration
    InputQueryPoseMode queryPoseMode = InputQueryPoseMode::CharacterPose;

    // Continuing pose bias (negative = prefer continuing, positive = switch faster)
    // Like Unreal's "Continuing Pose Cost Bias"
    float continuingPoseCostBias = -0.3f;

    // Looping animation bias
    float loopingCostBias = -0.1f;

    // Strafe mode configuration
    bool strafeMode = false;  // When true, character faces camera direction
    float strafeFacingWeight = 2.0f;  // Extra weight on facing match during strafe

    // Search configuration
    bool useKDTree = true;
    size_t kdTreeCandidates = 64;

    // Default schema for locomotion
    static PoseSearchSchema locomotion() {
        PoseSearchSchema schema;
        schema.name = "Locomotion";
        schema.channels.push_back(SchemaChannel::trajectoryChannel());
        schema.channels.push_back(SchemaChannel::poseChannel());
        schema.channels.push_back(SchemaChannel::velocityChannel());
        return schema;
    }

    // Schema with heading channel for strafe support
    static PoseSearchSchema locomotionWithStrafe() {
        PoseSearchSchema schema;
        schema.name = "LocomotionStrafe";
        schema.channels.push_back(SchemaChannel::trajectoryChannel());
        schema.channels.push_back(SchemaChannel::poseChannel());
        schema.channels.push_back(SchemaChannel::headingChannel());
        schema.channels.push_back(SchemaChannel::velocityChannel());
        return schema;
    }

    // Get channel by name
    SchemaChannel* getChannel(const std::string& name) {
        for (auto& ch : channels) {
            if (ch.name == name) return &ch;
        }
        return nullptr;
    }

    const SchemaChannel* getChannel(const std::string& name) const {
        for (const auto& ch : channels) {
            if (ch.name == name) return &ch;
        }
        return nullptr;
    }

    // Get total weight of all enabled channels (for normalization)
    float getTotalWeight() const {
        float total = 0.0f;
        for (const auto& ch : channels) {
            if (ch.enabled) total += ch.weight;
        }
        return total;
    }
};

// Heading feature for orientation queries (strafing support)
struct HeadingFeature {
    glm::vec3 direction{0.0f, 0.0f, 1.0f};  // Heading direction
    glm::vec3 movementDirection{0.0f};       // Desired movement direction
    float angleDifference = 0.0f;            // Angle between heading and movement (radians)

    // Compute cost between two heading features
    float computeCost(const HeadingFeature& other, float weight = 1.0f) const {
        // Cost based on heading direction difference
        float headingDot = glm::dot(
            glm::normalize(direction),
            glm::normalize(other.direction)
        );
        // 1 - dot gives 0 for same direction, 2 for opposite
        return (1.0f - headingDot) * weight;
    }

    // Compute strafe cost (how well the animation matches strafe direction)
    float computeStrafeCost(const glm::vec3& desiredMovement, float weight = 1.0f) const {
        if (glm::length(desiredMovement) < 0.001f) {
            return 0.0f;  // No movement, no strafe cost
        }

        // Compute angle between character heading and movement direction
        glm::vec3 normHeading = glm::normalize(direction);
        glm::vec3 normMovement = glm::normalize(desiredMovement);

        // Dot product gives cosine of angle
        float dot = glm::dot(normHeading, normMovement);

        // Convert to angle (0 = forward, PI/2 = strafe, PI = backward)
        float angle = std::acos(glm::clamp(dot, -1.0f, 1.0f));

        // The cost reflects how different the actual strafe angle is from expected
        // Lower cost when animation strafe matches desired strafe
        return std::abs(angle - angleDifference) * weight;
    }
};

// Maximum number of trajectory samples for prediction
constexpr size_t MAX_TRAJECTORY_SAMPLES = 8;

// Maximum number of bones to track for pose features
constexpr size_t MAX_FEATURE_BONES = 8;

// Forward declaration
struct FeatureNormalization;

// Default feature bones commonly used in locomotion
namespace FeatureBones {
    constexpr const char* LEFT_FOOT = "LeftFoot";
    constexpr const char* RIGHT_FOOT = "RightFoot";
    constexpr const char* LEFT_HAND = "LeftHand";
    constexpr const char* RIGHT_HAND = "RightHand";
    constexpr const char* HIPS = "Hips";
    constexpr const char* SPINE = "Spine";
}

// A single trajectory sample point
struct TrajectorySample {
    glm::vec3 position{0.0f};    // Position relative to character (local space)
    glm::vec3 velocity{0.0f};    // Velocity at this point
    glm::vec3 facing{0.0f, 0.0f, 1.0f}; // Facing direction
    float timeOffset = 0.0f;      // Time offset from current (negative = past, positive = future)
};

// Trajectory containing past and future movement prediction
struct Trajectory {
    std::array<TrajectorySample, MAX_TRAJECTORY_SAMPLES> samples;
    size_t sampleCount = 0;

    void clear() { sampleCount = 0; }

    void addSample(const TrajectorySample& sample) {
        if (sampleCount < MAX_TRAJECTORY_SAMPLES) {
            samples[sampleCount++] = sample;
        }
    }

    // Compute cost between two trajectories
    float computeCost(const Trajectory& other,
                      float positionWeight = 1.0f,
                      float velocityWeight = 0.5f,
                      float facingWeight = 0.3f) const;

    // Compute normalized cost between two trajectories
    float computeNormalizedCost(const Trajectory& other,
                                const FeatureNormalization& norm,
                                float positionWeight = 1.0f,
                                float velocityWeight = 0.5f,
                                float facingWeight = 0.3f) const;
};

// Feature for a single bone (position + velocity in character space)
struct BoneFeature {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};

    float computeCost(const BoneFeature& other,
                      float positionWeight = 1.0f,
                      float velocityWeight = 0.5f) const {
        float posCost = glm::length(position - other.position) * positionWeight;
        float velCost = glm::length(velocity - other.velocity) * velocityWeight;
        return posCost + velCost;
    }
};

// Complete pose features for matching
struct PoseFeatures {
    // Bone features (position + velocity for key bones)
    std::array<BoneFeature, MAX_FEATURE_BONES> boneFeatures;
    size_t boneCount = 0;

    // Root velocity (horizontal movement)
    glm::vec3 rootVelocity{0.0f};

    // Root angular velocity (turning rate)
    float rootAngularVelocity = 0.0f;

    // Foot phase information (0-1 cycle)
    float leftFootPhase = 0.0f;
    float rightFootPhase = 0.0f;

    // Heading feature (for strafe/orientation queries)
    HeadingFeature heading;

    // Compute cost between two pose features
    float computeCost(const PoseFeatures& other,
                      float boneWeight = 1.0f,
                      float rootVelWeight = 0.5f,
                      float angularVelWeight = 0.3f,
                      float phaseWeight = 0.2f) const;

    // Compute normalized cost between two pose features
    float computeNormalizedCost(const PoseFeatures& other,
                                const FeatureNormalization& norm,
                                float boneWeight = 1.0f,
                                float rootVelWeight = 0.5f,
                                float angularVelWeight = 0.3f,
                                float phaseWeight = 0.2f) const;

    // Compute heading/strafe cost separately
    float computeHeadingCost(const PoseFeatures& other, float weight = 1.0f) const {
        return heading.computeCost(other.heading, weight);
    }
};

// Normalization statistics for a single feature dimension
struct FeatureStats {
    float mean = 0.0f;
    float stdDev = 1.0f;  // Default to 1 to avoid division by zero

    // Normalize a value using these statistics
    float normalize(float value) const {
        return (value - mean) / stdDev;
    }
};

// Normalization data for all features
struct FeatureNormalization {
    // Trajectory normalization (per sample point)
    std::array<FeatureStats, MAX_TRAJECTORY_SAMPLES> trajectoryPosition;  // magnitude
    std::array<FeatureStats, MAX_TRAJECTORY_SAMPLES> trajectoryVelocity;  // magnitude

    // Bone feature normalization (per bone)
    std::array<FeatureStats, MAX_FEATURE_BONES> bonePosition;    // magnitude
    std::array<FeatureStats, MAX_FEATURE_BONES> boneVelocity;    // magnitude

    // Root motion normalization
    FeatureStats rootVelocity;       // magnitude
    FeatureStats rootAngularVelocity;

    bool isComputed = false;
};

// Configuration for feature extraction
struct FeatureConfig {
    // Bones to extract features from (by name)
    std::vector<std::string> featureBoneNames;

    // Weights for cost computation
    // Trajectory is weighted higher for locomotion type selection (idle/walk/run)
    // Pose is more important for continuity within the same locomotion type
    float trajectoryWeight = 2.0f;
    float poseWeight = 1.0f;
    float bonePositionWeight = 1.0f;
    float boneVelocityWeight = 0.5f;
    float trajectoryPositionWeight = 1.0f;
    float trajectoryVelocityWeight = 0.5f;
    float trajectoryFacingWeight = 0.3f;
    float rootVelocityWeight = 0.5f;
    float angularVelocityWeight = 0.3f;
    float phaseWeight = 0.2f;

    // Heading/Strafe configuration (Unreal-style)
    float headingWeight = 0.0f;  // 0 = disabled, > 0 = enable heading channel
    std::string headingBoneName = "Hips";
    HeadingAxis headingAxis = HeadingAxis::Z;
    ComponentStrip headingComponentStrip = ComponentStrip::StripY;

    // Trajectory sample times (relative to current time)
    std::vector<float> trajectorySampleTimes = {-0.2f, -0.1f, 0.1f, 0.2f, 0.4f, 0.6f};

    // Continuing pose cost bias (Unreal-style: negative = prefer continuing)
    float continuingPoseCostBias = -0.3f;

    // Looping animation bias
    float loopingCostBias = -0.1f;

    // Strafe mode: when enabled, heading channel is weighted heavily
    bool strafeMode = false;
    float strafeFacingWeight = 2.0f;  // Extra weight when in strafe mode

    // Default locomotion configuration
    static FeatureConfig locomotion() {
        FeatureConfig config;
        config.featureBoneNames = {
            FeatureBones::LEFT_FOOT,
            FeatureBones::RIGHT_FOOT,
            FeatureBones::HIPS
        };
        return config;
    }

    // Locomotion with strafe support
    static FeatureConfig locomotionWithStrafe() {
        FeatureConfig config;
        config.featureBoneNames = {
            FeatureBones::LEFT_FOOT,
            FeatureBones::RIGHT_FOOT,
            FeatureBones::HIPS
        };
        config.headingWeight = 1.5f;  // Enable heading channel
        config.headingBoneName = "Hips";
        config.headingAxis = HeadingAxis::Z;
        config.headingComponentStrip = ComponentStrip::StripY;
        return config;
    }

    // Full body configuration
    static FeatureConfig fullBody() {
        FeatureConfig config;
        config.featureBoneNames = {
            FeatureBones::LEFT_FOOT,
            FeatureBones::RIGHT_FOOT,
            FeatureBones::LEFT_HAND,
            FeatureBones::RIGHT_HAND,
            FeatureBones::HIPS,
            FeatureBones::SPINE
        };
        return config;
    }

    // Create from PoseSearchSchema
    static FeatureConfig fromSchema(const PoseSearchSchema& schema) {
        FeatureConfig config;

        // Extract from trajectory channel
        if (const auto* trajCh = schema.getChannel("Trajectory")) {
            config.trajectoryWeight = trajCh->weight;
            config.trajectorySampleTimes = trajCh->sampleTimes;
        }

        // Extract from pose channel
        if (const auto* poseCh = schema.getChannel("Pose")) {
            config.poseWeight = poseCh->weight;
            config.featureBoneNames = poseCh->boneNames;
        }

        // Extract from heading channel
        if (const auto* headingCh = schema.getChannel("Heading")) {
            config.headingWeight = headingCh->weight;
            config.headingBoneName = headingCh->headingBoneName;
            config.headingAxis = headingCh->headingAxis;
            config.headingComponentStrip = headingCh->componentStrip;
        }

        // Copy schema-level settings
        config.continuingPoseCostBias = schema.continuingPoseCostBias;
        config.loopingCostBias = schema.loopingCostBias;
        config.strafeMode = schema.strafeMode;
        config.strafeFacingWeight = schema.strafeFacingWeight;

        return config;
    }
};

// Feature extractor - extracts features from animation poses
class FeatureExtractor {
public:
    FeatureExtractor() = default;

    // Initialize with skeleton and configuration
    void initialize(const Skeleton& skeleton, const FeatureConfig& config);

    // Extract features from a pose at a specific time
    PoseFeatures extractFromPose(const Skeleton& skeleton,
                                  const SkeletonPose& pose,
                                  const SkeletonPose& prevPose,
                                  float deltaTime) const;

    // Extract features from an animation clip at a specific time
    PoseFeatures extractFromClip(const AnimationClip& clip,
                                  const Skeleton& skeleton,
                                  float time,
                                  float deltaTime = 1.0f / 60.0f) const;

    // Extract trajectory from an animation clip
    Trajectory extractTrajectoryFromClip(const AnimationClip& clip,
                                          const Skeleton& skeleton,
                                          float currentTime) const;

    // Extract heading feature from a pose (for strafe queries)
    HeadingFeature extractHeadingFromPose(const Skeleton& skeleton,
                                           const SkeletonPose& pose,
                                           const glm::vec3& movementDirection = glm::vec3(0.0f)) const;

    // Set strafe mode (affects how heading is computed)
    void setStrafeMode(bool enabled) { strafeMode_ = enabled; }
    bool isStrafeMode() const { return strafeMode_; }

    bool isInitialized() const { return initialized_; }
    const FeatureConfig& getConfig() const { return config_; }

private:
    FeatureConfig config_;
    std::vector<int32_t> featureBoneIndices_;
    int32_t rootBoneIndex_ = -1;
    int32_t headingBoneIndex_ = -1;  // For heading channel
    bool initialized_ = false;
    bool strafeMode_ = false;

    // Compute bone position in character space
    glm::vec3 computeBonePosition(const Skeleton& skeleton,
                                   const SkeletonPose& pose,
                                   int32_t boneIndex) const;

    // Compute root transform from pose
    glm::mat4 computeRootTransform(const Skeleton& skeleton,
                                    const SkeletonPose& pose) const;

    // Compute bone world transform
    glm::mat4 computeBoneWorldTransform(const Skeleton& skeleton,
                                         const SkeletonPose& pose,
                                         int32_t boneIndex) const;

    // Extract heading direction from bone transform
    glm::vec3 extractHeadingDirection(const glm::mat4& boneTransform,
                                       HeadingAxis axis,
                                       ComponentStrip strip) const;
};

} // namespace MotionMatching
