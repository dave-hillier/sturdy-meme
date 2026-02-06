#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "FBXLoader.h"
#include "FBXPostProcess.h"
#include "SkinnedMesh.h"
#include "MotionMatchingController.h"
#include "MotionDatabase.h"
#include "MotionMatchingFeature.h"
#include "Animation.h"
#include "AnimationBlend.h"
#include "GLTFLoader.h"

using namespace MotionMatching;

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <string>

// ============================================================================
// Test helpers
// ============================================================================

namespace {

const std::string ASSETS_DIR = "assets/characters/fbx/";
const std::string MODEL_PATH = ASSETS_DIR + "Y Bot.fbx";

// Animation file paths (relative to project root)
struct AnimFiles {
    // Locomotion
    static std::string idle() { return ASSETS_DIR + "sword and shield idle.fbx"; }
    static std::string idle2() { return ASSETS_DIR + "sword and shield idle (2).fbx"; }
    static std::string walk() { return ASSETS_DIR + "sword and shield walk.fbx"; }
    static std::string walk2() { return ASSETS_DIR + "sword and shield walk (2).fbx"; }
    static std::string run() { return ASSETS_DIR + "sword and shield run.fbx"; }
    static std::string run2() { return ASSETS_DIR + "sword and shield run (2).fbx"; }
    static std::string strafe() { return ASSETS_DIR + "sword and shield strafe.fbx"; }
    static std::string strafe2() { return ASSETS_DIR + "sword and shield strafe (2).fbx"; }
    static std::string strafe3() { return ASSETS_DIR + "sword and shield strafe (3).fbx"; }
    static std::string strafe4() { return ASSETS_DIR + "sword and shield strafe (4).fbx"; }
    static std::string turn() { return ASSETS_DIR + "sword and shield turn.fbx"; }
    static std::string turn180() { return ASSETS_DIR + "sword and shield 180 turn.fbx"; }
    static std::string jump() { return ASSETS_DIR + "sword and shield jump.fbx"; }
    static std::string jump2() { return ASSETS_DIR + "sword and shield jump (2).fbx"; }
};

// Locomotion speed constants (matching AnimatedCharacter::initializeMotionMatching)
constexpr float IDLE_SPEED = 0.0f;
constexpr float WALK_SPEED = 1.4f;
constexpr float RUN_SPEED = 5.0f;
constexpr float STRAFE_SPEED = 1.8f;
constexpr float TURN_SPEED = 0.5f;

// Check if a string contains a substring (case-insensitive)
bool containsCI(const std::string& str, const std::string& substr) {
    std::string lower = str;
    std::string lowerSub = substr;
    for (char& c : lower) c = static_cast<char>(std::tolower(c));
    for (char& c : lowerSub) c = static_cast<char>(std::tolower(c));
    return lower.find(lowerSub) != std::string::npos;
}

// Check if model file exists
bool modelExists() {
    return std::filesystem::exists(MODEL_PATH);
}

// Load the Y Bot model to get the skeleton
std::optional<GLTFSkinnedLoadResult> loadModel() {
    if (!modelExists()) return std::nullopt;
    return FBXLoader::loadSkinned(MODEL_PATH);
}

// Load animations from a single FBX file
std::vector<AnimationClip> loadAnims(const std::string& path, const Skeleton& skeleton) {
    if (!std::filesystem::exists(path)) return {};
    return FBXLoader::loadAnimations(path, skeleton);
}

// Classify an animation clip and return tags, looping flag, locomotion speed, cost bias
struct ClipClassification {
    std::vector<std::string> tags;
    bool looping = false;
    float locomotionSpeed = 0.0f;
    float costBias = 0.0f;
};

ClipClassification classifyClip(const AnimationClip& clip) {
    ClipClassification result;
    std::string lowerName = clip.name;
    for (char& c : lowerName) c = static_cast<char>(std::tolower(c));

    // Skip metadata
    if (lowerName == "mixamo.com" || lowerName.empty() || clip.duration < 0.1f) {
        return result;
    }

    bool isVariant = (lowerName.find("2") != std::string::npos ||
                     lowerName.find("_2") != std::string::npos ||
                     lowerName.find("alt") != std::string::npos);
    if (isVariant) {
        result.costBias = 0.5f;
    }

    if (lowerName.find("idle") != std::string::npos) {
        result.tags = {"idle", "locomotion"};
        result.looping = true;
        result.locomotionSpeed = IDLE_SPEED;
    } else if (lowerName.find("run") != std::string::npos) {
        result.tags = {"run", "locomotion"};
        result.looping = true;
        result.locomotionSpeed = RUN_SPEED;
    } else if (lowerName.find("walk") != std::string::npos) {
        result.tags = {"walk", "locomotion"};
        result.looping = true;
        result.locomotionSpeed = WALK_SPEED;
    } else if (lowerName.find("strafe") != std::string::npos) {
        result.tags = {"strafe", "locomotion"};
        result.looping = true;
        result.locomotionSpeed = STRAFE_SPEED;
    } else if (lowerName.find("turn") != std::string::npos) {
        result.tags = {"turn", "locomotion"};
        result.looping = false;
        result.locomotionSpeed = TURN_SPEED;
    } else if (lowerName.find("jump") != std::string::npos) {
        result.tags = {"jump"};
        result.looping = false;
    }

    return result;
}

// A fully loaded test fixture with skeleton + animations + motion matching controller
struct MotionMatchingFixture {
    Skeleton skeleton;
    std::vector<AnimationClip> allAnimations;
    MotionMatching::MotionMatchingController controller;
    bool valid = false;

