#include <doctest/doctest.h>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Jolt stubs — must come before CharacterController.h
namespace JPH {
    class CharacterVirtual {};
    class PhysicsSystem {};
    class TempAllocatorImpl {};
}

#include "GLTFLoader.h"
#include "CharacterController.h"
#include "ml/LatentSpace.h"
#include "ml/calm/LowLevelController.h"
#include "ml/calm/Controller.h"
#include "ml/Tensor.h"

// NOTE: CharacterController stubs and Skeleton stubs are defined in
// test_calm_observation.cpp and test_motion_matching.cpp respectively,
// which are linked into the same test executable.

// ---------------------------------------------------------------------------
// Helper: build a minimal test skeleton (same as in test_calm_observation.cpp)
// ---------------------------------------------------------------------------
static Skeleton makeHumanoidSkeleton() {
    Skeleton skel;
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
    addJoint("Head", neck);
    int32_t rArm = addJoint("RightArm", spine1);
    int32_t rForeArm = addJoint("RightForeArm", rArm);
    addJoint("RightHand", rForeArm);
    int32_t lArm = addJoint("LeftArm", spine1);
    int32_t lForeArm = addJoint("LeftForeArm", lArm);
    addJoint("LeftHand", lForeArm);
    int32_t rUpLeg = addJoint("RightUpLeg", hips);
    int32_t rLeg = addJoint("RightLeg", rUpLeg);
    addJoint("RightFoot", rLeg);
    int32_t lUpLeg = addJoint("LeftUpLeg", hips);
    int32_t lLeg = addJoint("LeftLeg", lUpLeg);
    addJoint("LeftFoot", lLeg);

    skel.buildHierarchy();
    return skel;
}

// Helper: build a trivial LLC with known weights
static ml::calm::LowLevelController makeTrivialLLC(int obsDim, int actionDim, int latentDim) {
    // Style MLP: latentDim → 8, Tanh
    ml::MLPNetwork styleMLP;
    styleMLP.addLayer(latentDim, 8, ml::Activation::Tanh);
    // Identity-like weights (just first 8 dims of latent)
    std::vector<float> sw(latentDim * 8, 0.0f);
    for (int i = 0; i < 8; ++i) sw[i * latentDim + i] = 1.0f;
    std::vector<float> sb(8, 0.0f);
    styleMLP.setLayerWeights(0, sw, sb);

    // Main MLP: (8 + obsDim) → 16 → actionDim, ReLU then None
    ml::MLPNetwork mainMLP;
    int mainIn = 8 + obsDim;
    mainMLP.addLayer(mainIn, 16, ml::Activation::ReLU);
    mainMLP.addLayer(16, actionDim, ml::Activation::None);
    // Initialize with varying weights so different style embeddings produce different outputs
    std::vector<float> w1(mainIn * 16);
    for (size_t i = 0; i < w1.size(); ++i) w1[i] = 0.01f * (1.0f + static_cast<float>(i % 7));
    std::vector<float> b1(16, 0.0f);
    mainMLP.setLayerWeights(0, w1, b1);
    std::vector<float> w2(16 * actionDim);
    for (size_t i = 0; i < w2.size(); ++i) w2[i] = 0.01f * (1.0f + static_cast<float>(i % 5));
    std::vector<float> b2(actionDim, 0.0f);
    mainMLP.setLayerWeights(1, w2, b2);

    ml::StyleConditionedNetwork scNet;
    scNet.setStyleMLP(std::move(styleMLP));
    scNet.setMainMLP(std::move(mainMLP));

    ml::calm::LowLevelController llc;
    llc.setNetwork(std::move(scNet));
    return llc;
}

// ---------------------------------------------------------------------------
// LatentSpace tests
// ---------------------------------------------------------------------------

