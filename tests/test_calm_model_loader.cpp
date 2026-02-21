#include <doctest/doctest.h>
#include "ml/ModelLoader.h"
#include "ml/calm/ModelLoader.h"
#include "ml/LatentSpace.h"
#include "ml/calm/LowLevelController.h"
#include "ml/TaskController.h"
#include "ml/Tensor.h"
#include <fstream>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace {

// Create a temporary directory for test files
struct TempDir {
    std::string path;
    TempDir() {
        path = std::filesystem::temp_directory_path().string() + "/calm_test_" +
               std::to_string(reinterpret_cast<uintptr_t>(this));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::filesystem::remove_all(path);
    }
};

// Write a simple MLP .bin file for testing
void writeDummyMLP(const std::string& filepath,
                   const std::vector<std::tuple<int, int, uint32_t>>& layerSpecs) {
    std::ofstream f(filepath, std::ios::binary);
    uint32_t magic = 0x4D4C5031;
    uint32_t version = 1;
    uint32_t numLayers = static_cast<uint32_t>(layerSpecs.size());
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&version), 4);
    f.write(reinterpret_cast<const char*>(&numLayers), 4);

    for (const auto& [inF, outF, act] : layerSpecs) {
        uint32_t in = static_cast<uint32_t>(inF);
        uint32_t out = static_cast<uint32_t>(outF);
        uint32_t activation = act;
        f.write(reinterpret_cast<const char*>(&in), 4);
        f.write(reinterpret_cast<const char*>(&out), 4);
        f.write(reinterpret_cast<const char*>(&activation), 4);

        // Write small random weights
        for (int i = 0; i < outF * inF; ++i) {
            float w = 0.01f * static_cast<float>((i * 7 + 3) % 13 - 6);
            f.write(reinterpret_cast<const char*>(&w), 4);
        }
        // Write zeros for bias
        for (int i = 0; i < outF; ++i) {
            float b = 0.0f;
            f.write(reinterpret_cast<const char*>(&b), 4);
        }
    }
}

} // anonymous namespace

