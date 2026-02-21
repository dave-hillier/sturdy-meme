#include <doctest/doctest.h>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Jolt stubs — must come before CharacterController.h
namespace JPH {
    class CharacterVirtual {};
    class PhysicsSystem {};
    class TempAllocatorImpl {};
}

#include "GLTFLoader.h"
#include "CharacterController.h"
#include "ml/TaskController.h"
#include "ml/BehaviorFSM.h"
#include "ml/AnimationIntegration.h"
#include "ml/Tensor.h"

// NOTE: CharacterController stubs defined in test_calm_observation.cpp

// ---------------------------------------------------------------------------
// Helper: build a minimal test skeleton
// ---------------------------------------------------------------------------
static Skeleton makeTestSkel() {
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

// Helper: make a trivial HLC network (taskDim -> latentDim)
static ml::TaskController makeTrivialHLC(int taskDim, int latentDim) {
    ml::MLPNetwork net;
    net.addLayer(taskDim, 32, ml::Activation::ReLU);
    net.addLayer(32, latentDim, ml::Activation::None);

    // Initialize with non-uniform weights
    std::vector<float> w1(taskDim * 32);
    for (size_t i = 0; i < w1.size(); ++i) w1[i] = 0.02f * (1.0f + static_cast<float>(i % 5));
    std::vector<float> b1(32, 0.0f);
    net.setLayerWeights(0, w1, b1);

    std::vector<float> w2(32 * latentDim);
    for (size_t i = 0; i < w2.size(); ++i) w2[i] = 0.01f * (1.0f + static_cast<float>(i % 7));
    std::vector<float> b2(latentDim, 0.0f);
    net.setLayerWeights(1, w2, b2);

    ml::TaskController hlc;
    hlc.setNetwork(std::move(net));
    return hlc;
}

// Helper: make a trivial LLC for integration tests
static ml::calm::LowLevelController makeTrivialLLCForFSM(int obsDim, int actionDim, int latentDim) {
    ml::MLPNetwork styleMLP;
    styleMLP.addLayer(latentDim, 8, ml::Activation::Tanh);
    std::vector<float> sw(latentDim * 8, 0.0f);
    for (int i = 0; i < 8; ++i) sw[i * latentDim + i] = 1.0f;
    std::vector<float> sb(8, 0.0f);
    styleMLP.setLayerWeights(0, sw, sb);

    ml::MLPNetwork mainMLP;
    int mainIn = 8 + obsDim;
    mainMLP.addLayer(mainIn, 16, ml::Activation::ReLU);
    mainMLP.addLayer(16, actionDim, ml::Activation::None);
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
// TaskController tests
// ---------------------------------------------------------------------------

TEST_SUITE("TaskController") {
    TEST_CASE("base HLC produces normalized latent") {
        auto hlc = makeTrivialHLC(3, 64);
        CHECK(hlc.isLoaded());
        CHECK(hlc.taskObsDim() == 3);
        CHECK(hlc.latentDim() == 64);

        ml::Tensor taskObs(3);
        taskObs[0] = 1.0f;
        taskObs[1] = 0.0f;
        taskObs[2] = 2.0f;

        ml::Tensor latent;
        hlc.evaluate(taskObs, latent);

        CHECK(static_cast<int>(latent.size()) == 64);
        CHECK(latent.l2Norm() == doctest::Approx(1.0f).epsilon(1e-4));
    }

    TEST_CASE("different task obs produce different latents") {
        auto hlc = makeTrivialHLC(3, 16);

        ml::Tensor taskA(3);
        taskA[0] = 1.0f; taskA[1] = 0.0f; taskA[2] = 0.0f;

        ml::Tensor taskB(3);
        taskB[0] = 0.0f; taskB[1] = 1.0f; taskB[2] = 0.0f;

        ml::Tensor latA, latB;
        hlc.evaluate(taskA, latA);
        hlc.evaluate(taskB, latB);

        bool differ = false;
        for (size_t i = 0; i < latA.size(); ++i) {
            if (std::abs(latA[i] - latB[i]) > 1e-6f) {
                differ = true;
                break;
            }
        }
        CHECK(differ);
    }
}

// ---------------------------------------------------------------------------
// HeadingController tests
// ---------------------------------------------------------------------------

TEST_SUITE("HeadingController") {
    TEST_CASE("heading controller produces latent") {
        ml::HeadingController heading;
        heading.setHLC(makeTrivialHLC(3, 64));

        heading.setTarget(glm::vec2(1.0f, 0.0f), 3.0f);

        ml::Tensor latent;
        heading.evaluate(0.0f, latent);  // facing forward

        CHECK(static_cast<int>(latent.size()) == 64);
        CHECK(latent.l2Norm() == doctest::Approx(1.0f).epsilon(1e-4));
    }

    TEST_CASE("different headings produce different latents") {
        ml::HeadingController heading;
        heading.setHLC(makeTrivialHLC(3, 16));
        heading.setTarget(glm::vec2(1.0f, 0.0f), 5.0f);

        ml::Tensor lat0, lat90;
        heading.evaluate(0.0f, lat0);
        heading.evaluate(1.5708f, lat90);  // 90 degrees

        bool differ = false;
        for (size_t i = 0; i < lat0.size(); ++i) {
            if (std::abs(lat0[i] - lat90[i]) > 1e-6f) {
                differ = true;
                break;
            }
        }
        CHECK(differ);
    }
}

// ---------------------------------------------------------------------------
// LocationController tests
// ---------------------------------------------------------------------------

TEST_SUITE("LocationController") {
    TEST_CASE("location controller evaluates") {
        ml::LocationController loc;
        loc.setHLC(makeTrivialHLC(3, 64));

        loc.setTarget(glm::vec3(10.0f, 0.0f, 10.0f));

        ml::Tensor latent;
        loc.evaluate(glm::vec3(0.0f), 0.0f, latent);

        CHECK(static_cast<int>(latent.size()) == 64);
        CHECK(latent.l2Norm() == doctest::Approx(1.0f).epsilon(1e-4));
    }

    TEST_CASE("hasReached works") {
        ml::LocationController loc;
        loc.setHLC(makeTrivialHLC(3, 64));
        loc.setTarget(glm::vec3(5.0f, 0.0f, 0.0f));

        CHECK_FALSE(loc.hasReached(glm::vec3(0.0f), 1.0f));
        CHECK(loc.hasReached(glm::vec3(4.5f, 0.0f, 0.0f), 1.0f));
        CHECK(loc.hasReached(glm::vec3(5.0f, 0.0f, 0.0f), 0.1f));
    }
}

// ---------------------------------------------------------------------------
// StrikeController tests
// ---------------------------------------------------------------------------

TEST_SUITE("StrikeController") {
    TEST_CASE("strike controller evaluates") {
        ml::StrikeController strike;
        strike.setHLC(makeTrivialHLC(4, 64));  // strike has 4D task obs

        strike.setTarget(glm::vec3(2.0f, 0.0f, 0.0f));

        ml::Tensor latent;
        strike.evaluate(glm::vec3(0.0f), 0.0f, latent);

        CHECK(static_cast<int>(latent.size()) == 64);
    }

    TEST_CASE("distanceToTarget computes correctly") {
        ml::StrikeController strike;
        strike.setHLC(makeTrivialHLC(4, 64));
        strike.setTarget(glm::vec3(3.0f, 4.0f, 0.0f));

        float dist = strike.distanceToTarget(glm::vec3(0.0f));
        CHECK(dist == doctest::Approx(5.0f));
    }
}

// ---------------------------------------------------------------------------
// BehaviorFSM tests
// ---------------------------------------------------------------------------

TEST_SUITE("BehaviorFSM") {
    TEST_CASE("add states and start") {
        Skeleton skel = makeTestSkel();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLCForFSM(charConfig.observationDim, charConfig.actionDim,
                                         charConfig.latentDim);
        ml::LatentSpace space(charConfig.latentDim);

        // Add behaviors to the latent space
        ml::Tensor zWalk(charConfig.latentDim);
        zWalk[0] = 1.0f;
        ml::Tensor::l2Normalize(zWalk);
        space.addBehavior("walk_fwd", {"walk"}, zWalk);

        ml::Tensor zIdle(charConfig.latentDim);
        zIdle[1] = 1.0f;
        ml::Tensor::l2Normalize(zIdle);
        space.addBehavior("idle", {"idle"}, zIdle);

        ml::calm::Controller controller;
        controller.init(charConfig, std::move(llc), std::move(space));

        ml::BehaviorFSM fsm;
        fsm.setController(&controller);

        int stepCount = 0;
        fsm.addState({"walk", "walk", {}, 10,
                       [&]() { return stepCount >= 5; }, "idle"});
        fsm.addState({"idle", "idle", {}, 10,
                       {}, ""});

        CHECK(fsm.stateCount() == 2);
        CHECK(fsm.hasState("walk"));
        CHECK(fsm.hasState("idle"));

        fsm.start("walk");
        CHECK(fsm.isRunning());
        CHECK(fsm.currentStateName() == "walk");
        CHECK_FALSE(fsm.isComplete());
    }

    TEST_CASE("FSM transitions on exit condition") {
        Skeleton skel = makeTestSkel();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLCForFSM(charConfig.observationDim, charConfig.actionDim,
                                         charConfig.latentDim);
        ml::LatentSpace space(charConfig.latentDim);

        ml::Tensor zWalk(charConfig.latentDim);
        zWalk[0] = 1.0f;
        ml::Tensor::l2Normalize(zWalk);
        space.addBehavior("walk_fwd", {"walk"}, zWalk);

        ml::Tensor zIdle(charConfig.latentDim);
        zIdle[1] = 1.0f;
        ml::Tensor::l2Normalize(zIdle);
        space.addBehavior("idle_anim", {"idle"}, zIdle);

        ml::calm::Controller controller;
        controller.init(charConfig, std::move(llc), std::move(space));

        ml::BehaviorFSM fsm;
        fsm.setController(&controller);

        int stepCount = 0;
        fsm.addState({"walk", "walk", {}, 10,
                       [&]() { return stepCount >= 3; }, "idle"});
        fsm.addState({"idle", "idle", {}, 10,
                       {}, ""});

        fsm.start("walk");

        // Not transitioned yet
        for (int i = 0; i < 3; ++i) {
            fsm.update();
            ++stepCount;
        }
        // Exit condition met on next update
        fsm.update();
        CHECK(fsm.currentStateName() == "idle");
    }

    TEST_CASE("FSM terminal state marks complete") {
        Skeleton skel = makeTestSkel();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLCForFSM(charConfig.observationDim, charConfig.actionDim,
                                         charConfig.latentDim);
        ml::LatentSpace space(charConfig.latentDim);

        ml::Tensor zIdle(charConfig.latentDim);
        zIdle[0] = 1.0f;
        ml::Tensor::l2Normalize(zIdle);
        space.addBehavior("idle_anim", {"idle"}, zIdle);

        ml::calm::Controller controller;
        controller.init(charConfig, std::move(llc), std::move(space));

        ml::BehaviorFSM fsm;
        fsm.setController(&controller);

        bool shouldExit = false;
        fsm.addState({"idle", "idle", {}, 5,
                       [&]() { return shouldExit; }, ""});

        fsm.start("idle");
        fsm.update();
        CHECK_FALSE(fsm.isComplete());

        shouldExit = true;
        fsm.update();
        CHECK(fsm.isComplete());
    }

    TEST_CASE("FSM stop and transitionTo") {
        Skeleton skel = makeTestSkel();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLCForFSM(charConfig.observationDim, charConfig.actionDim,
                                         charConfig.latentDim);
        ml::LatentSpace space(charConfig.latentDim);

        ml::Tensor z(charConfig.latentDim);
        z[0] = 1.0f;
        ml::Tensor::l2Normalize(z);
        space.addBehavior("walk_fwd", {"walk"}, z);
        space.addBehavior("run_fwd", {"run"}, z);

        ml::calm::Controller controller;
        controller.init(charConfig, std::move(llc), std::move(space));

        ml::BehaviorFSM fsm;
        fsm.setController(&controller);

        fsm.addState({"walk", "walk", {}, 10, {}, ""});
        fsm.addState({"run", "run", {}, 10, {}, ""});

        fsm.start("walk");
        CHECK(fsm.currentStateName() == "walk");

        fsm.transitionTo("run");
        CHECK(fsm.currentStateName() == "run");

        fsm.stop();
        CHECK_FALSE(fsm.isRunning());
    }
}

// ---------------------------------------------------------------------------
// AnimationIntegration tests
// ---------------------------------------------------------------------------

TEST_SUITE("AnimationIntegration") {
    TEST_CASE("create archetype and instance") {
        ml::ArchetypeManager manager;
        Skeleton skel = makeTestSkel();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLCForFSM(charConfig.observationDim, charConfig.actionDim,
                                         charConfig.latentDim);
        ml::LatentSpace space(charConfig.latentDim);

        uint32_t archetypeId = manager.createArchetype(
            "humanoid", 0, std::move(llc), std::move(space), charConfig);

        CHECK(manager.archetypeCount() == 1);
        CHECK(manager.getArchetype(archetypeId) != nullptr);
        CHECK(manager.findArchetype("humanoid") != nullptr);

        size_t instIdx = manager.createInstance(archetypeId);
        CHECK(manager.instanceCount() == 1);

        manager.initInstance(instIdx, skel);
        const auto* inst = manager.getInstance(instIdx);
        CHECK(inst != nullptr);
        CHECK(inst->initialized);
    }

    TEST_CASE("update instance produces valid pose") {
        ml::ArchetypeManager manager;
        Skeleton skel = makeTestSkel();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLCForFSM(charConfig.observationDim, charConfig.actionDim,
                                         charConfig.latentDim);
        ml::LatentSpace space(charConfig.latentDim);

        uint32_t archetypeId = manager.createArchetype(
            "humanoid", 0, std::move(llc), std::move(space), charConfig);
        size_t instIdx = manager.createInstance(archetypeId);
        manager.initInstance(instIdx, skel);

        CharacterController physics;
        manager.updateInstance(instIdx, 1.0f / 30.0f, skel, physics);

        const auto& matrices = manager.getBoneMatrices(instIdx);
        // Before computeBoneMatrices, the matrices are still identity
        manager.computeBoneMatrices(instIdx, skel);
        const auto& updatedMatrices = manager.getBoneMatrices(instIdx);
        CHECK(updatedMatrices.size() == skel.joints.size());
    }

    TEST_CASE("LOD-aware update skips frames") {
        ml::ArchetypeManager manager;
        Skeleton skel = makeTestSkel();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLCForFSM(charConfig.observationDim, charConfig.actionDim,
                                         charConfig.latentDim);
        ml::LatentSpace space(charConfig.latentDim);

        uint32_t archetypeId = manager.createArchetype(
            "humanoid", 0, std::move(llc), std::move(space), charConfig);
        size_t instIdx = manager.createInstance(archetypeId);
        manager.initInstance(instIdx, skel);

        CharacterLODConfig lodConfig;
        lodConfig.animationUpdateInterval = {1, 1, 2, 4};

        // At LOD 0, should update every frame
        manager.setInstanceLOD(instIdx, 0);
        CHECK(manager.shouldUpdateInstance(instIdx, 0, lodConfig));

        // At LOD 3, should skip frames
        manager.setInstanceLOD(instIdx, 3);
        // framesSinceUpdate starts at 0, interval is 4 — should not update yet
        CHECK_FALSE(manager.shouldUpdateInstance(instIdx, 0, lodConfig));
    }

    TEST_CASE("computeBoneMatricesFromPose produces valid output") {
        Skeleton skel = makeTestSkel();

        // Create an identity pose
        SkeletonPose pose;
        pose.resize(skel.joints.size());
        for (size_t i = 0; i < skel.joints.size(); ++i) {
            pose[i] = BonePose::identity();
        }

        std::vector<glm::mat4> matrices;
        ml::computeBoneMatricesFromPose(pose, skel, matrices);

        CHECK(matrices.size() == skel.joints.size());
        // For identity pose with identity inverse bind, all matrices should be identity
        for (size_t i = 0; i < matrices.size(); ++i) {
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    float expected = (r == c) ? 1.0f : 0.0f;
                    CHECK(matrices[i][r][c] == doctest::Approx(expected).epsilon(0.01));
                }
            }
        }
    }

    TEST_CASE("clearInstances keeps archetypes") {
        ml::ArchetypeManager manager;
        Skeleton skel = makeTestSkel();
        auto charConfig = ml::CharacterConfig::buildFromSkeleton(skel);
        auto llc = makeTrivialLLCForFSM(charConfig.observationDim, charConfig.actionDim,
                                         charConfig.latentDim);
        ml::LatentSpace space(charConfig.latentDim);

        manager.createArchetype("humanoid", 0, std::move(llc), std::move(space), charConfig);
        manager.createInstance(0);
        CHECK(manager.instanceCount() == 1);

        manager.clearInstances();
        CHECK(manager.instanceCount() == 0);
        CHECK(manager.archetypeCount() == 1);
    }
}

// NOTE: GPUInference tests require a Vulkan device and are not included
// in the unit test suite. GPU inference is tested via integration tests
// with the full rendering pipeline.