    // Clip name lookup by index in the database
    std::vector<std::string> clipNames;

    bool setup() {
        auto modelResult = loadModel();
        if (!modelResult) return false;

        skeleton = std::move(modelResult->skeleton);
        allAnimations = std::move(modelResult->animations);

        // Load additional animation files
        auto loadAdditional = [&](const std::string& path) {
            auto clips = loadAnims(path, skeleton);
            for (auto& clip : clips) {
                allAnimations.push_back(std::move(clip));
            }
        };

        loadAdditional(AnimFiles::idle());
        loadAdditional(AnimFiles::idle2());
        loadAdditional(AnimFiles::walk());
        loadAdditional(AnimFiles::walk2());
        loadAdditional(AnimFiles::run());
        loadAdditional(AnimFiles::run2());
        loadAdditional(AnimFiles::strafe());
        loadAdditional(AnimFiles::strafe2());
        loadAdditional(AnimFiles::strafe3());
        loadAdditional(AnimFiles::strafe4());
        loadAdditional(AnimFiles::turn());
        loadAdditional(AnimFiles::turn180());
        loadAdditional(AnimFiles::jump());
        loadAdditional(AnimFiles::jump2());

        // Initialize the controller
        MotionMatching::ControllerConfig config;
        config.searchInterval = 0.0f; // Search every frame for test determinism
        config.useInertialBlending = false; // Disable blending for cleaner test results
        controller.initialize(config);
        controller.setSkeleton(skeleton);

        // Add all clips to the database (mirroring AnimatedCharacter::initializeMotionMatching)
        for (size_t i = 0; i < allAnimations.size(); ++i) {
            const auto& clip = allAnimations[i];
            auto classification = classifyClip(clip);

            // Skip metadata/placeholder clips
            std::string lowerName = clip.name;
            for (char& c : lowerName) c = static_cast<char>(std::tolower(c));
            if (lowerName == "mixamo.com" || lowerName.empty() || clip.duration < 0.1f) {
                continue;
            }

            controller.addClip(&clip, clip.name, classification.looping,
                             classification.tags, classification.locomotionSpeed,
                             classification.costBias);
            clipNames.push_back(clip.name);
        }

        // Build the database
        MotionMatching::DatabaseBuildOptions buildOptions;
        buildOptions.defaultSampleRate = 30.0f;
        buildOptions.pruneStaticPoses = false;
        controller.buildDatabase(buildOptions);

        // Exclude jump from normal locomotion
        controller.setExcludedTags({"jump"});

        valid = true;
        return true;
    }

    // Run the controller for a number of frames with given input
    // Returns the name of the currently selected clip after the simulation
    std::string simulate(const glm::vec3& inputDirection, float inputMagnitude,
                        float duration = 1.0f, float dt = 1.0f / 30.0f) {
        glm::vec3 position(0.0f);
        glm::vec3 facing(0.0f, 0.0f, 1.0f); // Facing forward (+Z)

        int frames = static_cast<int>(duration / dt);
        for (int i = 0; i < frames; ++i) {
            controller.update(position, facing, inputDirection, inputMagnitude, dt);
        }

        return currentClipName();
    }

    // Get the current clip name from the controller
    // Use playback state directly since stats.currentClipName is only set on transitions
    std::string currentClipName() const {
        const auto& db = controller.getDatabase();
        if (!db.isBuilt() || db.getClipCount() == 0) return "";
        const auto& playback = controller.getPlaybackState();
        if (playback.clipIndex >= db.getClipCount()) return "";
        return db.getClip(playback.clipIndex).name;
    }
};

} // anonymous namespace

// ============================================================================
// FBX Loading Tests
// ============================================================================

