/**
 * Data-driven motion matching tests.
 *
 * These tests load real FBX animations and verify that the motion matching
 * system selects the correct animation given specific trajectories and poses.
 *
 * Design informed by:
 *  - Simon Clavet, "Motion Matching and The Road to Next-Gen Animation" (GDC 2016)
 *    Core technique: continuous search of mocap database matching current pose + future trajectory.
 *  - Kristjan Zadziuk, "Motion Matching: The Future of Games Animation... Today" (GDC 2016)
 *    "Dance cards": structured mocap capture patterns (circles, figure-8s, sudden stops).
 *  - Daniel Holden (orangeduck), Learned Motion Matching (SIGGRAPH 2020) & open-source impl.
 *    Feature vectors: root trajectory + foot positions/velocities; spring-damper trajectory model.
 *  - David Bollo, "Inertialization" (GDC 2018, Gears of War)
 *    Transition quality: decay offset rather than cross-fade.
 *  - Naughty Dog, "Motion Matching in The Last of Us Part II" (GDC 2021)
 *    Production lessons: database coverage validation and cost monitoring.
 *
 * Test categories:
 *  1. Trajectory-Driven Selection   – explicit trajectories → correct animation type
 *  2. Speed Discrimination          – parametric speed sweep across walk/run boundary
 *  3. Direction Discrimination      – lateral, diagonal, backward inputs
 *  4. Dance Card Scenarios          – Zadziuk-inspired movement patterns
 *  5. Cost Function Validation      – ordering, decomposition, symmetry
 *  6. KD-Tree vs Brute Force        – correctness verification with real data
 *  7. Feature Normalization          – statistical properties from real database
 *  8. Locomotion Transitions         – multi-phase idle→walk→run→idle sequences
 *  9. Regression Tests               – golden value stability
 */

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
#include <numeric>

// ============================================================================
// Shared test infrastructure
// ============================================================================

namespace {

const std::string ASSETS_DIR = "assets/characters/fbx/";
const std::string MODEL_PATH = ASSETS_DIR + "Y Bot.fbx";

// Animation file paths
struct AnimFiles {
    static std::string idle()     { return ASSETS_DIR + "sword and shield idle.fbx"; }
    static std::string idle2()    { return ASSETS_DIR + "sword and shield idle (2).fbx"; }
    static std::string walk()     { return ASSETS_DIR + "sword and shield walk.fbx"; }
    static std::string walk2()    { return ASSETS_DIR + "sword and shield walk (2).fbx"; }
    static std::string run()      { return ASSETS_DIR + "sword and shield run.fbx"; }
    static std::string run2()     { return ASSETS_DIR + "sword and shield run (2).fbx"; }
    static std::string strafe()   { return ASSETS_DIR + "sword and shield strafe.fbx"; }
    static std::string strafe2()  { return ASSETS_DIR + "sword and shield strafe (2).fbx"; }
    static std::string strafe3()  { return ASSETS_DIR + "sword and shield strafe (3).fbx"; }
    static std::string strafe4()  { return ASSETS_DIR + "sword and shield strafe (4).fbx"; }
    static std::string turn()     { return ASSETS_DIR + "sword and shield turn.fbx"; }
    static std::string turn180()  { return ASSETS_DIR + "sword and shield 180 turn.fbx"; }
    static std::string jump()     { return ASSETS_DIR + "sword and shield jump.fbx"; }
    static std::string jump2()    { return ASSETS_DIR + "sword and shield jump (2).fbx"; }
};

// Locomotion speed constants (matching AnimatedCharacter::initializeMotionMatching)
constexpr float IDLE_SPEED   = 0.0f;
constexpr float WALK_SPEED   = 1.4f;
constexpr float RUN_SPEED    = 5.0f;
constexpr float STRAFE_SPEED = 1.8f;
constexpr float TURN_SPEED   = 0.5f;

// Case-insensitive substring check
bool containsCI(const std::string& str, const std::string& substr) {
    std::string lower = str;
    std::string lowerSub = substr;
    for (char& c : lower) c = static_cast<char>(std::tolower(c));
    for (char& c : lowerSub) c = static_cast<char>(std::tolower(c));
    return lower.find(lowerSub) != std::string::npos;
}

bool modelExists() { return std::filesystem::exists(MODEL_PATH); }

std::optional<GLTFSkinnedLoadResult> loadModel() {
    if (!modelExists()) return std::nullopt;
    return FBXLoader::loadSkinned(MODEL_PATH);
}

std::vector<AnimationClip> loadAnims(const std::string& path, const Skeleton& skeleton) {
    if (!std::filesystem::exists(path)) return {};
    return FBXLoader::loadAnimations(path, skeleton);
}

// Clip classification (mirrors AnimatedCharacter::initializeMotionMatching)
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

    if (lowerName == "mixamo.com" || lowerName.empty() || clip.duration < 0.1f) {
        return result;
    }

    bool isVariant = (lowerName.find("2") != std::string::npos ||
                     lowerName.find("_2") != std::string::npos ||
                     lowerName.find("alt") != std::string::npos);
    if (isVariant) result.costBias = 0.5f;

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

// Full test fixture: loads model + animations, builds motion matching database
struct MotionMatchingFixture {
    Skeleton skeleton;
    std::vector<AnimationClip> allAnimations;
    MotionMatchingController controller;
    bool valid = false;

    bool setup() {
        auto modelResult = loadModel();
        if (!modelResult) return false;

        skeleton = std::move(modelResult->skeleton);
        allAnimations = std::move(modelResult->animations);

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

        ControllerConfig config;
        config.searchInterval = 0.0f;           // Search every frame for determinism
        config.useInertialBlending = false;      // Disable blending for cleaner results
        controller.initialize(config);
        controller.setSkeleton(skeleton);

        for (size_t i = 0; i < allAnimations.size(); ++i) {
            const auto& clip = allAnimations[i];
            auto classification = classifyClip(clip);
            std::string lowerName = clip.name;
            for (char& c : lowerName) c = static_cast<char>(std::tolower(c));
            if (lowerName == "mixamo.com" || lowerName.empty() || clip.duration < 0.1f) continue;

            controller.addClip(&clip, clip.name, classification.looping,
                             classification.tags, classification.locomotionSpeed,
                             classification.costBias);
        }

        DatabaseBuildOptions buildOptions;
        buildOptions.defaultSampleRate = 30.0f;
        buildOptions.pruneStaticPoses = false;
        controller.buildDatabase(buildOptions);
        controller.setExcludedTags({"jump"});

        valid = true;
        return true;
    }

    // Simulate with constant input, return selected clip name
    std::string simulate(const glm::vec3& inputDirection, float inputMagnitude,
                        float duration = 1.0f, float dt = 1.0f / 30.0f) {
        glm::vec3 position(0.0f);
        glm::vec3 facing(0.0f, 0.0f, 1.0f);

        int frames = static_cast<int>(duration / dt);
        for (int i = 0; i < frames; ++i) {
            controller.update(position, facing, inputDirection, inputMagnitude, dt);
        }
        return currentClipName();
    }

