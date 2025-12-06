#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <optional>
#include <functional>

#include "GLTFLoader.h"

// Joint rotation limits (in radians)
struct JointLimits {
    glm::vec3 minAngles = glm::vec3(-glm::pi<float>());
    glm::vec3 maxAngles = glm::vec3(glm::pi<float>());
    bool enabled = false;
};

// Two-bone IK chain definition (for arms/legs)
struct TwoBoneIKChain {
    int32_t rootBoneIndex = -1;     // Upper arm / thigh
    int32_t midBoneIndex = -1;      // Forearm / shin
    int32_t endBoneIndex = -1;      // Hand / foot

    glm::vec3 targetPosition = glm::vec3(0.0f);
    glm::vec3 poleVector = glm::vec3(0.0f, 0.0f, 1.0f);  // Controls elbow/knee direction
    float weight = 1.0f;            // Blend weight (0 = animation only, 1 = full IK)
    bool enabled = false;

    // Joint limits for mid bone (elbow/knee)
    JointLimits midBoneLimits;
};

// Debug visualization data for IK chains
struct IKDebugData {
    struct Chain {
        glm::vec3 rootPos;
        glm::vec3 midPos;
        glm::vec3 endPos;
        glm::vec3 targetPos;
        glm::vec3 polePos;
        bool active;
    };
    std::vector<Chain> chains;

    struct LookAt {
        glm::vec3 headPos;
        glm::vec3 targetPos;
        glm::vec3 forward;
        bool active;
    };
    std::vector<LookAt> lookAtTargets;

    struct FootPlacement {
        glm::vec3 footPos;
        glm::vec3 groundPos;
        glm::vec3 normal;
        bool active;
    };
    std::vector<FootPlacement> footPlacements;
};

// Look-At IK definition (for head/eye tracking)
struct LookAtIK {
    int32_t headBoneIndex = -1;       // Head bone to rotate
    int32_t neckBoneIndex = -1;       // Optional neck bone for smoother rotation
    int32_t spineBoneIndex = -1;      // Optional spine for full body turn

    glm::vec3 targetPosition = glm::vec3(0.0f);
    glm::vec3 eyeOffset = glm::vec3(0.0f, 0.1f, 0.1f);  // Offset from head bone to "eyes"

    // Distribution of rotation across bones (should sum to ~1.0)
    float headWeight = 0.6f;          // How much head contributes
    float neckWeight = 0.3f;          // How much neck contributes
    float spineWeight = 0.1f;         // How much spine contributes

    // Angular limits (radians)
    float maxYawAngle = glm::radians(80.0f);    // Left/right limit
    float maxPitchAngle = glm::radians(60.0f);  // Up/down limit

    float weight = 1.0f;              // Overall blend weight
    float smoothSpeed = 8.0f;         // Interpolation speed (0 = instant)
    bool enabled = false;

    // Internal state for smooth interpolation
    glm::quat currentHeadRotation = glm::quat(1, 0, 0, 0);
    glm::quat currentNeckRotation = glm::quat(1, 0, 0, 0);
    glm::quat currentSpineRotation = glm::quat(1, 0, 0, 0);
};

// Ground query result for foot placement
struct GroundQueryResult {
    glm::vec3 position;      // Hit position on ground
    glm::vec3 normal;        // Surface normal at hit
    float distance;          // Distance from query origin
    bool hit;                // Whether ground was found
};

// Callback for ground height queries
// Takes world position (x, z) and returns ground height and normal
using GroundQueryFunc = std::function<GroundQueryResult(const glm::vec3& position, float maxDistance)>;

// Foot Placement IK definition
struct FootPlacementIK {
    // Leg chain indices (uses TwoBoneIK internally)
    int32_t hipBoneIndex = -1;        // Thigh/upper leg
    int32_t kneeBoneIndex = -1;       // Shin/lower leg
    int32_t footBoneIndex = -1;       // Foot/ankle
    int32_t toeBoneIndex = -1;        // Optional toe bone for ground alignment