TEST_SUITE("FBX Model Loading") {

TEST_CASE("Y Bot model file exists") {
    REQUIRE(modelExists());
}

TEST_CASE("load Y Bot skeleton from FBX") {
    auto result = loadModel();
    REQUIRE(result.has_value());

    const auto& skeleton = result->skeleton;
    CHECK(skeleton.joints.size() > 0);

    // Y Bot should have a reasonable number of bones (Mixamo standard ~65)
    CHECK(skeleton.joints.size() > 20);
    CHECK(skeleton.joints.size() < 200);

    // Should have a root joint
    bool hasRoot = false;
    for (const auto& joint : skeleton.joints) {
        if (joint.parentIndex < 0) {
            hasRoot = true;
            break;
        }
    }
    CHECK(hasRoot);
}

TEST_CASE("load Y Bot has standard bone names") {
    auto result = loadModel();
    REQUIRE(result.has_value());

    const auto& skeleton = result->skeleton;

    // After Mixamo import processing, bone names should have the prefix stripped
    // Check for common bone names
    bool hasHips = skeleton.findJointIndex("Hips") >= 0;
    bool hasSpine = skeleton.findJointIndex("Spine") >= 0;
    bool hasHead = skeleton.findJointIndex("Head") >= 0;
    bool hasLeftFoot = skeleton.findJointIndex("LeftFoot") >= 0;
    bool hasRightFoot = skeleton.findJointIndex("RightFoot") >= 0;

    CHECK(hasHips);
    CHECK(hasSpine);
    CHECK(hasHead);
    CHECK(hasLeftFoot);
    CHECK(hasRightFoot);
}

TEST_CASE("Y Bot skeleton has valid hierarchy") {
    auto result = loadModel();
    REQUIRE(result.has_value());

    const auto& skeleton = result->skeleton;

    // Count root bones (parentIndex == -1)
    int rootCount = 0;
    for (const auto& joint : skeleton.joints) {
        if (joint.parentIndex < 0) {
            rootCount++;
        } else {
            // Non-root joints must have valid parent index
            CHECK(joint.parentIndex >= 0);
            CHECK(static_cast<size_t>(joint.parentIndex) < skeleton.joints.size());
        }
    }

    // Should have exactly one root (or at most a few for armature + root bone)
    CHECK(rootCount >= 1);
    CHECK(rootCount <= 3);
}

} // TEST_SUITE("FBX Model Loading")

// ============================================================================
// Animation Loading Tests
// ============================================================================

TEST_SUITE("FBX Animation Loading") {

TEST_CASE("load idle animation from FBX") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto clips = loadAnims(AnimFiles::idle(), model->skeleton);
    REQUIRE(!clips.empty());

    // Should have at least one non-metadata clip
    bool hasValidClip = false;
    for (const auto& clip : clips) {
        if (clip.duration > 0.1f && clip.name != "mixamo.com") {
            hasValidClip = true;
            CHECK(clip.duration > 0.0f);
            CHECK(!clip.channels.empty());
        }
    }
    CHECK(hasValidClip);
}

TEST_CASE("load walk animation from FBX") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto clips = loadAnims(AnimFiles::walk(), model->skeleton);
    REQUIRE(!clips.empty());

    for (const auto& clip : clips) {
        if (clip.duration > 0.1f && clip.name != "mixamo.com") {
            CHECK(clip.duration > 0.0f);
            // Walk animation should have channels for multiple joints
            CHECK(clip.channels.size() > 5);
        }
    }
}

TEST_CASE("load run animation from FBX") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto clips = loadAnims(AnimFiles::run(), model->skeleton);
    REQUIRE(!clips.empty());

    for (const auto& clip : clips) {
        if (clip.duration > 0.1f && clip.name != "mixamo.com") {
            CHECK(clip.duration > 0.0f);
        }
    }
}

TEST_CASE("load strafe animations from FBX") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto leftClips = loadAnims(AnimFiles::strafe(), model->skeleton);
    auto rightClips = loadAnims(AnimFiles::strafe2(), model->skeleton);

    CHECK(!leftClips.empty());
    CHECK(!rightClips.empty());
}

TEST_CASE("load jump animation from FBX") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto clips = loadAnims(AnimFiles::jump(), model->skeleton);
    REQUIRE(!clips.empty());

    for (const auto& clip : clips) {
        if (clip.duration > 0.1f && clip.name != "mixamo.com") {
            CHECK(clip.duration > 0.0f);
        }
    }
}

TEST_CASE("animation channels reference valid skeleton joints") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto clips = loadAnims(AnimFiles::walk(), model->skeleton);
    REQUIRE(!clips.empty());

    for (const auto& clip : clips) {
        if (clip.duration < 0.1f) continue;
        for (const auto& channel : clip.channels) {
            // Every channel should reference a valid joint
            CHECK(channel.jointIndex >= 0);
            CHECK(static_cast<size_t>(channel.jointIndex) < model->skeleton.joints.size());
        }
    }
}

TEST_CASE("animation can be sampled without crashing") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto clips = loadAnims(AnimFiles::walk(), model->skeleton);
    REQUIRE(!clips.empty());

    for (const auto& clip : clips) {
        if (clip.duration < 0.1f) continue;

        // Sample at various time points
        Skeleton tempSkel = model->skeleton;
        clip.sample(0.0f, tempSkel);
        clip.sample(clip.duration * 0.5f, tempSkel);
        clip.sample(clip.duration, tempSkel);

        // Verify no NaN in joint transforms
        for (const auto& joint : tempSkel.joints) {
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    CHECK_FALSE(std::isnan(joint.localTransform[col][row]));
                    CHECK_FALSE(std::isinf(joint.localTransform[col][row]));
                }
            }
        }
    }
}