    // Multi-phase simulation: run multiple input phases sequentially
    struct InputPhase {
        glm::vec3 direction;
        float magnitude;
        float duration;
    };

    std::vector<std::string> simulatePhases(const std::vector<InputPhase>& phases,
                                             float dt = 1.0f / 30.0f) {
        std::vector<std::string> clipHistory;
        glm::vec3 position(0.0f);
        glm::vec3 facing(0.0f, 0.0f, 1.0f);

        for (const auto& phase : phases) {
            int frames = static_cast<int>(phase.duration / dt);
            for (int i = 0; i < frames; ++i) {
                controller.update(position, facing, phase.direction, phase.magnitude, dt);
            }
            clipHistory.push_back(currentClipName());
        }
        return clipHistory;
    }

    std::string currentClipName() const {
        const auto& db = controller.getDatabase();
        if (!db.isBuilt() || db.getClipCount() == 0) return "";
        const auto& playback = controller.getPlaybackState();
        if (playback.clipIndex >= db.getClipCount()) return "";
        return db.getClip(playback.clipIndex).name;
    }

    // Build a trajectory for a given constant velocity and facing
    Trajectory buildTrajectory(const glm::vec3& velocity, const glm::vec3& facing) const {
        Trajectory traj;
        // Use the standard sample times from FeatureConfig::locomotion()
        std::vector<float> sampleTimes = {-0.2f, -0.1f, 0.1f, 0.2f, 0.4f, 0.6f};
        for (float t : sampleTimes) {
            TrajectorySample s;
            s.timeOffset = t;
            s.position = velocity * t;      // linear extrapolation
            s.velocity = velocity;
            s.facing = glm::normalize(facing);
            traj.addSample(s);
        }
        return traj;
    }
};

// Determine the "animation type" from a clip name for easier assertions
enum class AnimType { Idle, Walk, Run, Strafe, Turn, Jump, Unknown };

AnimType classifyName(const std::string& name) {
    std::string lower = name;
    for (char& c : lower) c = static_cast<char>(std::tolower(c));
    if (lower.find("idle") != std::string::npos) return AnimType::Idle;
    if (lower.find("run") != std::string::npos) return AnimType::Run;
    if (lower.find("walk") != std::string::npos) return AnimType::Walk;
    if (lower.find("strafe") != std::string::npos) return AnimType::Strafe;
    if (lower.find("turn") != std::string::npos) return AnimType::Turn;
    if (lower.find("jump") != std::string::npos) return AnimType::Jump;
    return AnimType::Unknown;
}

const char* animTypeName(AnimType t) {
    switch (t) {
        case AnimType::Idle: return "Idle";
        case AnimType::Walk: return "Walk";
        case AnimType::Run: return "Run";
        case AnimType::Strafe: return "Strafe";
        case AnimType::Turn: return "Turn";
        case AnimType::Jump: return "Jump";
        case AnimType::Unknown: return "Unknown";
    }
    return "Unknown";
}

} // anonymous namespace

// ============================================================================
// 1. Trajectory-Driven Animation Selection
//    (Clavet GDC 2016: "continuously find the frame that simultaneously matches
//     the current pose and the desired future plan")
// ============================================================================

TEST_SUITE("Trajectory-Driven Selection") {

TEST_CASE("zero-velocity trajectory selects idle") {
    // A stationary trajectory (no movement) should select an idle animation.
    // This validates the most basic motion matching invariant: still character → idle.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    // Stationary trajectory
    Trajectory traj = f.buildTrajectory(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    PoseFeatures queryPose;
    queryPose.rootVelocity = glm::vec3(0.0f);

    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};

    auto result = matcher.findBestMatch(traj, queryPose, opts);
    REQUIRE(result.isValid());

    INFO("Selected: " << result.clip->name);
    AnimType type = classifyName(result.clip->name);
    CHECK(type == AnimType::Idle);
}

TEST_CASE("walk-speed trajectory selects walk animation") {
    // A trajectory at walk speed (~1.4 m/s forward) should select a walk clip.
    // Validates speed-based locomotion discrimination (Clavet: trajectory determines type).
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    glm::vec3 walkVelocity(0.0f, 0.0f, WALK_SPEED);
    Trajectory traj = f.buildTrajectory(walkVelocity, glm::vec3(0.0f, 0.0f, 1.0f));

    PoseFeatures queryPose;
    queryPose.rootVelocity = walkVelocity;

    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};

    auto result = matcher.findBestMatch(traj, queryPose, opts);
    REQUIRE(result.isValid());

    INFO("Selected: " << result.clip->name);
    AnimType type = classifyName(result.clip->name);
    // Should be walk (or at least locomotion, not idle)
    CHECK((type == AnimType::Walk || type == AnimType::Strafe));
}

TEST_CASE("run-speed trajectory selects run animation") {
    // A trajectory at run speed (~5.0 m/s forward) should select a run clip.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    glm::vec3 runVelocity(0.0f, 0.0f, RUN_SPEED);
    Trajectory traj = f.buildTrajectory(runVelocity, glm::vec3(0.0f, 0.0f, 1.0f));

    PoseFeatures queryPose;
    queryPose.rootVelocity = runVelocity;

    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};

    auto result = matcher.findBestMatch(traj, queryPose, opts);
    REQUIRE(result.isValid());

    INFO("Selected: " << result.clip->name);
    AnimType type = classifyName(result.clip->name);
    // At 5.0 m/s (run speed), the system should clearly select a run animation
    // since the trajectory velocity matches run clips far better than walk (1.4 m/s)
    CHECK(type == AnimType::Run);
}

TEST_CASE("run-speed trajectory has lower cost for run clips than idle clips") {
    // The fundamental cost ordering: a run-speed query should have strictly lower
    // cost against run poses than against idle poses.
    // (Holden: feature vector distance defines match quality)
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    glm::vec3 runVelocity(0.0f, 0.0f, RUN_SPEED);
    Trajectory traj = f.buildTrajectory(runVelocity, glm::vec3(0.0f, 0.0f, 1.0f));

    PoseFeatures queryPose;
    queryPose.rootVelocity = runVelocity;

    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};

    // Best match with only run clips
    opts.requiredTags = {"run"};
    auto runResult = matcher.findBestMatch(traj, queryPose, opts);

    // Best match with only idle clips
    opts.requiredTags = {"idle"};
    auto idleResult = matcher.findBestMatch(traj, queryPose, opts);

    REQUIRE(runResult.isValid());
    REQUIRE(idleResult.isValid());

    INFO("Run cost:  " << runResult.cost);
    INFO("Idle cost: " << idleResult.cost);
    CHECK(runResult.cost < idleResult.cost);
}

