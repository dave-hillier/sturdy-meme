#include <doctest/doctest.h>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Jolt stubs — provide complete types before CharacterController.h is included,
// so unique_ptr<JPH::CharacterVirtual> can compile without the real Jolt headers.
namespace JPH {
    class CharacterVirtual {};
    class PhysicsSystem {};
    class TempAllocatorImpl {};
}

#include "GLTFLoader.h"
#include "CharacterController.h"
#include "ml/CALMCharacterConfig.h"
#include "ml/CALMObservation.h"
#include "ml/CALMActionApplier.h"
#include "ml/Tensor.h"

// Stub CharacterController member functions for testing (avoids linking Jolt)
CharacterController::CharacterController() = default;
CharacterController::~CharacterController() = default;
CharacterController::CharacterController(CharacterController&&) noexcept = default;
CharacterController& CharacterController::operator=(CharacterController&&) noexcept = default;

bool CharacterController::create(JPH::PhysicsSystem*, const glm::vec3&, float, float) { return true; }
void CharacterController::update(float, JPH::PhysicsSystem*, JPH::TempAllocatorImpl*) {}
void CharacterController::setInput(const glm::vec3&, bool) {}
void CharacterController::setPosition(const glm::vec3& p) { desiredVelocity_ = p; }
glm::vec3 CharacterController::getPosition() const { return glm::vec3(0.0f, 1.0f, 0.0f); }
glm::vec3 CharacterController::getVelocity() const { return desiredVelocity_; }
bool CharacterController::isOnGround() const { return true; }

// ---------------------------------------------------------------------------
// Helper: build a minimal test skeleton with standard humanoid bones
// ---------------------------------------------------------------------------
static Skeleton makeTestSkeleton() {
    Skeleton skel;
    // Build a minimal humanoid skeleton with the bones CALM expects
    auto addJoint = [&](const std::string& boneName, int32_t parent) -> int32_t {
        Joint jnt;
        jnt.name = boneName;
        jnt.parentIndex = parent;
        jnt.inverseBindMatrix = glm::mat4(1.0f);
        jnt.localTransform = glm::mat4(1.0f);
        jnt.preRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        int32_t idx = static_cast<int32_t>(skel.joints.size());
        skel.joints.push_back(jnt);
        return idx;
    };

    int32_t hips = addJoint("Hips", -1);
    int32_t spine = addJoint("Spine", hips);
    int32_t spine1 = addJoint("Spine1", spine);
    int32_t neck = addJoint("Neck", spine1);
    int32_t head = addJoint("Head", neck);

    int32_t rArm = addJoint("RightArm", spine1);
    int32_t rForeArm = addJoint("RightForeArm", rArm);
    int32_t rHand = addJoint("RightHand", rForeArm);

    int32_t lArm = addJoint("LeftArm", spine1);
    int32_t lForeArm = addJoint("LeftForeArm", lArm);
    int32_t lHand = addJoint("LeftHand", lForeArm);

    int32_t rUpLeg = addJoint("RightUpLeg", hips);
    int32_t rLeg = addJoint("RightLeg", rUpLeg);
    int32_t rFoot = addJoint("RightFoot", rLeg);

    int32_t lUpLeg = addJoint("LeftUpLeg", hips);
    int32_t lLeg = addJoint("LeftLeg", lUpLeg);
    int32_t lFoot = addJoint("LeftFoot", lLeg);

    // Suppress unused variable warnings
    (void)head; (void)rHand; (void)lHand; (void)rFoot; (void)lFoot;

    skel.buildHierarchy();
    return skel;
}

// ---------------------------------------------------------------------------
// CALMCharacterConfig tests
// ---------------------------------------------------------------------------