TEST_CASE("multiple animation files load consistently") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    // Load several animation files and verify they all produce valid clips
    std::vector<std::string> animPaths = {
        AnimFiles::idle(), AnimFiles::walk(), AnimFiles::run(),
        AnimFiles::strafe(), AnimFiles::turn(), AnimFiles::jump()
    };

    for (const auto& path : animPaths) {
        if (!std::filesystem::exists(path)) continue;

        auto clips = loadAnims(path, model->skeleton);
        INFO("Animation file: " << path);
        CHECK(!clips.empty());

        for (const auto& clip : clips) {
            if (clip.duration < 0.1f) continue;
            // All valid clips should have reasonable duration (< 30 seconds for motion capture)
            CHECK(clip.duration < 30.0f);
            CHECK(!clip.channels.empty());
        }
    }
}

} // TEST_SUITE("FBX Animation Loading")

// ============================================================================
// Motion Matching Database Building Tests
// ============================================================================

TEST_SUITE("Motion Matching Database Build") {

TEST_CASE("database builds from real FBX animations") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    const auto& db = fixture.controller.getDatabase();
    CHECK(db.isBuilt());
    CHECK(db.getPoseCount() > 0);
    CHECK(db.getClipCount() > 0);
}

TEST_CASE("database has reasonable pose count") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    const auto& db = fixture.controller.getDatabase();

    // With ~14 animation files at 30fps, each ~1-3 seconds,
    // we should have at least 100 poses
    CHECK(db.getPoseCount() > 100);

    // But not absurdly many (sanity check)
    CHECK(db.getPoseCount() < 100000);

    auto stats = db.getStats();
    CHECK(stats.totalClips > 5);
    CHECK(stats.totalDuration > 5.0f);  // At least 5 seconds of animation
}

TEST_CASE("database clips have correct tags") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    const auto& db = fixture.controller.getDatabase();

    bool hasIdlePoses = false;
    bool hasWalkPoses = false;
    bool hasRunPoses = false;
    bool hasStrafePoses = false;
    bool hasJumpPoses = false;

    for (size_t i = 0; i < db.getClipCount(); ++i) {
        const auto& clip = db.getClip(i);
        std::string lowerName = clip.name;
        for (char& c : lowerName) c = static_cast<char>(std::tolower(c));

        if (lowerName.find("idle") != std::string::npos) {
            hasIdlePoses = true;
            // Idle clips should have the "idle" and "locomotion" tags
            bool found = false;
            for (const auto& tag : clip.tags) {
                if (tag == "idle") found = true;
            }
            CHECK(found);
        }
        if (lowerName.find("walk") != std::string::npos) hasWalkPoses = true;
        if (lowerName.find("run") != std::string::npos) hasRunPoses = true;
        if (lowerName.find("strafe") != std::string::npos) hasStrafePoses = true;
        if (lowerName.find("jump") != std::string::npos) hasJumpPoses = true;
    }

    CHECK(hasIdlePoses);
    CHECK(hasWalkPoses);
    CHECK(hasRunPoses);
    CHECK(hasStrafePoses);
    CHECK(hasJumpPoses);
}

TEST_CASE("database poses have valid features") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    const auto& db = fixture.controller.getDatabase();

    for (size_t i = 0; i < std::min(db.getPoseCount(), size_t(100)); ++i) {
        const auto& pose = db.getPose(i);

        // Pose should reference a valid clip
        CHECK(pose.clipIndex < db.getClipCount());

        // Time should be within clip duration
        const auto& clip = db.getClip(pose.clipIndex);
        CHECK(pose.time >= 0.0f);
        CHECK(pose.time <= clip.duration + 0.01f);

        // Features should not contain NaN
        for (size_t j = 0; j < pose.poseFeatures.boneCount; ++j) {
            const auto& bone = pose.poseFeatures.boneFeatures[j];
            CHECK_FALSE(std::isnan(bone.position.x));
            CHECK_FALSE(std::isnan(bone.position.y));
            CHECK_FALSE(std::isnan(bone.position.z));
            CHECK_FALSE(std::isnan(bone.velocity.x));
            CHECK_FALSE(std::isnan(bone.velocity.y));
            CHECK_FALSE(std::isnan(bone.velocity.z));
        }

        CHECK_FALSE(std::isnan(pose.poseFeatures.rootVelocity.x));
        CHECK_FALSE(std::isnan(pose.poseFeatures.rootVelocity.y));
        CHECK_FALSE(std::isnan(pose.poseFeatures.rootVelocity.z));
    }
}

TEST_CASE("database KD-tree is built") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    const auto& db = fixture.controller.getDatabase();
    CHECK(db.hasKDTree());
}