    // Foot dimensions for ground probing
    glm::vec3 footOffset = glm::vec3(0.0f, -0.05f, 0.0f);  // Offset from ankle to sole
    float footLength = 0.2f;          // Foot length for toe raycast
    float raycastHeight = 0.5f;       // How high above foot to start raycast
    float raycastDistance = 1.0f;     // How far down to check for ground

    // IK settings
    glm::vec3 poleVector = glm::vec3(0.0f, 0.0f, 1.0f);  // Knee direction
    float weight = 1.0f;
    bool enabled = false;

    // Foot rotation alignment
    bool alignToGround = true;        // Rotate foot to match ground slope
    float maxFootAngle = glm::radians(45.0f);  // Max foot rotation

    // Internal state
    glm::vec3 currentFootTarget = glm::vec3(0.0f);
    glm::quat currentFootRotation = glm::quat(1, 0, 0, 0);
    float currentGroundHeight = 0.0f;
    bool isGrounded = false;

    // Foot locking state (prevents sliding during idle)
    glm::vec3 lockedWorldPosition = glm::vec3(0.0f);  // World position where foot is locked
    bool isLocked = false;                             // Whether foot is currently locked in place
    float lockBlend = 0.0f;                            // Blend factor toward locked position (0-1)
};

// Pelvis adjustment for foot placement
struct PelvisAdjustment {
    int32_t pelvisBoneIndex = -1;     // Hips/pelvis bone
    float minOffset = -0.3f;          // Max downward adjustment
    float maxOffset = 0.1f;           // Max upward adjustment
    float smoothSpeed = 5.0f;         // Interpolation speed
    bool enabled = false;

    // Internal state
    float currentOffset = 0.0f;
};

// Straddling IK - for stepping onto elevated surfaces (boxes, stairs, etc.)
struct StraddleIK {
    int32_t pelvisBoneIndex = -1;     // Hips bone for tilt
    int32_t spineBaseBoneIndex = -1;  // Lower spine for counter-rotation

    // Height difference thresholds
    float minHeightDiff = 0.05f;      // Minimum height diff to trigger straddle
    float maxHeightDiff = 0.6f;       // Maximum supported height difference
    float maxStepHeight = 0.5f;       // Max height character can step up

    // Hip tilt settings
    float maxHipTilt = glm::radians(15.0f);   // Max hip rotation angle
    float maxHipShift = 0.15f;        // Max lateral hip shift toward higher foot
    float tiltSmoothSpeed = 6.0f;     // Interpolation speed

    // Weight distribution (0 = left foot, 1 = right foot)
    float weightBalance = 0.5f;       // Current weight distribution
    float targetWeightBalance = 0.5f;

    float weight = 1.0f;
    bool enabled = false;

    // Internal state
    float currentHipTilt = 0.0f;      // Current hip tilt angle (roll)
    float currentHipShift = 0.0f;     // Current lateral shift
    glm::quat currentSpineCompensation = glm::quat(1, 0, 0, 0);
    float leftFootHeight = 0.0f;
    float rightFootHeight = 0.0f;
};

// Hand hold for climbing
struct HandHold {
    glm::vec3 position = glm::vec3(0.0f);     // World position of hold
    glm::vec3 normal = glm::vec3(0, 0, -1);   // Surface normal (facing away from wall)
    glm::vec3 gripDirection = glm::vec3(0, -1, 0);  // Direction fingers wrap
    float radius = 0.05f;             // Size of hold
    bool isValid = false;             // Whether this hold is active
};

// Foot hold for climbing
struct FootHold {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0, 0, -1);
    float radius = 0.1f;
    bool isValid = false;
};

// Climbing surface query result
struct ClimbSurfaceQuery {
    glm::vec3 surfacePoint;           // Nearest point on climbable surface
    glm::vec3 surfaceNormal;          // Surface normal at that point
    float distance;                   // Distance from query point
    bool isClimbable;                 // Whether surface can be climbed
};