TEST_SUITE("LatentSpace") {
    TEST_CASE("zeroLatent is unit vector") {
        ml::LatentSpace space(64);
        ml::Tensor z = space.zeroLatent();
        CHECK(static_cast<int>(z.size()) == 64);
        CHECK(z.l2Norm() == doctest::Approx(1.0f));
        CHECK(z[0] == doctest::Approx(1.0f));
    }

    TEST_CASE("addBehavior and sampleRandom") {
        ml::LatentSpace space(4);
        ml::Tensor z1(1, 4, {1, 0, 0, 0});
        ml::Tensor z2(1, 4, {0, 1, 0, 0});
        space.addBehavior("walk", {"walk", "locomotion"}, z1);
        space.addBehavior("run", {"run", "locomotion"}, z2);

        CHECK(space.librarySize() == 2);

        std::mt19937 rng(123);
        const auto& sampled = space.sampleRandom(rng);
        CHECK(static_cast<int>(sampled.size()) == 4);
        CHECK(sampled.l2Norm() == doctest::Approx(1.0f));
    }

    TEST_CASE("sampleByTag returns matching behavior") {
        ml::LatentSpace space(4);
        ml::Tensor zWalk(1, 4, {1, 0, 0, 0});
        ml::Tensor zRun(1, 4, {0, 1, 0, 0});
        ml::Tensor zCrouch(1, 4, {0, 0, 1, 0});
        space.addBehavior("walk", {"walk"}, zWalk);
        space.addBehavior("run", {"run"}, zRun);
        space.addBehavior("crouch", {"crouch"}, zCrouch);

        std::mt19937 rng(42);
        // Sample by tag — should always get the matching one since there's only one per tag
        const auto& runZ = space.sampleByTag("run", rng);
        CHECK(runZ[1] == doctest::Approx(1.0f));
    }

    TEST_CASE("getBehaviorsByTag") {
        ml::LatentSpace space(4);
        ml::Tensor z1(1, 4, {1, 0, 0, 0});
        ml::Tensor z2(1, 4, {0, 1, 0, 0});
        ml::Tensor z3(1, 4, {0, 0, 1, 0});
        space.addBehavior("walk_fwd", {"walk", "locomotion"}, z1);
        space.addBehavior("walk_left", {"walk", "locomotion"}, z2);
        space.addBehavior("run", {"run", "locomotion"}, z3);

        auto walks = space.getBehaviorsByTag("walk");
        CHECK(walks.size() == 2);

        auto locomotion = space.getBehaviorsByTag("locomotion");
        CHECK(locomotion.size() == 3);

        auto combat = space.getBehaviorsByTag("combat");
        CHECK(combat.size() == 0);
    }

    TEST_CASE("interpolate produces normalized result") {
        ml::Tensor z0(1, 4, {1, 0, 0, 0});
        ml::Tensor z1(1, 4, {0, 1, 0, 0});

        auto mid = ml::LatentSpace::interpolate(z0, z1, 0.5f);
        CHECK(mid.l2Norm() == doctest::Approx(1.0f));

        // At alpha=0, should be z0
        auto atZero = ml::LatentSpace::interpolate(z0, z1, 0.0f);
        CHECK(atZero[0] == doctest::Approx(1.0f));

        // At alpha=1, should be z1
        auto atOne = ml::LatentSpace::interpolate(z0, z1, 1.0f);
        CHECK(atOne[1] == doctest::Approx(1.0f));
    }

    TEST_CASE("encode produces normalized output") {
        ml::LatentSpace space(4);

        ml::MLPNetwork encoder;
        encoder.addLayer(8, 4, ml::Activation::ReLU);
        std::vector<float> w(32, 0.1f);
        std::vector<float> b(4, 0.0f);
        encoder.setLayerWeights(0, w, b);
        space.setEncoder(std::move(encoder));

        CHECK(space.hasEncoder());

        ml::Tensor obs(1, 8, {1, 2, 3, 4, 5, 6, 7, 8});
        ml::Tensor z = space.encode(obs);
        CHECK(static_cast<int>(z.size()) == 4);
        CHECK(z.l2Norm() == doctest::Approx(1.0f));
    }

    TEST_CASE("sampleRandom with empty library returns fallback") {
        ml::LatentSpace space(4);
        std::mt19937 rng(1);
        const auto& z = space.sampleRandom(rng);
        CHECK(static_cast<int>(z.size()) == 4);
        CHECK(z.l2Norm() == doctest::Approx(1.0f));
    }
}

// ---------------------------------------------------------------------------
// calm::LowLevelController tests
// ---------------------------------------------------------------------------

TEST_SUITE("calm::LowLevelController") {
    TEST_CASE("evaluate produces action output") {
        int obsDim = 10;
        int actionDim = 5;
        int latentDim = 8;
        auto llc = makeTrivialLLC(obsDim, actionDim, latentDim);

        ml::Tensor latent(latentDim);
        latent.fill(0.1f);
        ml::Tensor::l2Normalize(latent);

        ml::Tensor obs(obsDim);
        obs.fill(1.0f);

        ml::Tensor actions;
        llc.evaluate(latent, obs, actions);

        CHECK(static_cast<int>(actions.size()) == actionDim);
    }

    TEST_CASE("different latents produce different actions") {
        int obsDim = 10;
        int actionDim = 5;
        int latentDim = 8;
        auto llc = makeTrivialLLC(obsDim, actionDim, latentDim);

        ml::Tensor obs(obsDim);
        obs.fill(1.0f);

        ml::Tensor z1(latentDim);
        z1[0] = 1.0f;
        ml::Tensor::l2Normalize(z1);

        ml::Tensor z2(latentDim);
        z2[1] = 1.0f;
        ml::Tensor::l2Normalize(z2);

        ml::Tensor actions1, actions2;
        llc.evaluate(z1, obs, actions1);
        llc.evaluate(z2, obs, actions2);

        // Actions should differ for different latents
        bool differ = false;
        for (int i = 0; i < actionDim; ++i) {
            if (std::abs(actions1[i] - actions2[i]) > 1e-6f) {
                differ = true;
                break;
            }
        }
        CHECK(differ);
    }

    TEST_CASE("isLoaded check") {
        ml::calm::LowLevelController empty;
        CHECK_FALSE(empty.isLoaded());

        auto loaded = makeTrivialLLC(10, 5, 8);
        CHECK(loaded.isLoaded());
    }
}