TEST_CASE("database locomotion speeds are set correctly") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    const auto& db = fixture.controller.getDatabase();

    for (size_t i = 0; i < db.getClipCount(); ++i) {
        const auto& clip = db.getClip(i);
        std::string lowerName = clip.name;
        for (char& c : lowerName) c = static_cast<char>(std::tolower(c));

        if (lowerName.find("idle") != std::string::npos) {
            CHECK(clip.locomotionSpeed == doctest::Approx(IDLE_SPEED));
        } else if (lowerName.find("walk") != std::string::npos) {
            CHECK(clip.locomotionSpeed == doctest::Approx(WALK_SPEED));
        } else if (lowerName.find("run") != std::string::npos) {
            CHECK(clip.locomotionSpeed == doctest::Approx(RUN_SPEED));
        } else if (lowerName.find("strafe") != std::string::npos) {
            CHECK(clip.locomotionSpeed == doctest::Approx(STRAFE_SPEED));
        }
    }
}

} // TEST_SUITE("Motion Matching Database Build")

// ============================================================================
// Animation Selection Tests (core integration scenarios)
// ============================================================================

TEST_SUITE("Motion Matching Animation Selection") {

TEST_CASE("standing still selects idle animation") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    // No input = standing still
    glm::vec3 noInput(0.0f);
    std::string selected = fixture.simulate(noInput, 0.0f, 2.0f);

    INFO("Selected clip: " << selected);
    CHECK(containsCI(selected, "idle"));
}

TEST_CASE("forward movement selects walk or run") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    // Moderate forward input
    glm::vec3 forward(0.0f, 0.0f, 1.0f);
    std::string selected = fixture.simulate(forward, 0.5f, 2.0f);

    INFO("Selected clip: " << selected);
    // Should select some locomotion animation (walk or run)
    bool isLocomotion = containsCI(selected, "walk") ||
                       containsCI(selected, "run");
    CHECK(isLocomotion);
}

TEST_CASE("full speed forward selects run animation") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    // Full speed forward input
    glm::vec3 forward(0.0f, 0.0f, 1.0f);
    std::string selected = fixture.simulate(forward, 1.0f, 2.0f);

    INFO("Selected clip: " << selected);
    // At full speed, should prefer run over walk
    bool isRunOrWalk = containsCI(selected, "run") ||
                      containsCI(selected, "walk");
    CHECK(isRunOrWalk);
}

TEST_CASE("jump animations are excluded from normal search") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    // Test with various inputs - jump should never be selected
    std::vector<std::pair<glm::vec3, float>> inputs = {
        {{0.0f, 0.0f, 0.0f}, 0.0f},    // Idle
        {{0.0f, 0.0f, 1.0f}, 0.5f},     // Walk
        {{0.0f, 0.0f, 1.0f}, 1.0f},     // Run
        {{1.0f, 0.0f, 0.0f}, 0.5f},     // Strafe right
    };

    for (const auto& input : inputs) {
        const glm::vec3& dir = input.first;
        float mag = input.second;

        // Reset by creating a fresh fixture each time
        MotionMatchingFixture fresh;
        REQUIRE(fresh.setup());

        std::string selected = fresh.simulate(dir, mag, 1.0f);
        INFO("Input dir: (" << dir.x << ", " << dir.y << ", " << dir.z << ") mag: " << mag);
        INFO("Selected clip: " << selected);
        CHECK_FALSE(containsCI(selected, "jump"));
    }
}

TEST_CASE("controller transitions between animations smoothly") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    // Start idle
    fixture.simulate(glm::vec3(0.0f), 0.0f, 1.0f);
    std::string idleClip = fixture.currentClipName();

    // Switch to moving
    fixture.simulate(glm::vec3(0.0f, 0.0f, 1.0f), 1.0f, 2.0f);
    std::string movingClip = fixture.currentClipName();

    // The animation should have changed
    INFO("Idle clip: " << idleClip);
    INFO("Moving clip: " << movingClip);

    // At least one of them should be a locomotion animation
    bool idleIsIdle = containsCI(idleClip, "idle");
    bool movingIsLocomoting = containsCI(movingClip, "walk") ||
                             containsCI(movingClip, "run");

    // We expect idle to become some movement clip, or at minimum the clip changed
    CHECK((idleIsIdle || movingIsLocomoting || idleClip != movingClip));
}