TEST_CASE("idle trajectory has lower cost for idle clips than run clips") {
    // Converse of above: stationary query should prefer idle over run.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    Trajectory traj = f.buildTrajectory(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    PoseFeatures queryPose;
    queryPose.rootVelocity = glm::vec3(0.0f);

    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};

    opts.requiredTags = {"idle"};
    auto idleResult = matcher.findBestMatch(traj, queryPose, opts);

    opts.requiredTags = {"run"};
    auto runResult = matcher.findBestMatch(traj, queryPose, opts);

    REQUIRE(idleResult.isValid());
    REQUIRE(runResult.isValid());

    INFO("Idle cost: " << idleResult.cost);
    INFO("Run cost:  " << runResult.cost);
    CHECK(idleResult.cost < runResult.cost);
}

} // TEST_SUITE("Trajectory-Driven Selection")

// ============================================================================
// 2. Speed Discrimination
//    (Clavet GDC 2016: trajectory array encodes speed changes; without it the
//     system only finds full-speed frames)
// ============================================================================

TEST_SUITE("Speed Discrimination") {

TEST_CASE("parametric speed sweep: idle vs locomotion boundary") {
    // Sweep speeds from 0 to RUN_SPEED. At zero speed we should get idle;
    // at walk speed or above we should get locomotion.
    // This tests the "responsiveness vs quality" tradeoff (Clavet/Holden).
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    struct SpeedTest {
        float speed;
        AnimType minExpected; // Idle = expect idle, Walk = expect walk+, Run = expect run
    };

    std::vector<SpeedTest> tests = {
        {0.0f,  AnimType::Idle},   // stationary → idle
        {0.2f,  AnimType::Idle},   // very slow → idle (below walk threshold)
        {1.0f,  AnimType::Walk},   // near walk speed → walk or faster locomotion
        {WALK_SPEED, AnimType::Walk},  // walk speed → walk
        {3.0f,  AnimType::Walk},   // between walk and run → walk or run
        {RUN_SPEED, AnimType::Run},    // run speed → run
    };

    for (const auto& test : tests) {
        std::string selected = f.simulate(glm::vec3(0.0f, 0.0f, 1.0f), test.speed / 6.0f, 2.0f);
        AnimType type = classifyName(selected);

        INFO("Speed: " << test.speed << " → " << selected << " (" << animTypeName(type) << ")");

        if (test.minExpected == AnimType::Idle) {
            CHECK(type == AnimType::Idle);
        } else if (test.minExpected == AnimType::Run) {
            // At run speed, should specifically select run (not walk/strafe)
            CHECK(type == AnimType::Run);
        } else {
            // At walk-range speeds, any locomotion is acceptable
            CHECK((type == AnimType::Walk || type == AnimType::Run ||
                   type == AnimType::Strafe || type == AnimType::Turn));
        }

        // Reset for next test by re-initializing controller state
        f.controller.forceSearch();
    }
}

TEST_CASE("walk-speed cost is lower than run-speed cost for walk queries") {
    // When querying at walk speed, walk clips should have lower cost than run clips.
    // This validates the feature normalization and cost function work together to
    // discriminate speed (Holden: normalized features enable fair comparison).
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    glm::vec3 walkVel(0.0f, 0.0f, WALK_SPEED);
    Trajectory traj = f.buildTrajectory(walkVel, glm::vec3(0.0f, 0.0f, 1.0f));

    PoseFeatures queryPose;
    queryPose.rootVelocity = walkVel;

    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};

    opts.requiredTags = {"walk"};
    auto walkResult = matcher.findBestMatch(traj, queryPose, opts);

    opts.requiredTags = {"run"};
    auto runResult = matcher.findBestMatch(traj, queryPose, opts);

    REQUIRE(walkResult.isValid());
    REQUIRE(runResult.isValid());

    INFO("Walk cost for walk query: " << walkResult.cost);
    INFO("Run cost for walk query: " << runResult.cost);
    CHECK(walkResult.cost < runResult.cost);
}

} // TEST_SUITE("Speed Discrimination")

// ============================================================================
// 3. Direction Discrimination
//    (Zadziuk GDC 2016: dance cards include lateral movement, strafing, turns)
// ============================================================================

TEST_SUITE("Direction Discrimination") {

TEST_CASE("forward input selects forward locomotion") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    std::string selected = f.simulate(glm::vec3(0.0f, 0.0f, 1.0f), 0.5f, 2.0f);
    AnimType type = classifyName(selected);

    INFO("Forward → " << selected);
    // Forward input should select forward locomotion (walk or run), not strafe
    CHECK((type == AnimType::Walk || type == AnimType::Run));
}

TEST_CASE("lateral input selects strafe or locomotion") {
    // Pure lateral movement should favor strafe animations.
    // (Zadziuk: capture lateral movement patterns in dance cards)
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    std::string selected = f.simulate(glm::vec3(1.0f, 0.0f, 0.0f), 0.5f, 2.0f);
    AnimType type = classifyName(selected);

    INFO("Right lateral → " << selected);
    // Pure lateral movement should favor strafe animations (designed for sideways motion)
    // Walk is also acceptable since it has similar speed (1.4 vs 1.8 m/s)
    CHECK((type == AnimType::Strafe || type == AnimType::Walk));
}

TEST_CASE("left lateral input selects strafe or locomotion") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    std::string selected = f.simulate(glm::vec3(-1.0f, 0.0f, 0.0f), 0.5f, 2.0f);
    AnimType type = classifyName(selected);

    INFO("Left lateral → " << selected);
    // Pure lateral movement should favor strafe animations
    CHECK((type == AnimType::Strafe || type == AnimType::Walk));
}

TEST_CASE("diagonal forward-left selects locomotion") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    glm::vec3 diag = glm::normalize(glm::vec3(-1.0f, 0.0f, 1.0f));
    std::string selected = f.simulate(diag, 0.5f, 2.0f);
    AnimType type = classifyName(selected);

    INFO("Diagonal forward-left → " << selected);
    // Diagonal input should select locomotion — walk, run, or strafe
    CHECK((type == AnimType::Walk || type == AnimType::Run || type == AnimType::Strafe));
}

TEST_CASE("no input after movement returns to idle") {
    // After moving forward, releasing input should transition back to idle.
    // (Clavet: trajectory array naturally ramps down speed → selects deceleration/idle)
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    auto clips = f.simulatePhases({
        {glm::vec3(0.0f, 0.0f, 1.0f), 1.0f, 2.0f},  // Move forward
        {glm::vec3(0.0f),               0.0f, 3.0f},  // Stop
    });

    REQUIRE(clips.size() == 2);
    AnimType movingType = classifyName(clips[0]);
    AnimType stoppedType = classifyName(clips[1]);

    INFO("Moving: " << clips[0] << ", Stopped: " << clips[1]);
    CHECK(movingType != AnimType::Idle);
    CHECK(stoppedType == AnimType::Idle);
}

} // TEST_SUITE("Direction Discrimination")