TEST_SUITE("LatentSpace_JSON") {

TEST_CASE("loadLibraryFromJSON loads valid library") {
    TempDir tmp;
    std::string jsonPath = tmp.path + "/latent_library.json";

    nlohmann::json doc;
    doc["latent_dim"] = 4;
    doc["behaviors"] = nlohmann::json::array();

    // Add a walk behavior
    nlohmann::json walk;
    walk["clip"] = "walk.npy";
    walk["tags"] = {"walk", "locomotion"};
    walk["latent"] = {0.5f, 0.5f, 0.5f, 0.5f};
    doc["behaviors"].push_back(walk);

    // Add a run behavior
    nlohmann::json run;
    run["clip"] = "run.npy";
    run["tags"] = {"run"};
    run["latent"] = {-0.5f, 0.5f, -0.5f, 0.5f};
    doc["behaviors"].push_back(run);

    std::ofstream f(jsonPath);
    f << doc.dump(2);
    f.close();

    ml::LatentSpace space(4);
    REQUIRE(space.loadLibraryFromJSON(jsonPath));
    CHECK(space.librarySize() == 2);

    // Check behaviors are loaded with correct tags
    auto walkBehaviors = space.getBehaviorsByTag("walk");
    CHECK(walkBehaviors.size() == 1);
    CHECK(walkBehaviors[0]->clipName == "walk.npy");
    CHECK(walkBehaviors[0]->tags.size() == 2);

    auto runBehaviors = space.getBehaviorsByTag("run");
    CHECK(runBehaviors.size() == 1);

    // Verify latents are L2-normalized
    float norm = walkBehaviors[0]->latent.l2Norm();
    CHECK(norm == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("loadLibraryFromJSON rejects missing file") {
    ml::LatentSpace space(64);
    CHECK_FALSE(space.loadLibraryFromJSON("/nonexistent/path.json"));
}

TEST_CASE("loadLibraryFromJSON skips mismatched dimensions") {
    TempDir tmp;
    std::string jsonPath = tmp.path + "/lib.json";

    nlohmann::json doc;
    doc["latent_dim"] = 4;
    doc["behaviors"] = nlohmann::json::array();

    // Wrong dimension (3 instead of 4)
    nlohmann::json wrong;
    wrong["clip"] = "wrong.npy";
    wrong["tags"] = {"walk"};
    wrong["latent"] = {0.1f, 0.2f, 0.3f};
    doc["behaviors"].push_back(wrong);

    // Correct dimension
    nlohmann::json ok;
    ok["clip"] = "ok.npy";
    ok["tags"] = {"idle"};
    ok["latent"] = {0.5f, 0.5f, 0.5f, 0.5f};
    doc["behaviors"].push_back(ok);

    std::ofstream f(jsonPath);
    f << doc.dump(2);
    f.close();

    ml::LatentSpace space(4);
    REQUIRE(space.loadLibraryFromJSON(jsonPath));
    CHECK(space.librarySize() == 1);  // Only the correctly-dimensioned one
}

} // TEST_SUITE

TEST_SUITE("calm::ModelLoader") {

TEST_CASE("loadLLC loads three .bin files") {
    TempDir tmp;

    // Style MLP: 64 -> 256(tanh) -> 128(tanh)
    writeDummyMLP(tmp.path + "/llc_style.bin", {
        {64, 256, 2},  // tanh
        {256, 128, 2}, // tanh
    });
    // Main MLP: (128+50) -> 256(relu) -> 128(relu)
    writeDummyMLP(tmp.path + "/llc_main.bin", {
        {178, 256, 1}, // relu
        {256, 128, 1}, // relu
    });
    // Mu head: 128 -> 20(none)
    writeDummyMLP(tmp.path + "/llc_mu_head.bin", {
        {128, 20, 0},  // none
    });

    ml::calm::LowLevelController llc;
    REQUIRE(ml::calm::ModelLoader::loadLLC(tmp.path, llc));
    CHECK(llc.isLoaded());
}

TEST_CASE("loadLLC fails with missing files") {
    TempDir tmp;
    ml::calm::LowLevelController llc;
    CHECK_FALSE(ml::calm::ModelLoader::loadLLC(tmp.path, llc));
}

TEST_CASE("loadHLC loads optional task network") {
    TempDir tmp;

    // HLC heading: 3 -> 64(relu) -> 32(none)
    writeDummyMLP(tmp.path + "/hlc_heading.bin", {
        {3, 64, 1},
        {64, 32, 0},
    });

    ml::TaskController hlc;
    REQUIRE(ml::calm::ModelLoader::loadHLC(tmp.path, "heading", hlc));
    CHECK(hlc.isLoaded());
}

TEST_CASE("loadHLC returns false for missing task") {
    TempDir tmp;
    ml::TaskController hlc;
    CHECK_FALSE(ml::calm::ModelLoader::loadHLC(tmp.path, "nonexistent", hlc));
}

TEST_CASE("loadRetargetMap loads valid JSON") {
    TempDir tmp;
    std::string jsonPath = tmp.path + "/retarget_map.json";

    nlohmann::json doc;
    doc["training_to_engine_joint_map"] = {
        {"pelvis", "Hips"},
        {"left_thigh", "LeftUpLeg"},
        {"right_thigh", "RightUpLeg"},
    };
    doc["scale_factor"] = 1.5f;

    std::ofstream f(jsonPath);
    f << doc.dump(2);
    f.close();

    ml::calm::ModelLoader::RetargetMap map;
    REQUIRE(ml::calm::ModelLoader::loadRetargetMap(jsonPath, map));
    CHECK(map.jointMap.size() == 3);
    CHECK(map.jointMap["pelvis"] == "Hips");
    CHECK(map.scaleFactor == doctest::Approx(1.5f));
}

TEST_CASE("loadAll loads LLC and optional components") {
    TempDir tmp;

    // Create minimal LLC files
    writeDummyMLP(tmp.path + "/llc_style.bin", {
        {8, 16, 2},
        {16, 8, 2},
    });
    writeDummyMLP(tmp.path + "/llc_main.bin", {
        {18, 32, 1},
        {32, 16, 1},
    });
    writeDummyMLP(tmp.path + "/llc_mu_head.bin", {
        {16, 4, 0},
    });

    // Create a latent library
    nlohmann::json lib;
    lib["latent_dim"] = 8;
    lib["behaviors"] = nlohmann::json::array();
    nlohmann::json b;
    b["clip"] = "walk.npy";
    b["tags"] = {"walk"};
    b["latent"] = {0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f};
    lib["behaviors"].push_back(b);
    std::ofstream lf(tmp.path + "/latent_library.json");
    lf << lib.dump(2);
    lf.close();

    ml::calm::ModelLoader::ModelSet models;
    REQUIRE(ml::calm::ModelLoader::loadAll(tmp.path, models, 8));
    CHECK(models.llc.isLoaded());
    CHECK(models.hasLibrary);
    CHECK(models.latentSpace.librarySize() == 1);
    CHECK_FALSE(models.hasEncoder);
    CHECK_FALSE(models.hasHeadingHLC);
}

} // TEST_SUITE