// Callback for finding climbable surfaces
using ClimbSurfaceQueryFunc = std::function<ClimbSurfaceQuery(const glm::vec3& position, float maxDistance)>;

// Climbing IK - for climbing walls, ledges, ladders
struct ClimbingIK {
    // Bone indices
    int32_t pelvisBoneIndex = -1;
    int32_t spineBaseBoneIndex = -1;
    int32_t spineMidBoneIndex = -1;
    int32_t chestBoneIndex = -1;

    // Arm chains (indices into IKSystem's two-bone chains)
    int32_t leftArmChainIndex = -1;
    int32_t rightArmChainIndex = -1;

    // Leg chains
    int32_t leftLegChainIndex = -1;
    int32_t rightLegChainIndex = -1;

    // Hand bone indices for grip rotation
    int32_t leftHandBoneIndex = -1;
    int32_t rightHandBoneIndex = -1;

    // Current holds
    HandHold leftHandHold;
    HandHold rightHandHold;
    FootHold leftFootHold;
    FootHold rightFootHold;

    // Body positioning
    float wallDistance = 0.3f;        // How far body stays from wall
    float bodyLean = 0.0f;            // Lean toward wall (-1 to 1)
    glm::vec3 wallNormal = glm::vec3(0, 0, -1);  // Current wall facing direction

    // Reach settings
    float maxArmReach = 0.8f;         // Maximum arm extension
    float maxLegReach = 0.9f;         // Maximum leg extension
    float comfortArmReach = 0.5f;     // Comfortable reaching distance
    float comfortLegReach = 0.6f;

    // Transition settings
    float transitionSpeed = 4.0f;     // Speed of transitioning into/out of climb
    float currentTransition = 0.0f;   // 0 = not climbing, 1 = fully climbing

    float weight = 1.0f;
    bool enabled = false;

    // Internal state
    glm::vec3 targetBodyPosition = glm::vec3(0.0f);
    glm::quat targetBodyRotation = glm::quat(1, 0, 0, 0);
    glm::vec3 currentBodyPosition = glm::vec3(0.0f);
    glm::quat currentBodyRotation = glm::quat(1, 0, 0, 0);
};

// Two-Bone IK Solver
// Uses analytical solution for exactly 2 bones (arm or leg)
// Algorithm: Law of cosines to find joint angles, pole vector for bend direction
class TwoBoneIKSolver {
public:
    // Solve IK for a two-bone chain
    // Modifies skeleton joint transforms in place
    // Returns true if target was reachable
    static bool solve(
        Skeleton& skeleton,
        const TwoBoneIKChain& chain,
        const std::vector<glm::mat4>& globalTransforms
    );

    // Solve with blend weight (interpolates between animation and IK result)
    static bool solveBlended(
        Skeleton& skeleton,
        const TwoBoneIKChain& chain,
        const std::vector<glm::mat4>& globalTransforms,
        float weight
    );

private:
    // Apply joint limits to a rotation
    static glm::quat applyJointLimits(const glm::quat& rotation, const JointLimits& limits);

    // Calculate angle between two vectors
    static float angleBetween(const glm::vec3& a, const glm::vec3& b);
};

// Look-At IK Solver
// Rotates head/neck/spine to look at a target
class LookAtIKSolver {
public:
    // Solve look-at IK
    // deltaTime is used for smooth interpolation
    static void solve(
        Skeleton& skeleton,
        LookAtIK& lookAt,
        const std::vector<glm::mat4>& globalTransforms,
        float deltaTime
    );

    // Calculate the look direction from a bone to target
    static glm::vec3 getLookDirection(
        const glm::mat4& boneGlobalTransform,
        const glm::vec3& targetPosition,
        const glm::vec3& eyeOffset
    );

private:
    // Clamp rotation to yaw/pitch limits
    static glm::quat clampLookRotation(
        const glm::quat& rotation,
        float maxYaw,
        float maxPitch
    );