// ============================================================================
// 4. Dance Card Scenarios
//    (Zadziuk GDC 2016: "dance cards" – structured mocap patterns for coverage
//     testing: circles, figure-8s, sudden stops, 180-degree reversals)
// ============================================================================

TEST_SUITE("Dance Card Scenarios") {

TEST_CASE("idle → walk → run ramp: acceleration profile") {
    // Zadziuk dance card: gradual acceleration from standing to full speed.
    // Each phase should select progressively faster animations.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    auto clips = f.simulatePhases({
        {glm::vec3(0.0f),               0.0f, 1.5f},  // Idle
        {glm::vec3(0.0f, 0.0f, 1.0f),   0.3f, 1.5f},  // Slow walk
        {glm::vec3(0.0f, 0.0f, 1.0f),   1.0f, 1.5f},  // Full run
    });

    REQUIRE(clips.size() == 3);
    AnimType idleType = classifyName(clips[0]);
    AnimType slowType = classifyName(clips[1]);
    AnimType fastType = classifyName(clips[2]);

    INFO("Idle: " << clips[0] << ", Slow: " << clips[1] << ", Fast: " << clips[2]);

    CHECK(idleType == AnimType::Idle);
    // Slow phase (magnitude 0.3 → 1.8 m/s) should select walk-range locomotion
    CHECK((slowType == AnimType::Walk || slowType == AnimType::Strafe || slowType == AnimType::Run));
    // Fast phase (magnitude 1.0 → 6.0 m/s) should select run animation
    CHECK(fastType == AnimType::Run);
}

TEST_CASE("run → idle deceleration: sudden stop from full speed") {
    // Zadziuk dance card: sudden stop. The system should transition from run to idle.
    // (Clavet: "when a character needs to stop, the system finds frames showing
    //  natural deceleration – taking extra steps, weight shifting")
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    auto clips = f.simulatePhases({
        {glm::vec3(0.0f, 0.0f, 1.0f),   1.0f, 2.0f},  // Full run
        {glm::vec3(0.0f),                0.0f, 3.0f},  // Sudden stop
    });

    REQUIRE(clips.size() == 2);
    AnimType runType = classifyName(clips[0]);
    AnimType stopType = classifyName(clips[1]);

    INFO("Running: " << clips[0] << ", After stop: " << clips[1]);
    CHECK(runType != AnimType::Idle);
    CHECK(stopType == AnimType::Idle);
}

TEST_CASE("direction reversal: forward then backward") {
    // Zadziuk dance card: 180-degree direction reversal.
    // Both phases should select locomotion (not idle), and the system should
    // not crash or produce NaN during the reversal.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    float dt = 1.0f / 30.0f;
    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);

    // Forward phase
    for (int i = 0; i < 60; ++i) {
        f.controller.update(position, facing, glm::vec3(0.0f, 0.0f, 1.0f), 0.7f, dt);
    }
    std::string forwardClip = f.currentClipName();

    // Sudden reversal
    for (int i = 0; i < 60; ++i) {
        f.controller.update(position, facing, glm::vec3(0.0f, 0.0f, -1.0f), 0.7f, dt);
        // No NaN during reversal
        CHECK_FALSE(std::isnan(f.controller.getStats().lastMatchCost));
    }
    std::string reversedClip = f.currentClipName();

    INFO("Forward: " << forwardClip << ", Reversed: " << reversedClip);
    CHECK(!forwardClip.empty());
    CHECK(!reversedClip.empty());
    // Both phases have magnitude 0.7 input, so both should be in locomotion
    AnimType fwdType = classifyName(forwardClip);
    AnimType revType = classifyName(reversedClip);
    CHECK(fwdType != AnimType::Idle);
    CHECK(revType != AnimType::Idle);
}

TEST_CASE("circular path: constant turning input") {
    // Zadziuk dance card: running in circles of various diameters.
    // The system should maintain locomotion and not oscillate or crash.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    float dt = 1.0f / 30.0f;
    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);

    int nanCount = 0;
    int idleCount = 0;
    int totalFrames = 180; // 6 seconds at 30fps

    for (int i = 0; i < totalFrames; ++i) {
        // Rotate input direction over time (one full circle in ~3 seconds)
        float angle = static_cast<float>(i) * (2.0f * 3.14159f / 90.0f);
        glm::vec3 dir(std::sin(angle), 0.0f, std::cos(angle));

        f.controller.update(position, facing, dir, 0.6f, dt);

        if (std::isnan(f.controller.getStats().lastMatchCost)) nanCount++;

        std::string clip = f.currentClipName();
        if (classifyName(clip) == AnimType::Idle) idleCount++;
    }

    CHECK(nanCount == 0);
    // During circular movement, the system should be in locomotion most of the time.
    // With continuous input at magnitude 0.6 (3.6 m/s), idle selection indicates
    // the system is failing to match locomotion — only brief direction changes should idle.
    float idleFraction = static_cast<float>(idleCount) / static_cast<float>(totalFrames);
    INFO("Idle fraction during circular movement: " << idleFraction);
    CHECK(idleFraction < 0.15f);
}

TEST_CASE("figure-eight pattern: alternating turns") {
    // Zadziuk dance card: figure-8 pattern tests alternating left/right turns.
    // System should handle smooth direction transitions without instability.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    float dt = 1.0f / 30.0f;
    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);

    int totalFrames = 240; // 8 seconds
    bool hadNaN = false;
    int idleCount = 0;

    for (int i = 0; i < totalFrames; ++i) {
        // Figure-8: sin with different frequencies on x and z
        float t = static_cast<float>(i) * dt;
        float angle = std::sin(t * 2.0f) * 1.5f; // oscillating angle
        glm::vec3 dir(std::sin(angle), 0.0f, std::cos(angle));

        f.controller.update(position, facing, dir, 0.5f, dt);

        if (std::isnan(f.controller.getStats().lastMatchCost)) hadNaN = true;
        if (classifyName(f.currentClipName()) == AnimType::Idle) idleCount++;
    }

    CHECK_FALSE(hadNaN);
    CHECK(!f.currentClipName().empty());
    // With continuous input at magnitude 0.5, should stay in locomotion
    float idleFraction = static_cast<float>(idleCount) / static_cast<float>(totalFrames);
    INFO("Idle fraction during figure-8: " << idleFraction);
    CHECK(idleFraction < 0.15f);
}

TEST_CASE("rapid direction oscillation: stress test") {
    // Extreme test: flip direction every few frames.
    // This stresses the trajectory prediction and search stability.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    float dt = 1.0f / 30.0f;
    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);

    int nanCount = 0;
    int idleCount = 0;
    int totalFrames = 300;
    for (int i = 0; i < totalFrames; ++i) {
        // Flip direction every 5 frames
        float sign = ((i / 5) % 2 == 0) ? 1.0f : -1.0f;
        glm::vec3 dir(0.0f, 0.0f, sign);

        f.controller.update(position, facing, dir, 0.8f, dt);
        if (std::isnan(f.controller.getStats().lastMatchCost)) nanCount++;
        if (classifyName(f.currentClipName()) == AnimType::Idle) idleCount++;
    }

    CHECK(nanCount == 0);
    CHECK(!f.currentClipName().empty());
    // With high magnitude input (0.8), should stay in locomotion despite direction flips
    float idleFraction = static_cast<float>(idleCount) / static_cast<float>(totalFrames);
    INFO("Idle fraction during oscillation: " << idleFraction);
    CHECK(idleFraction < 0.2f);
}

} // TEST_SUITE("Dance Card Scenarios")