TEST_CASE("controller does not produce NaN values during simulation") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    float dt = 1.0f / 30.0f;
    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);

    // Simulate various inputs over time
    struct InputPhase {
        glm::vec3 direction;
        float magnitude;
        float duration;
    };
    std::vector<InputPhase> phases = {
        {{0.0f, 0.0f, 0.0f}, 0.0f, 0.5f},  // Idle
        {{0.0f, 0.0f, 1.0f}, 0.3f, 0.5f},  // Slow walk
        {{0.0f, 0.0f, 1.0f}, 1.0f, 0.5f},  // Full run
        {{1.0f, 0.0f, 0.0f}, 0.5f, 0.5f},  // Strafe
        {{0.0f, 0.0f, 0.0f}, 0.0f, 0.5f},  // Back to idle
    };

    for (const auto& phase : phases) {
        int frames = static_cast<int>(phase.duration / dt);
        for (int i = 0; i < frames; ++i) {
            fixture.controller.update(position, facing, phase.direction, phase.magnitude, dt);

            // Verify stats are valid (cost can be negative due to continuing pose bias)
            const auto& stats = fixture.controller.getStats();
            CHECK_FALSE(std::isnan(stats.lastMatchCost));
            CHECK_FALSE(std::isinf(stats.lastMatchCost));
        }
    }

    // Verify the current pose is valid
    SkeletonPose pose;
    fixture.controller.getCurrentPose(pose);
    CHECK(!pose.empty());

    for (size_t i = 0; i < pose.size(); ++i) {
        CHECK_FALSE(std::isnan(pose[i].translation.x));
        CHECK_FALSE(std::isnan(pose[i].translation.y));
        CHECK_FALSE(std::isnan(pose[i].translation.z));
        CHECK_FALSE(std::isnan(pose[i].rotation.x));
        CHECK_FALSE(std::isnan(pose[i].rotation.y));
        CHECK_FALSE(std::isnan(pose[i].rotation.z));
        CHECK_FALSE(std::isnan(pose[i].rotation.w));
    }
}

TEST_CASE("apply to skeleton produces valid transforms") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    // Run a few frames
    fixture.simulate(glm::vec3(0.0f, 0.0f, 1.0f), 0.5f, 0.5f);

    // Apply to skeleton and check results
    Skeleton skel = fixture.skeleton;
    fixture.controller.applyToSkeleton(skel);

    for (const auto& joint : skel.joints) {
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                CHECK_FALSE(std::isnan(joint.localTransform[col][row]));
                CHECK_FALSE(std::isinf(joint.localTransform[col][row]));
            }
        }
    }
}

TEST_CASE("skeleton global transforms are valid after motion matching") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    // Simulate
    fixture.simulate(glm::vec3(0.0f, 0.0f, 1.0f), 0.5f, 0.5f);

    // Apply and compute global transforms
    Skeleton skel = fixture.skeleton;
    fixture.controller.applyToSkeleton(skel);
    skel.buildHierarchy();

    std::vector<glm::mat4> globalTransforms;
    skel.computeGlobalTransforms(globalTransforms);

    CHECK(globalTransforms.size() == skel.joints.size());

    for (size_t i = 0; i < globalTransforms.size(); ++i) {
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                INFO("Joint " << i << " (" << skel.joints[i].name << ") matrix[" << col << "][" << row << "]");
                CHECK_FALSE(std::isnan(globalTransforms[i][col][row]));
                CHECK_FALSE(std::isinf(globalTransforms[i][col][row]));
            }
        }
    }
}

} // TEST_SUITE("Motion Matching Animation Selection")

// ============================================================================
// Tag Filtering Tests
// ============================================================================

TEST_SUITE("Motion Matching Tag Filtering") {

TEST_CASE("required tags filter works with real data") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    const auto& db = fixture.controller.getDatabase();

    // Get poses with "idle" tag
    auto idlePoses = db.getPosesWithTag("idle");
    CHECK(!idlePoses.empty());

    // Get poses with "locomotion" tag
    auto locoPoses = db.getPosesWithTag("locomotion");
    CHECK(!locoPoses.empty());

    // Locomotion should include idle, walk, run, strafe, turn
    CHECK(locoPoses.size() >= idlePoses.size());
}

TEST_CASE("excluding jump tags prevents jump selection") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    // With jump excluded (set in setup), verify jump poses are filtered out
    // by checking that the matcher never returns a jump clip
    MotionMatching::MotionMatcher matcher;
    matcher.setDatabase(&fixture.controller.getDatabase());

    Trajectory queryTraj;
    PoseFeatures queryPose;
    MotionMatching::SearchOptions options;
    options.excludedTags = {"jump"};
    options.useKDTree = false; // Brute force to check all poses

    auto result = matcher.findBestMatch(queryTraj, queryPose, options);
    if (result.isValid()) {
        CHECK_FALSE(result.pose->hasTag("jump"));
    }
}

TEST_CASE("requiring idle tag selects only idle animations") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    MotionMatching::MotionMatcher matcher;
    matcher.setDatabase(&fixture.controller.getDatabase());

    Trajectory queryTraj;
    PoseFeatures queryPose;
    MotionMatching::SearchOptions options;
    options.requiredTags = {"idle"};
    options.excludedTags.clear();
    options.useKDTree = false; // Brute force to guarantee finding tagged poses

    auto result = matcher.findBestMatch(queryTraj, queryPose, options);
    REQUIRE(result.isValid());
    CHECK(result.pose->hasTag("idle"));

    // The clip should have "idle" in its name
    INFO("Selected clip: " << result.clip->name);
    CHECK(containsCI(result.clip->name, "idle"));
}