    // Apply rotation to a single bone
    static void applyBoneRotation(
        Joint& joint,
        const glm::quat& targetRotation,
        const glm::mat4& parentGlobalTransform,
        float weight
    );
};

// Foot Placement IK Solver
// Plants feet on uneven terrain using ground queries
class FootPlacementIKSolver {
public:
    // Solve foot placement for a single foot
    // Uses GroundQueryFunc to probe terrain height
    static void solve(
        Skeleton& skeleton,
        FootPlacementIK& foot,
        const std::vector<glm::mat4>& globalTransforms,
        const GroundQueryFunc& groundQuery,
        const glm::mat4& characterTransform,
        float deltaTime
    );

    // Calculate required pelvis offset to keep both feet grounded
    static float calculatePelvisOffset(
        const FootPlacementIK& leftFoot,
        const FootPlacementIK& rightFoot,
        float currentPelvisHeight
    );

    // Apply pelvis height adjustment
    static void applyPelvisAdjustment(
        Skeleton& skeleton,
        PelvisAdjustment& pelvis,
        float targetOffset,
        float deltaTime
    );

private:
    // Align foot rotation to ground normal
    static glm::quat alignFootToGround(
        const glm::vec3& groundNormal,
        const glm::quat& currentRotation,
        float maxAngle
    );
};

// Straddle IK Solver
// Handles hip tilt and weight distribution when feet are at different heights
class StraddleIKSolver {
public:
    // Solve straddling based on foot height difference
    static void solve(
        Skeleton& skeleton,
        StraddleIK& straddle,
        const FootPlacementIK* leftFoot,
        const FootPlacementIK* rightFoot,
        const std::vector<glm::mat4>& globalTransforms,
        float deltaTime
    );

    // Calculate hip tilt angle from foot height difference
    static float calculateHipTilt(
        float leftFootHeight,
        float rightFootHeight,
        float maxTilt,
        float maxHeightDiff
    );

    // Calculate weight distribution based on foot positions
    static float calculateWeightBalance(
        float leftFootHeight,
        float rightFootHeight,
        float characterVelocityX  // Lateral velocity affects weight shift
    );

private:
    // Apply hip tilt rotation
    static void applyHipTilt(
        Joint& pelvisJoint,
        float tiltAngle,
        float lateralShift,
        const glm::mat4& parentGlobalTransform
    );

    // Apply spine counter-rotation to keep upper body upright
    static void applySpineCompensation(
        Joint& spineJoint,
        float compensationAngle,
        const glm::mat4& parentGlobalTransform
    );
};

// Climbing IK Solver
// Coordinates limbs for climbing walls, ledges, and ladders
class ClimbingIKSolver {
public:
    // Main solve function - positions body and limbs for climbing
    static void solve(
        Skeleton& skeleton,
        ClimbingIK& climbing,
        std::vector<TwoBoneIKChain>& armChains,
        std::vector<TwoBoneIKChain>& legChains,
        const std::vector<glm::mat4>& globalTransforms,
        const glm::mat4& characterTransform,
        float deltaTime
    );

    // Calculate optimal body position given current holds
    static glm::vec3 calculateBodyPosition(
        const ClimbingIK& climbing,
        const glm::mat4& characterTransform
    );

    // Calculate body rotation to face wall
    static glm::quat calculateBodyRotation(
        const glm::vec3& wallNormal,
        const glm::vec3& upVector
    );

    // Set a hand hold position
    static void setHandHold(
        ClimbingIK& climbing,
        bool isLeft,
        const glm::vec3& position,
        const glm::vec3& normal,
        const glm::vec3& gripDir
    );

    // Set a foot hold position
    static void setFootHold(
        ClimbingIK& climbing,
        bool isLeft,
        const glm::vec3& position,
        const glm::vec3& normal
    );

    // Clear a hold (limb returns to default position)
    static void clearHandHold(ClimbingIK& climbing, bool isLeft);
    static void clearFootHold(ClimbingIK& climbing, bool isLeft);