// ============================================================================
// 5. Cost Function Validation
//    (Holden: "Cost = Sum_i(weight_i * distance_i(query_i, candidate_i))")
//    Validates cost ordering, decomposition, and bias effects.
// ============================================================================

TEST_SUITE("Cost Function Validation") {

TEST_CASE("cost decomposition: components are valid and discriminative") {
    // The total cost uses normalized features, while the component breakdown
    // (trajectoryCost, poseCost) uses unnormalized features as diagnostics.
    // We verify: (1) components are finite and non-negative, (2) trajectory cost
    // discriminates between matching and non-matching animation types.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    glm::vec3 walkVel(0.0f, 0.0f, WALK_SPEED);
    Trajectory traj = f.buildTrajectory(walkVel, glm::vec3(0.0f, 0.0f, 1.0f));
    PoseFeatures queryPose;
    queryPose.rootVelocity = walkVel;

    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};

    auto results = matcher.findTopMatches(traj, queryPose, 20, opts);
    REQUIRE(!results.empty());

    // Track trajectory costs by animation type for discrimination check
    float bestWalkTrajCost = std::numeric_limits<float>::max();
    float bestIdleTrajCost = std::numeric_limits<float>::max();

    for (const auto& r : results) {
        INFO("Total: " << r.cost << " clip: " << r.clip->name
             << " (traj=" << r.trajectoryCost << " pose=" << r.poseCost
             << " heading=" << r.headingCost << " bias=" << r.biasCost << ")");

        // All components must be finite
        CHECK_FALSE(std::isnan(r.cost));
        CHECK_FALSE(std::isinf(r.cost));
        CHECK_FALSE(std::isnan(r.trajectoryCost));
        CHECK_FALSE(std::isnan(r.poseCost));

        // Unnormalized trajectory and pose costs are squared distances: non-negative
        CHECK(r.trajectoryCost >= 0.0f);
        CHECK(r.poseCost >= 0.0f);

        AnimType type = classifyName(r.clip->name);
        if (type == AnimType::Walk && r.trajectoryCost < bestWalkTrajCost)
            bestWalkTrajCost = r.trajectoryCost;
        if (type == AnimType::Idle && r.trajectoryCost < bestIdleTrajCost)
            bestIdleTrajCost = r.trajectoryCost;
    }

    // For a walk-speed query, the unnormalized trajectory cost should be lower
    // for walk clips than for idle clips (trajectory velocity mismatch)
    if (bestWalkTrajCost < std::numeric_limits<float>::max() &&
        bestIdleTrajCost < std::numeric_limits<float>::max()) {
        INFO("Best walk traj cost: " << bestWalkTrajCost
             << ", Best idle traj cost: " << bestIdleTrajCost);
        CHECK(bestWalkTrajCost < bestIdleTrajCost);
    }
}

TEST_CASE("continuing pose bias reduces cost for same clip") {
    // (Clavet GDC 2016 / Unreal: "Continuing Pose Cost Bias" – negative bias
    //  favors staying in the current animation for stability)
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    Trajectory traj = f.buildTrajectory(glm::vec3(0.0f, 0.0f, WALK_SPEED),
                                         glm::vec3(0.0f, 0.0f, 1.0f));
    PoseFeatures queryPose;
    queryPose.rootVelocity = glm::vec3(0.0f, 0.0f, WALK_SPEED);

    // Cost without continuing bias
    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};
    opts.continuingPoseCostBias = 0.0f;

    float costNoBias = matcher.computeCost(0, traj, queryPose, opts);

    // Cost with strong continuing bias for the same clip
    opts.continuingPoseCostBias = -5.0f;
    opts.currentClipIndex = 0;

    float costWithBias = matcher.computeCost(0, traj, queryPose, opts);

    INFO("Without bias: " << costNoBias << ", With bias: " << costWithBias);
    CHECK(costWithBias < costNoBias);
}

TEST_CASE("top matches are sorted by ascending cost") {
    // Basic invariant: findTopMatches should return results sorted by cost.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    Trajectory traj = f.buildTrajectory(glm::vec3(0.0f, 0.0f, 2.0f),
                                         glm::vec3(0.0f, 0.0f, 1.0f));
    PoseFeatures queryPose;
    queryPose.rootVelocity = glm::vec3(0.0f, 0.0f, 2.0f);

    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};

    auto results = matcher.findTopMatches(traj, queryPose, 20, opts);
    REQUIRE(results.size() >= 2);

    for (size_t i = 1; i < results.size(); ++i) {
        CHECK(results[i].cost >= results[i - 1].cost);
    }
}

TEST_CASE("cost is finite and well-ordered") {
    // With normalization, the cost function uses mean-centered features
    // (Holden: "(value - mean) / stdDev"), so individual costs can be negative.
    // We verify that costs are finite, non-NaN, and that the best match
    // has a lower cost than the worst.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    Trajectory traj = f.buildTrajectory(glm::vec3(0.0f, 0.0f, 1.0f),
                                         glm::vec3(0.0f, 0.0f, 1.0f));
    PoseFeatures queryPose;

    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};
    opts.continuingPoseCostBias = 0.0f;
    opts.loopingCostBias = 0.0f;

    const auto& db = f.controller.getDatabase();
    float minCost = std::numeric_limits<float>::max();
    float maxCost = std::numeric_limits<float>::lowest();

    for (size_t i = 0; i < std::min(db.getPoseCount(), size_t(100)); ++i) {
        float cost = matcher.computeCost(i, traj, queryPose, opts);
        CHECK_FALSE(std::isnan(cost));
        CHECK_FALSE(std::isinf(cost));
        if (cost < minCost) minCost = cost;
        if (cost > maxCost) maxCost = cost;
    }

    INFO("Min cost: " << minCost << ", Max cost: " << maxCost);
    // There should be variance in costs (not all identical)
    CHECK(maxCost > minCost);
}

} // TEST_SUITE("Cost Function Validation")

// ============================================================================
// 6. KD-Tree vs Brute Force Consistency
//    (Clavet GDC 2016: "use a KD-tree to speed up the search, where the cost
//     function becomes a distance function in the tree")
// ============================================================================