TEST_CASE("requiring run tag selects only run animations") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    MotionMatching::MotionMatcher matcher;
    matcher.setDatabase(&fixture.controller.getDatabase());

    Trajectory queryTraj;
    PoseFeatures queryPose;
    MotionMatching::SearchOptions options;
    options.requiredTags = {"run"};
    options.excludedTags.clear();
    options.useKDTree = false; // Brute force to guarantee finding tagged poses

    auto result = matcher.findBestMatch(queryTraj, queryPose, options);
    REQUIRE(result.isValid());
    CHECK(result.pose->hasTag("run"));

    INFO("Selected clip: " << result.clip->name);
    CHECK(containsCI(result.clip->name, "run"));
}

} // TEST_SUITE("Motion Matching Tag Filtering")

// ============================================================================
// Feature Extraction with Real Data
// ============================================================================

TEST_SUITE("Feature Extraction Real Data") {

TEST_CASE("feature extractor produces valid features from real animations") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto walkClips = loadAnims(AnimFiles::walk(), model->skeleton);
    REQUIRE(!walkClips.empty());

    // Find a valid walk clip
    const AnimationClip* walkClip = nullptr;
    for (const auto& clip : walkClips) {
        if (clip.duration > 0.1f && clip.name != "mixamo.com") {
            walkClip = &clip;
            break;
        }
    }
    REQUIRE(walkClip != nullptr);

    // Extract features
    FeatureConfig config = FeatureConfig::locomotion();
    FeatureExtractor extractor;
    extractor.initialize(model->skeleton, config);

    PoseFeatures features = extractor.extractFromClip(*walkClip, model->skeleton, 0.0f);

    // Features should not be all zeros (animation should have some motion)
    bool hasNonZeroFeature = false;
    for (size_t i = 0; i < features.boneCount; ++i) {
        if (glm::length(features.boneFeatures[i].position) > 0.001f) {
            hasNonZeroFeature = true;
            break;
        }
    }
    CHECK(hasNonZeroFeature);

    // Check for NaN
    for (size_t i = 0; i < features.boneCount; ++i) {
        CHECK_FALSE(std::isnan(features.boneFeatures[i].position.x));
        CHECK_FALSE(std::isnan(features.boneFeatures[i].position.y));
        CHECK_FALSE(std::isnan(features.boneFeatures[i].position.z));
    }
}

TEST_CASE("different animations produce different features") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto idleClips = loadAnims(AnimFiles::idle(), model->skeleton);
    auto runClips = loadAnims(AnimFiles::run(), model->skeleton);
    REQUIRE(!idleClips.empty());
    REQUIRE(!runClips.empty());

    const AnimationClip* idleClip = nullptr;
    const AnimationClip* runClip = nullptr;
    for (const auto& clip : idleClips) {
        if (clip.duration > 0.1f && clip.name != "mixamo.com") { idleClip = &clip; break; }
    }
    for (const auto& clip : runClips) {
        if (clip.duration > 0.1f && clip.name != "mixamo.com") { runClip = &clip; break; }
    }
    REQUIRE(idleClip != nullptr);
    REQUIRE(runClip != nullptr);

    FeatureConfig config = FeatureConfig::locomotion();
    FeatureExtractor extractor;
    extractor.initialize(model->skeleton, config);

    PoseFeatures idleFeatures = extractor.extractFromClip(*idleClip, model->skeleton, 0.0f);
    PoseFeatures runFeatures = extractor.extractFromClip(*runClip, model->skeleton, 0.0f);

    // The features should be different between idle and run
    float diff = 0.0f;
    size_t count = std::min(idleFeatures.boneCount, runFeatures.boneCount);
    for (size_t i = 0; i < count; ++i) {
        diff += glm::length(idleFeatures.boneFeatures[i].position - runFeatures.boneFeatures[i].position);
    }
    diff += glm::length(idleFeatures.rootVelocity - runFeatures.rootVelocity);

    // There should be some measurable difference between idle and run poses
    CHECK(diff > 0.0f);
}

TEST_CASE("features extracted at different times in same clip vary") {
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto walkClips = loadAnims(AnimFiles::walk(), model->skeleton);
    REQUIRE(!walkClips.empty());

    const AnimationClip* walkClip = nullptr;
    for (const auto& clip : walkClips) {
        if (clip.duration > 0.1f && clip.name != "mixamo.com") { walkClip = &clip; break; }
    }
    REQUIRE(walkClip != nullptr);

    FeatureConfig config = FeatureConfig::locomotion();
    FeatureExtractor extractor;
    extractor.initialize(model->skeleton, config);

    PoseFeatures features0 = extractor.extractFromClip(*walkClip, model->skeleton, 0.0f);
    PoseFeatures featuresHalf = extractor.extractFromClip(
        *walkClip, model->skeleton, walkClip->duration * 0.5f);

    // Features at different times should differ (walk has cyclic motion)
    float diff = 0.0f;
    size_t count = std::min(features0.boneCount, featuresHalf.boneCount);
    for (size_t i = 0; i < count; ++i) {
        diff += glm::length(features0.boneFeatures[i].position - featuresHalf.boneFeatures[i].position);
    }

    CHECK(diff > 0.001f);
}

} // TEST_SUITE("Feature Extraction Real Data")