    // Check if a reach is possible from current body position
    static bool canReach(
        const ClimbingIK& climbing,
        const glm::vec3& holdPosition,
        bool isArm,
        bool isLeft,
        const std::vector<glm::mat4>& globalTransforms
    );

private:
    // Position body relative to wall
    static void positionBody(
        Skeleton& skeleton,
        ClimbingIK& climbing,
        const std::vector<glm::mat4>& globalTransforms,
        float deltaTime
    );

    // Orient hand to grip hold
    static void orientHandToHold(
        Joint& handJoint,
        const HandHold& hold,
        const glm::mat4& parentGlobalTransform
    );

    // Orient foot to contact hold
    static void orientFootToHold(
        Joint& footJoint,
        const FootHold& hold,
        const glm::mat4& parentGlobalTransform
    );
};

// IK System - manages multiple IK chains for a character
class IKSystem {
public:
    IKSystem() = default;

    // ========== Two-Bone IK (Arms/Legs) ==========

    // Setup chains by bone names
    bool addTwoBoneChain(
        const std::string& name,
        const Skeleton& skeleton,
        const std::string& rootBoneName,
        const std::string& midBoneName,
        const std::string& endBoneName
    );

    // Get chain by name for modification
    TwoBoneIKChain* getChain(const std::string& name);
    const TwoBoneIKChain* getChain(const std::string& name) const;

    // Set target for a chain
    void setTarget(const std::string& chainName, const glm::vec3& target);
    void setPoleVector(const std::string& chainName, const glm::vec3& pole);
    void setWeight(const std::string& chainName, float weight);
    void setEnabled(const std::string& chainName, bool enabled);

    // ========== Look-At IK ==========

    // Setup look-at by bone names
    bool setupLookAt(
        const Skeleton& skeleton,
        const std::string& headBoneName,
        const std::string& neckBoneName = "",
        const std::string& spineBoneName = ""
    );

    // Get look-at for modification
    LookAtIK* getLookAt() { return lookAtEnabled ? &lookAt : nullptr; }
    const LookAtIK* getLookAt() const { return lookAtEnabled ? &lookAt : nullptr; }

    // Set look-at target
    void setLookAtTarget(const glm::vec3& target);
    void setLookAtWeight(float weight);
    void setLookAtEnabled(bool enabled);

    // ========== Foot Placement IK ==========

    // Setup foot placement by bone names
    bool addFootPlacement(
        const std::string& name,
        const Skeleton& skeleton,
        const std::string& hipBoneName,
        const std::string& kneeBoneName,
        const std::string& footBoneName,
        const std::string& toeBoneName = ""
    );

    // Setup pelvis adjustment
    bool setupPelvisAdjustment(
        const Skeleton& skeleton,
        const std::string& pelvisBoneName
    );

    // Get foot placement by name
    FootPlacementIK* getFootPlacement(const std::string& name);
    const FootPlacementIK* getFootPlacement(const std::string& name) const;

    // Get pelvis adjustment
    PelvisAdjustment* getPelvisAdjustment() { return &pelvisAdjustment; }

    // Set ground query function for foot placement
    void setGroundQueryFunc(const GroundQueryFunc& func) { groundQuery = func; }

    // Enable/disable foot placement
    void setFootPlacementEnabled(const std::string& name, bool enabled);
    void setFootPlacementWeight(const std::string& name, float weight);

    // Reset all foot locks (call when character is teleported or position changes significantly)
    void resetFootLocks();

    // ========== Straddling IK (stepping on boxes) ==========

    // Setup straddling by bone names
    bool setupStraddle(
        const Skeleton& skeleton,
        const std::string& pelvisBoneName,
        const std::string& spineBaseBoneName = ""
    );

    // Get straddle for modification
    StraddleIK* getStraddle() { return straddleEnabled ? &straddle : nullptr; }
    const StraddleIK* getStraddle() const { return straddleEnabled ? &straddle : nullptr; }

    // Enable/disable straddling
    void setStraddleEnabled(bool enabled);
    void setStraddleWeight(float weight);