TEST_SUITE("KD-Tree vs Brute Force") {

TEST_CASE("idle query: KD-tree matches brute force") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    Trajectory traj = f.buildTrajectory(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    PoseFeatures queryPose;

    SearchOptions bf;
    bf.useKDTree = false;
    bf.excludedTags = {"jump"};

    SearchOptions kd;
    kd.useKDTree = true;
    kd.kdTreeCandidates = 128;
    kd.excludedTags = {"jump"};

    auto bfResult = matcher.findBestMatch(traj, queryPose, bf);
    auto kdResult = matcher.findBestMatch(traj, queryPose, kd);

    REQUIRE(bfResult.isValid());
    REQUIRE(kdResult.isValid());

    INFO("BF cost: " << bfResult.cost << " clip: " << bfResult.clip->name);
    INFO("KD cost: " << kdResult.cost << " clip: " << kdResult.clip->name);
    // KD-tree is an approximation (searches K nearest in feature space,
    // then evaluates full cost), so allow a small tolerance
    CHECK(kdResult.cost == doctest::Approx(bfResult.cost).epsilon(0.15f));
}

TEST_CASE("walk query: KD-tree matches brute force") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    glm::vec3 walkVel(0.0f, 0.0f, WALK_SPEED);
    Trajectory traj = f.buildTrajectory(walkVel, glm::vec3(0.0f, 0.0f, 1.0f));
    PoseFeatures queryPose;
    queryPose.rootVelocity = walkVel;

    SearchOptions bf;
    bf.useKDTree = false;
    bf.excludedTags = {"jump"};

    SearchOptions kd;
    kd.useKDTree = true;
    kd.kdTreeCandidates = 128;
    kd.excludedTags = {"jump"};

    auto bfResult = matcher.findBestMatch(traj, queryPose, bf);
    auto kdResult = matcher.findBestMatch(traj, queryPose, kd);

    REQUIRE(bfResult.isValid());
    REQUIRE(kdResult.isValid());

    INFO("BF cost: " << bfResult.cost << " clip: " << bfResult.clip->name);
    INFO("KD cost: " << kdResult.cost << " clip: " << kdResult.clip->name);
    // KD-tree is an approximation (searches K nearest in feature space,
    // then evaluates full cost), so allow a small tolerance
    CHECK(kdResult.cost == doctest::Approx(bfResult.cost).epsilon(0.15f));
}

TEST_CASE("run query: KD-tree matches brute force") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    glm::vec3 runVel(0.0f, 0.0f, RUN_SPEED);
    Trajectory traj = f.buildTrajectory(runVel, glm::vec3(0.0f, 0.0f, 1.0f));
    PoseFeatures queryPose;
    queryPose.rootVelocity = runVel;

    SearchOptions bf;
    bf.useKDTree = false;
    bf.excludedTags = {"jump"};

    SearchOptions kd;
    kd.useKDTree = true;
    kd.kdTreeCandidates = 128;
    kd.excludedTags = {"jump"};

    auto bfResult = matcher.findBestMatch(traj, queryPose, bf);
    auto kdResult = matcher.findBestMatch(traj, queryPose, kd);

    REQUIRE(bfResult.isValid());
    REQUIRE(kdResult.isValid());

    INFO("BF cost: " << bfResult.cost << " clip: " << bfResult.clip->name);
    INFO("KD cost: " << kdResult.cost << " clip: " << kdResult.clip->name);
    // KD-tree is an approximation (searches K nearest in feature space,
    // then evaluates full cost), so allow a small tolerance
    CHECK(kdResult.cost == doctest::Approx(bfResult.cost).epsilon(0.15f));
}

TEST_CASE("multiple queries: KD-tree and brute force select same animation type") {
    // For various trajectory speeds, verify KD-tree selects the same animation
    // type as brute force. This is a softer check (type rather than exact pose)
    // since KD-tree is an approximation.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    std::vector<glm::vec3> queryVelocities = {
        {0.0f, 0.0f, 0.0f},          // idle
        {0.0f, 0.0f, WALK_SPEED},     // walk
        {0.0f, 0.0f, RUN_SPEED},      // run
        {STRAFE_SPEED, 0.0f, 0.0f},   // lateral
    };

    for (const auto& vel : queryVelocities) {
        Trajectory traj = f.buildTrajectory(vel, glm::vec3(0.0f, 0.0f, 1.0f));
        PoseFeatures queryPose;
        queryPose.rootVelocity = vel;

        SearchOptions bf;
        bf.useKDTree = false;
        bf.excludedTags = {"jump"};

        SearchOptions kd;
        kd.useKDTree = true;
        kd.kdTreeCandidates = 128;
        kd.excludedTags = {"jump"};

        auto bfResult = matcher.findBestMatch(traj, queryPose, bf);
        auto kdResult = matcher.findBestMatch(traj, queryPose, kd);

        REQUIRE(bfResult.isValid());
        REQUIRE(kdResult.isValid());

        AnimType bfType = classifyName(bfResult.clip->name);
        AnimType kdType = classifyName(kdResult.clip->name);

        INFO("Velocity (" << vel.x << "," << vel.y << "," << vel.z << "): "
             << "BF=" << bfResult.clip->name << " KD=" << kdResult.clip->name);
        CHECK(bfType == kdType);
    }
}

} // TEST_SUITE("KD-Tree vs Brute Force")

// ============================================================================
// 7. Feature Normalization Properties
//    (Holden: "Features must be standardized (zero mean, unit variance) before
//     comparison. Without normalization, features with naturally larger magnitudes
//     would dominate the cost function.")
// ============================================================================

TEST_SUITE("Feature Normalization Properties") {

TEST_CASE("normalization statistics are computed") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    const auto& norm = f.controller.getDatabase().getNormalization();
    CHECK(norm.isComputed);
}

TEST_CASE("root velocity normalization has positive standard deviation") {
    // With varying clip speeds (idle=0, walk=1.4, run=5.0), the root velocity
    // should have non-trivial variance.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    const auto& norm = f.controller.getDatabase().getNormalization();
    CHECK(norm.rootVelocity.stdDev > 0.0f);
    CHECK_FALSE(std::isnan(norm.rootVelocity.mean));
    CHECK_FALSE(std::isnan(norm.rootVelocity.stdDev));
    CHECK_FALSE(std::isinf(norm.rootVelocity.stdDev));
}

TEST_CASE("bone normalization stats are valid (no NaN or Inf)") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    const auto& norm = f.controller.getDatabase().getNormalization();

    for (size_t i = 0; i < MAX_FEATURE_BONES; ++i) {
        CHECK_FALSE(std::isnan(norm.bonePosition[i].mean));
        CHECK_FALSE(std::isnan(norm.bonePosition[i].stdDev));
        CHECK_FALSE(std::isinf(norm.bonePosition[i].mean));
        CHECK_FALSE(std::isinf(norm.bonePosition[i].stdDev));

        CHECK_FALSE(std::isnan(norm.boneVelocity[i].mean));
        CHECK_FALSE(std::isnan(norm.boneVelocity[i].stdDev));
    }
}