// ---------------------------------------------------------------------------
// calm::Controller (integrated) tests
// ---------------------------------------------------------------------------

TEST_SUITE("calm::Controller") {
    TEST_CASE("init and update produce valid pose") {
        Skeleton skel = makeHumanoidSkeleton();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLC(charConfig.observationDim, charConfig.actionDim,
                                   charConfig.latentDim);

        ml::LatentSpace space(charConfig.latentDim);

        ml::calm::Controller controller;
        controller.init(charConfig, std::move(llc), std::move(space));
        CHECK(controller.isInitialized());

        CharacterController physics;
        SkeletonPose pose;
        controller.update(1.0f / 30.0f, skel, physics, pose);

        CHECK(pose.size() == skel.joints.size());
    }

    TEST_CASE("setLatent changes current latent") {
        Skeleton skel = makeHumanoidSkeleton();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLC(charConfig.observationDim, charConfig.actionDim,
                                   charConfig.latentDim);

        ml::LatentSpace space(charConfig.latentDim);

        ml::calm::Controller controller;
        controller.init(charConfig, std::move(llc), std::move(space));

        ml::Tensor z(charConfig.latentDim);
        z[5] = 1.0f;
        ml::Tensor::l2Normalize(z);

        controller.setLatent(z);
        CHECK(controller.currentLatent()[5] == doctest::Approx(1.0f));
    }

    TEST_CASE("transitionToLatent interpolates over steps") {
        Skeleton skel = makeHumanoidSkeleton();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLC(charConfig.observationDim, charConfig.actionDim,
                                   charConfig.latentDim);

        ml::LatentSpace space(charConfig.latentDim);

        ml::calm::Controller controller;
        controller.init(charConfig, std::move(llc), std::move(space));

        // Start at z0
        ml::Tensor z0(charConfig.latentDim);
        z0[0] = 1.0f;
        ml::Tensor::l2Normalize(z0);
        controller.setLatent(z0);

        // Transition to z1 over 10 steps
        ml::Tensor z1(charConfig.latentDim);
        z1[1] = 1.0f;
        ml::Tensor::l2Normalize(z1);
        controller.transitionToLatent(z1, 10);

        CHECK(controller.isTransitioning());

        // Step through
        CharacterController physics;
        SkeletonPose pose;
        for (int i = 0; i < 10; ++i) {
            controller.update(1.0f / 30.0f, skel, physics, pose);
        }

        // After 10 steps, should no longer be transitioning
        CHECK_FALSE(controller.isTransitioning());
    }

    TEST_CASE("transitionToBehavior uses tag") {
        Skeleton skel = makeHumanoidSkeleton();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLC(charConfig.observationDim, charConfig.actionDim,
                                   charConfig.latentDim);

        ml::LatentSpace space(charConfig.latentDim);
        ml::Tensor zRun(charConfig.latentDim);
        zRun[3] = 1.0f;
        ml::Tensor::l2Normalize(zRun);
        space.addBehavior("run", {"run"}, zRun);

        ml::calm::Controller controller;
        controller.init(charConfig, std::move(llc), std::move(space));

        controller.transitionToBehavior("run", 5);
        CHECK(controller.isTransitioning());
    }

    TEST_CASE("reset clears state") {
        Skeleton skel = makeHumanoidSkeleton();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLC(charConfig.observationDim, charConfig.actionDim,
                                   charConfig.latentDim);

        ml::LatentSpace space(charConfig.latentDim);

        ml::calm::Controller controller;
        controller.init(charConfig, std::move(llc), std::move(space));

        ml::Tensor z(charConfig.latentDim);
        z[5] = 1.0f;
        ml::Tensor::l2Normalize(z);
        controller.transitionToLatent(z, 10);

        controller.reset();
        CHECK_FALSE(controller.isTransitioning());
    }

    TEST_CASE("updateBlended with weight 0 returns base pose") {
        Skeleton skel = makeHumanoidSkeleton();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLC(charConfig.observationDim, charConfig.actionDim,
                                   charConfig.latentDim);

        ml::LatentSpace space(charConfig.latentDim);

        ml::calm::Controller controller;
        controller.init(charConfig, std::move(llc), std::move(space));

        SkeletonPose basePose;
        basePose.resize(skel.joints.size());
        for (size_t j = 0; j < skel.joints.size(); ++j) {
            basePose[j] = BonePose::identity();
            basePose[j].translation = glm::vec3(static_cast<float>(j) * 0.1f, 0, 0);
        }

        CharacterController physics;
        SkeletonPose blended;
        controller.updateBlended(1.0f / 30.0f, skel, physics, basePose, 0.0f, blended);

        for (size_t j = 0; j < skel.joints.size(); ++j) {
            CHECK(blended[j].translation.x == doctest::Approx(basePose[j].translation.x));
        }
    }
}