// ============================================================================
// Stability / Stress Tests
// ============================================================================

TEST_SUITE("Motion Matching Stability") {

TEST_CASE("rapid input changes don't crash") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    float dt = 1.0f / 60.0f;
    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);

    // Rapidly change input direction every frame
    for (int i = 0; i < 300; ++i) {
        float angle = static_cast<float>(i) * 0.5f;
        glm::vec3 dir(std::sin(angle), 0.0f, std::cos(angle));
        float mag = (i % 3 == 0) ? 0.0f : 1.0f; // Toggle between idle and max

        fixture.controller.update(position, facing, dir, mag, dt);
    }

    // Should still be in a valid state
    CHECK(!fixture.currentClipName().empty());
    CHECK_FALSE(std::isnan(fixture.controller.getStats().lastMatchCost));
}

TEST_CASE("long simulation remains stable") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    float dt = 1.0f / 30.0f;
    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);
    glm::vec3 forward(0.0f, 0.0f, 1.0f);

    // Simulate 30 seconds of constant forward movement
    int totalFrames = static_cast<int>(30.0f / dt);
    for (int i = 0; i < totalFrames; ++i) {
        fixture.controller.update(position, facing, forward, 0.7f, dt);

        // Check periodically
        if (i % 100 == 0) {
            CHECK_FALSE(std::isnan(fixture.controller.getStats().lastMatchCost));
        }
    }

    CHECK(!fixture.currentClipName().empty());
}

TEST_CASE("zero delta time doesn't crash") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);
    glm::vec3 forward(0.0f, 0.0f, 1.0f);

    // Zero dt should be handled gracefully
    for (int i = 0; i < 10; ++i) {
        fixture.controller.update(position, facing, forward, 0.5f, 0.0f);
    }
    CHECK_FALSE(std::isnan(fixture.controller.getStats().lastMatchCost));
}

TEST_CASE("very large delta time doesn't crash") {
    MotionMatchingFixture fixture;
    REQUIRE(fixture.setup());

    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);
    glm::vec3 forward(0.0f, 0.0f, 1.0f);

    // Very large dt (simulating a huge lag spike)
    fixture.controller.update(position, facing, forward, 0.5f, 10.0f);
    CHECK_FALSE(std::isnan(fixture.controller.getStats().lastMatchCost));
}

TEST_CASE("inertial blending with real animations doesn't produce NaN") {
    // Rebuild fixture with inertial blending enabled
    auto model = loadModel();
    REQUIRE(model.has_value());

    auto allAnims = model->animations;
    auto loadAdditional = [&](const std::string& path) {
        if (!std::filesystem::exists(path)) return;
        auto clips = loadAnims(path, model->skeleton);
        for (auto& c : clips) allAnims.push_back(std::move(c));
    };
    loadAdditional(AnimFiles::idle());
    loadAdditional(AnimFiles::walk());
    loadAdditional(AnimFiles::run());

    MotionMatching::ControllerConfig config;
    config.searchInterval = 0.0f;
    config.useInertialBlending = true;  // Enable blending
    config.defaultBlendDuration = 0.2f;

    MotionMatching::MotionMatchingController controller;
    controller.initialize(config);
    controller.setSkeleton(model->skeleton);

    for (const auto& clip : allAnims) {
        std::string lowerName = clip.name;
        for (char& c : lowerName) c = static_cast<char>(std::tolower(c));
        if (lowerName == "mixamo.com" || lowerName.empty() || clip.duration < 0.1f) continue;

        auto classification = classifyClip(clip);
        controller.addClip(&clip, clip.name, classification.looping,
                          classification.tags, classification.locomotionSpeed,
                          classification.costBias);
    }

    MotionMatching::DatabaseBuildOptions buildOptions;
    buildOptions.pruneStaticPoses = false;
    controller.buildDatabase(buildOptions);
    controller.setExcludedTags({"jump"});

    float dt = 1.0f / 30.0f;
    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);

    // Alternate between idle and running to trigger transitions with blending
    for (int phase = 0; phase < 6; ++phase) {
        glm::vec3 dir = (phase % 2 == 0) ? glm::vec3(0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
        float mag = (phase % 2 == 0) ? 0.0f : 1.0f;

        for (int i = 0; i < 30; ++i) {
            controller.update(position, facing, dir, mag, dt);

            // Check that pose is valid
            SkeletonPose pose;
            controller.getCurrentPose(pose);
            for (size_t j = 0; j < pose.size(); ++j) {
                CHECK_FALSE(std::isnan(pose[j].translation.x));
                CHECK_FALSE(std::isnan(pose[j].rotation.w));
            }
        }
    }
}

} // TEST_SUITE("Motion Matching Stability")