TEST_CASE("trajectory normalization stats are valid") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    const auto& norm = f.controller.getDatabase().getNormalization();

    for (size_t i = 0; i < MAX_TRAJECTORY_SAMPLES; ++i) {
        CHECK_FALSE(std::isnan(norm.trajectoryPosition[i].mean));
        CHECK_FALSE(std::isnan(norm.trajectoryPosition[i].stdDev));
        CHECK_FALSE(std::isinf(norm.trajectoryPosition[i].mean));
        CHECK_FALSE(std::isinf(norm.trajectoryPosition[i].stdDev));

        CHECK_FALSE(std::isnan(norm.trajectoryVelocity[i].mean));
        CHECK_FALSE(std::isnan(norm.trajectoryVelocity[i].stdDev));
    }
}

TEST_CASE("normalization stdDev is never zero for active features") {
    // Zero stdDev would cause division-by-zero in normalization.
    // Active features (those with varying values) must have stdDev > 0.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    const auto& norm = f.controller.getDatabase().getNormalization();

    // Root velocity must have non-zero stdDev (we have idle + walk + run)
    CHECK(norm.rootVelocity.stdDev > 0.0f);

    // Angular velocity stdDev should be positive (we have turns)
    CHECK(norm.rootAngularVelocity.stdDev > 0.0f);
}

TEST_CASE("normalization is deterministic across rebuilds") {
    // Building the database twice with the same data should produce identical
    // normalization statistics.
    // (O3DE docs: normalization consistency across databases is critical)
    MotionMatchingFixture f1, f2;
    REQUIRE(f1.setup());
    REQUIRE(f2.setup());

    const auto& norm1 = f1.controller.getDatabase().getNormalization();
    const auto& norm2 = f2.controller.getDatabase().getNormalization();

    CHECK(norm1.rootVelocity.mean == doctest::Approx(norm2.rootVelocity.mean));
    CHECK(norm1.rootVelocity.stdDev == doctest::Approx(norm2.rootVelocity.stdDev));

    for (size_t i = 0; i < MAX_FEATURE_BONES; ++i) {
        CHECK(norm1.bonePosition[i].mean == doctest::Approx(norm2.bonePosition[i].mean));
        CHECK(norm1.bonePosition[i].stdDev == doctest::Approx(norm2.bonePosition[i].stdDev));
    }
}

} // TEST_SUITE("Feature Normalization Properties")

// ============================================================================
// 8. Locomotion Transitions
//    (Naughty Dog GDC 2021: "the system takes hundreds of animations, chops
//     them into tiny bits, and finds animations matching the current path
//     and blends them frame-by-frame")
// ============================================================================

TEST_SUITE("Locomotion Transitions") {

TEST_CASE("idle → walk transition") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    auto clips = f.simulatePhases({
        {glm::vec3(0.0f),               0.0f, 1.5f},  // Idle
        {glm::vec3(0.0f, 0.0f, 1.0f),   0.4f, 2.0f},  // Walk
    });

    REQUIRE(clips.size() == 2);
    INFO("Idle: " << clips[0] << ", Walk: " << clips[1]);

    CHECK(classifyName(clips[0]) == AnimType::Idle);
    AnimType walkType = classifyName(clips[1]);
    // At magnitude 0.4 (→ 2.4 m/s), should select walk-range locomotion
    CHECK((walkType == AnimType::Walk || walkType == AnimType::Strafe));
}

TEST_CASE("walk → run transition") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    auto clips = f.simulatePhases({
        {glm::vec3(0.0f, 0.0f, 1.0f),   0.3f, 2.0f},  // Walk
        {glm::vec3(0.0f, 0.0f, 1.0f),   1.0f, 2.0f},  // Run
    });

    REQUIRE(clips.size() == 2);
    INFO("Walk: " << clips[0] << ", Run: " << clips[1]);

    AnimType walkType = classifyName(clips[0]);
    AnimType runType = classifyName(clips[1]);
    // Walk phase (magnitude 0.3 → 1.8 m/s) should select walk-range
    CHECK((walkType == AnimType::Walk || walkType == AnimType::Strafe));
    // Run phase (magnitude 1.0 → 6.0 m/s) should select run
    CHECK(runType == AnimType::Run);
}

TEST_CASE("run → idle transition") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    auto clips = f.simulatePhases({
        {glm::vec3(0.0f, 0.0f, 1.0f),   1.0f, 2.0f},  // Run
        {glm::vec3(0.0f),                0.0f, 3.0f},  // Stop
    });

    REQUIRE(clips.size() == 2);
    INFO("Run: " << clips[0] << ", Stopped: " << clips[1]);

    CHECK(classifyName(clips[0]) != AnimType::Idle);
    CHECK(classifyName(clips[1]) == AnimType::Idle);
}

TEST_CASE("full cycle: idle → walk → run → walk → idle") {
    // Tests the complete locomotion cycle that a player character typically goes through.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    auto clips = f.simulatePhases({
        {glm::vec3(0.0f),                0.0f, 1.5f},  // Idle
        {glm::vec3(0.0f, 0.0f, 1.0f),    0.3f, 1.5f},  // Walk
        {glm::vec3(0.0f, 0.0f, 1.0f),    1.0f, 1.5f},  // Run
        {glm::vec3(0.0f, 0.0f, 1.0f),    0.3f, 1.5f},  // Walk again
        {glm::vec3(0.0f),                0.0f, 2.0f},  // Idle again
    });

    REQUIRE(clips.size() == 5);
    INFO("Cycle: " << clips[0] << " → " << clips[1] << " → " << clips[2]
         << " → " << clips[3] << " → " << clips[4]);

    AnimType t0 = classifyName(clips[0]);
    AnimType t1 = classifyName(clips[1]);
    AnimType t2 = classifyName(clips[2]);
    AnimType t3 = classifyName(clips[3]);
    AnimType t4 = classifyName(clips[4]);

    // First and last should be idle
    CHECK(t0 == AnimType::Idle);
    CHECK(t4 == AnimType::Idle);

    // Walk phase (0.3 → 1.8 m/s) should select walk-range locomotion
    CHECK((t1 == AnimType::Walk || t1 == AnimType::Strafe));
    // Run phase (1.0 → 6.0 m/s) should select run
    CHECK(t2 == AnimType::Run);
    // Return to walk (0.3 → 1.8 m/s) should select walk-range again
    CHECK((t3 == AnimType::Walk || t3 == AnimType::Strafe));
}