    // ========== Climbing IK ==========

    // Setup climbing by bone names
    bool setupClimbing(
        const Skeleton& skeleton,
        const std::string& pelvisBoneName,
        const std::string& spineBaseBoneName,
        const std::string& spineMidBoneName = "",
        const std::string& chestBoneName = ""
    );

    // Link arm/leg chains to climbing system
    void setClimbingArmChains(const std::string& leftArmChainName, const std::string& rightArmChainName);
    void setClimbingLegChains(const std::string& leftLegChainName, const std::string& rightLegChainName);

    // Set hand bone indices for grip orientation
    void setClimbingHandBones(
        const Skeleton& skeleton,
        const std::string& leftHandBoneName,
        const std::string& rightHandBoneName
    );

    // Get climbing for modification
    ClimbingIK* getClimbing() { return climbingEnabled ? &climbing : nullptr; }
    const ClimbingIK* getClimbing() const { return climbingEnabled ? &climbing : nullptr; }

    // Climbing hold control
    void setClimbingHandHold(bool isLeft, const glm::vec3& position, const glm::vec3& normal, const glm::vec3& gripDir);
    void setClimbingFootHold(bool isLeft, const glm::vec3& position, const glm::vec3& normal);
    void clearClimbingHandHold(bool isLeft);
    void clearClimbingFootHold(bool isLeft);

    // Enable/disable climbing
    void setClimbingEnabled(bool enabled);
    void setClimbingWeight(float weight);
    void setClimbingWallNormal(const glm::vec3& normal);

    // ========== Solving ==========

    // Solve all enabled IK chains
    // Call this after animation sampling, before computing bone matrices
    void solve(Skeleton& skeleton, float deltaTime = 0.016f);

    // Solve with character transform (needed for foot placement world queries)
    void solve(Skeleton& skeleton, const glm::mat4& characterTransform, float deltaTime = 0.016f);

    // Get debug visualization data
    IKDebugData getDebugData(const Skeleton& skeleton) const;

    // Clear all chains
    void clear();

    // Check if any chains are enabled
    bool hasEnabledChains() const;

private:
    // Two-bone chains
    struct NamedChain {
        std::string name;
        TwoBoneIKChain chain;
    };
    std::vector<NamedChain> chains;

    // Look-at IK
    LookAtIK lookAt;
    bool lookAtEnabled = false;

    // Foot placement
    struct NamedFootPlacement {
        std::string name;
        FootPlacementIK foot;
    };
    std::vector<NamedFootPlacement> footPlacements;
    PelvisAdjustment pelvisAdjustment;
    GroundQueryFunc groundQuery;

    // Straddling IK
    StraddleIK straddle;
    bool straddleEnabled = false;

    // Climbing IK
    ClimbingIK climbing;
    bool climbingEnabled = false;
    std::string leftArmChainName;
    std::string rightArmChainName;
    std::string leftLegChainName;
    std::string rightLegChainName;

    // Cached global transforms (computed once per solve)
    mutable std::vector<glm::mat4> cachedGlobalTransforms;
};

// Helper functions for extracting transform components
namespace IKUtils {
    // Decompose a matrix into translation, rotation, scale
    void decomposeTransform(
        const glm::mat4& transform,
        glm::vec3& translation,
        glm::quat& rotation,
        glm::vec3& scale
    );

    // Compose a matrix from components
    glm::mat4 composeTransform(
        const glm::vec3& translation,
        const glm::quat& rotation,
        const glm::vec3& scale
    );

    // Get world position from global transform
    glm::vec3 getWorldPosition(const glm::mat4& globalTransform);

    // Calculate bone length from global transforms
    float getBoneLength(
        const std::vector<glm::mat4>& globalTransforms,
        int32_t boneIndex,
        int32_t childBoneIndex
    );

    // Rotate a bone to point toward a target position
    glm::quat aimAt(
        const glm::vec3& currentDir,
        const glm::vec3& targetDir,
        const glm::vec3& upHint
    );
}