TEST_SUITE("CALMCharacterConfig") {
    TEST_CASE("buildFromSkeleton finds standard humanoid bones") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);

        // Should have found DOFs for the standard bones
        CHECK(config.actionDim > 0);
        CHECK(config.observationDim > 0);
        CHECK(config.dofMappings.size() == static_cast<size_t>(config.actionDim));

        // Key bodies: head, right_hand, left_hand, right_foot, left_foot = 5
        CHECK(config.keyBodies.size() == 5);

        // Root should be Hips (index 0)
        CHECK(config.rootJointIndex == 0);
    }

    TEST_CASE("observation dim matches expected formula") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);

        // obs = root_h(1) + root_rot(6) + root_vel(3) + root_ang_vel(3)
        //     + dof_pos(N) + dof_vel(N) + key_body_pos(K*3)
        int expected = 1 + 6 + 3 + 3 + config.actionDim * 2
                       + static_cast<int>(config.keyBodies.size()) * 3;
        CHECK(config.observationDim == expected);
    }

    TEST_CASE("buildFromNameMap with partial mapping") {
        Skeleton skel = makeTestSkeleton();
        std::unordered_map<std::string, std::string> nameMap = {
            {"pelvis", "Hips"},
            {"head", "Head"},
            {"right_foot", "RightFoot"},
            {"left_foot", "LeftFoot"},
        };
        auto config = ml::CALMCharacterConfig::buildFromNameMap(skel, nameMap);

        // pelvis(3) + head(3) + right_foot(3) + left_foot(3) = 12
        CHECK(config.actionDim == 12);
        CHECK(config.keyBodies.size() == 3); // head + right_foot + left_foot
    }

    TEST_CASE("DOF mappings have valid joint indices") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);

        for (const auto& dof : config.dofMappings) {
            CHECK(dof.jointIndex >= 0);
            CHECK(static_cast<size_t>(dof.jointIndex) < skel.joints.size());
            CHECK(dof.axis >= 0);
            CHECK(dof.axis <= 2);
        }
    }
}

// ---------------------------------------------------------------------------
// CALMObservationExtractor tests
// ---------------------------------------------------------------------------

TEST_SUITE("CALMObservationExtractor") {
    TEST_CASE("produces correct observation dimension") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);
        ml::CALMObservationExtractor extractor(config);

        CharacterController controller;

        extractor.extractFrame(skel, controller, 1.0f / 30.0f);
        ml::Tensor obs = extractor.getCurrentObs();

        CHECK(static_cast<int>(obs.size()) == config.observationDim);
    }

    TEST_CASE("root height appears in first element") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);
        ml::CALMObservationExtractor extractor(config);

        CharacterController controller;
        // getPosition() returns (0, 1, 0) in our stub

        extractor.extractFrame(skel, controller, 1.0f / 30.0f);
        ml::Tensor obs = extractor.getCurrentObs();

        // First element should be root height = 1.0
        CHECK(obs[0] == doctest::Approx(1.0f));
    }

    TEST_CASE("stacked observations have correct dimension") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);
        ml::CALMObservationExtractor extractor(config);

        CharacterController controller;

        // Extract several frames
        for (int i = 0; i < 5; ++i) {
            extractor.extractFrame(skel, controller, 1.0f / 30.0f);
        }

        ml::Tensor stacked = extractor.getStackedObs(3);
        CHECK(static_cast<int>(stacked.size()) == 3 * config.observationDim);

        ml::Tensor encoder = extractor.getEncoderObs();
        CHECK(static_cast<int>(encoder.size()) == config.numAMPEncObsSteps * config.observationDim);
    }

    TEST_CASE("reset clears history") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);
        ml::CALMObservationExtractor extractor(config);

        CharacterController controller;
        extractor.extractFrame(skel, controller, 1.0f / 30.0f);

        extractor.reset();

        // After reset, getCurrentObs returns zeros
        ml::Tensor obs = extractor.getCurrentObs();
        CHECK(static_cast<int>(obs.size()) == config.observationDim);
        // All zeros since no frames extracted after reset
        float sum = 0.0f;
        for (size_t i = 0; i < obs.size(); ++i) sum += std::abs(obs[i]);
        CHECK(sum == doctest::Approx(0.0f));
    }

    TEST_CASE("velocity features are zero on first frame") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);
        ml::CALMObservationExtractor extractor(config);

        CharacterController controller;
        extractor.extractFrame(skel, controller, 1.0f / 30.0f);
        ml::Tensor obs = extractor.getCurrentObs();

        // Angular velocity (indices 10,11,12) should be zero on first frame
        CHECK(obs[10] == doctest::Approx(0.0f));
        CHECK(obs[11] == doctest::Approx(0.0f));
        CHECK(obs[12] == doctest::Approx(0.0f));
    }
}