TEST_CASE("transition does not produce NaN during any phase") {
    // (Naughty Dog: "initial joy, later frustration" – NaN bugs are common
    //  during transitions between very different animation types)
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    float dt = 1.0f / 30.0f;
    glm::vec3 position(0.0f);
    glm::vec3 facing(0.0f, 0.0f, 1.0f);

    struct Phase {
        glm::vec3 dir;
        float mag;
        int frames;
    };

    std::vector<Phase> phases = {
        {{0.0f, 0.0f, 0.0f}, 0.0f, 30},   // Idle
        {{0.0f, 0.0f, 1.0f}, 0.3f, 30},   // Walk
        {{0.0f, 0.0f, 1.0f}, 1.0f, 30},   // Run
        {{1.0f, 0.0f, 0.0f}, 0.5f, 30},   // Strafe
        {{0.0f, 0.0f, -1.0f}, 0.7f, 30},  // Backward
        {{0.0f, 0.0f, 0.0f}, 0.0f, 30},   // Idle
    };

    int nanCount = 0;
    for (const auto& phase : phases) {
        for (int i = 0; i < phase.frames; ++i) {
            f.controller.update(position, facing, phase.dir, phase.mag, dt);
            if (std::isnan(f.controller.getStats().lastMatchCost)) nanCount++;
        }
    }

    CHECK(nanCount == 0);

    // Verify final pose is valid
    SkeletonPose pose;
    f.controller.getCurrentPose(pose);
    for (size_t i = 0; i < pose.size(); ++i) {
        CHECK_FALSE(std::isnan(pose[i].translation.x));
        CHECK_FALSE(std::isnan(pose[i].rotation.w));
    }
}

} // TEST_SUITE("Locomotion Transitions")

// ============================================================================
// 9. Regression Tests
//    (Production best practice: golden-value regression tests catch regressions
//     in search algorithm, normalization, or feature extraction)
// ============================================================================

TEST_SUITE("Regression Tests") {

TEST_CASE("database pose count is stable") {
    // The pose count should be deterministic for the same input data and sample rate.
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    const auto& db = f.controller.getDatabase();
    size_t poseCount = db.getPoseCount();

    INFO("Total poses: " << poseCount);

    // With ~14 animation files at 30fps, each ~1-3 seconds, we expect >100 poses
    CHECK(poseCount > 100);
    CHECK(poseCount < 100000);

    // Build a second time and check it's the same
    MotionMatchingFixture f2;
    REQUIRE(f2.setup());
    CHECK(f2.controller.getDatabase().getPoseCount() == poseCount);
}

TEST_CASE("database clip count is stable") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    const auto& db = f.controller.getDatabase();
    size_t clipCount = db.getClipCount();

    INFO("Total clips: " << clipCount);
    CHECK(clipCount > 5);  // We load many animation files

    MotionMatchingFixture f2;
    REQUIRE(f2.setup());
    CHECK(f2.controller.getDatabase().getClipCount() == clipCount);
}

TEST_CASE("idle query cost is within expected range") {
    // For an idle query, the best match cost should be relatively low (good match).
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    MotionMatcher matcher;
    matcher.setDatabase(&f.controller.getDatabase());

    Trajectory traj = f.buildTrajectory(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    PoseFeatures queryPose;

    SearchOptions opts;
    opts.useKDTree = false;
    opts.excludedTags = {"jump"};

    auto result = matcher.findBestMatch(traj, queryPose, opts);
    REQUIRE(result.isValid());

    INFO("Idle best match cost: " << result.cost);
    // Cost should be reasonable (not huge), indicating a good match exists
    CHECK(result.cost < 100.0f);
}

TEST_CASE("walk query consistently selects walk animation without tag constraint") {
    // Run the same walk query multiple times (different fixture instances)
    // to verify deterministic selection. Crucially, NO requiredTags filter is used —
    // the system must naturally prefer walk clips based on feature matching alone.
    std::string firstSelected;

    for (int trial = 0; trial < 3; ++trial) {
        MotionMatchingFixture f;
        REQUIRE(f.setup());

        MotionMatcher matcher;
        matcher.setDatabase(&f.controller.getDatabase());

        glm::vec3 walkVel(0.0f, 0.0f, WALK_SPEED);
        Trajectory traj = f.buildTrajectory(walkVel, glm::vec3(0.0f, 0.0f, 1.0f));

        PoseFeatures queryPose;
        queryPose.rootVelocity = walkVel;

        SearchOptions opts;
        opts.useKDTree = false;
        opts.excludedTags = {"jump"};
        // No requiredTags — unconstrained search must naturally select walk

        auto result = matcher.findBestMatch(traj, queryPose, opts);
        REQUIRE(result.isValid());

        AnimType type = classifyName(result.clip->name);
        INFO("Trial " << trial << ": " << result.clip->name << " cost=" << result.cost);
        // Walk-speed query should select walk (or strafe, which has similar speed 1.8 m/s)
        CHECK((type == AnimType::Walk || type == AnimType::Strafe));

        // Verify determinism: same result every time
        if (trial == 0) {
            firstSelected = result.clip->name;
        } else {
            CHECK(result.clip->name == firstSelected);
        }
    }
}

TEST_CASE("all database poses have valid (non-NaN) features") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    const auto& db = f.controller.getDatabase();

    for (size_t i = 0; i < db.getPoseCount(); ++i) {
        const auto& pose = db.getPose(i);

        // Validate pose features
        CHECK_FALSE(std::isnan(pose.poseFeatures.rootVelocity.x));
        CHECK_FALSE(std::isnan(pose.poseFeatures.rootVelocity.y));
        CHECK_FALSE(std::isnan(pose.poseFeatures.rootVelocity.z));
        CHECK_FALSE(std::isnan(pose.poseFeatures.rootAngularVelocity));

        for (size_t j = 0; j < pose.poseFeatures.boneCount; ++j) {
            CHECK_FALSE(std::isnan(pose.poseFeatures.boneFeatures[j].position.x));
            CHECK_FALSE(std::isnan(pose.poseFeatures.boneFeatures[j].position.y));
            CHECK_FALSE(std::isnan(pose.poseFeatures.boneFeatures[j].position.z));
        }

        // Validate trajectory samples
        for (size_t j = 0; j < pose.trajectory.sampleCount; ++j) {
            const auto& s = pose.trajectory.samples[j];
            CHECK_FALSE(std::isnan(s.position.x));
            CHECK_FALSE(std::isnan(s.position.y));
            CHECK_FALSE(std::isnan(s.position.z));
            CHECK_FALSE(std::isnan(s.velocity.x));
            CHECK_FALSE(std::isnan(s.velocity.y));
            CHECK_FALSE(std::isnan(s.velocity.z));
        }

        // Valid clip reference
        CHECK(pose.clipIndex < db.getClipCount());
        CHECK(pose.time >= 0.0f);
    }
}

TEST_CASE("database total duration is consistent") {
    MotionMatchingFixture f;
    REQUIRE(f.setup());

    auto stats = f.controller.getDatabase().getStats();

    INFO("Total duration: " << stats.totalDuration << "s");
    CHECK(stats.totalDuration > 5.0f);   // At least 5 seconds total
    CHECK(stats.totalDuration < 300.0f); // Less than 5 minutes total

    // Second build should match
    MotionMatchingFixture f2;
    REQUIRE(f2.setup());
    auto stats2 = f2.controller.getDatabase().getStats();
    CHECK(stats.totalDuration == doctest::Approx(stats2.totalDuration));
}

} // TEST_SUITE("Regression Tests")