// ---------------------------------------------------------------------------
// CALMActionApplier tests
// ---------------------------------------------------------------------------

TEST_SUITE("CALMActionApplier") {
    TEST_CASE("produces pose with correct bone count") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);
        ml::CALMActionApplier applier(config);

        ml::Tensor actions(config.actionDim);
        actions.fill(0.0f); // Zero angles = identity-ish

        SkeletonPose pose;
        applier.applyToSkeleton(actions, skel, pose);

        CHECK(pose.size() == skel.joints.size());
    }

    TEST_CASE("zero actions produce near-identity rotations") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);
        ml::CALMActionApplier applier(config);

        ml::Tensor actions(config.actionDim);
        actions.fill(0.0f);

        SkeletonPose pose;
        applier.applyToSkeleton(actions, skel, pose);

        // All CALM-controlled joints should have identity-ish rotation
        for (const auto& dof : config.dofMappings) {
            const auto& bone = pose[dof.jointIndex];
            // Quaternion should be near identity (w ≈ 1)
            CHECK(bone.rotation.w == doctest::Approx(1.0f).epsilon(0.01));
        }
    }

    TEST_CASE("non-zero action rotates joint") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);
        ml::CALMActionApplier applier(config);

        ml::Tensor actions(config.actionDim);
        actions.fill(0.0f);
        // Set first DOF to 90 degrees (pi/2)
        actions[0] = 1.5707963f;

        SkeletonPose pose;
        applier.applyToSkeleton(actions, skel, pose);

        int32_t jointIdx = config.dofMappings[0].jointIndex;
        const auto& bone = pose[jointIdx];
        // Rotation should no longer be identity
        CHECK(bone.rotation.w != doctest::Approx(1.0f).epsilon(0.01));
    }

    TEST_CASE("clampActions respects joint limits") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);
        ml::CALMActionApplier applier(config);

        ml::Tensor actions(config.actionDim);
        actions.fill(100.0f); // Way beyond limits

        applier.clampActions(actions);

        for (int d = 0; d < config.actionDim; ++d) {
            CHECK(actions[d] <= config.dofMappings[d].rangeMax);
            CHECK(actions[d] >= config.dofMappings[d].rangeMin);
        }
    }

    TEST_CASE("blended with weight 0 returns base pose") {
        Skeleton skel = makeTestSkeleton();
        auto config = ml::CALMCharacterConfig::buildFromSkeleton(skel);
        ml::CALMActionApplier applier(config);

        // Create a base pose with known rotations
        SkeletonPose basePose;
        basePose.resize(skel.joints.size());
        for (size_t j = 0; j < skel.joints.size(); ++j) {
            basePose[j] = BonePose::identity();
            basePose[j].translation = glm::vec3(static_cast<float>(j), 0, 0);
        }

        ml::Tensor actions(config.actionDim);
        actions.fill(1.0f); // Non-zero actions

        SkeletonPose blended;
        applier.applyBlended(actions, skel, basePose, 0.0f, blended);

        // With weight 0, should be base pose
        for (size_t j = 0; j < skel.joints.size(); ++j) {
            CHECK(blended[j].translation.x == doctest::Approx(basePose[j].translation.x));
        }
    }
}
