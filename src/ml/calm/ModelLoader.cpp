#include "ModelLoader.h"
#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace ml::calm {

static std::string joinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    if (dir.back() == '/' || dir.back() == '\\') return dir + file;
    return dir + "/" + file;
}

bool ModelLoader::loadLLC(const std::string& modelDir, LowLevelController& llc) {
    std::string stylePath = joinPath(modelDir, "llc_style.bin");
    std::string mainPath = joinPath(modelDir, "llc_main.bin");
    std::string muHeadPath = joinPath(modelDir, "llc_mu_head.bin");

    StyleConditionedNetwork network;
    if (!ml::ModelLoader::loadStyleConditioned(stylePath, mainPath, network)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "calm::ModelLoader: failed to load LLC style/main from %s", modelDir.c_str());
        return false;
    }

    MLPNetwork muHead;
    if (!ml::ModelLoader::loadMLP(muHeadPath, muHead)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "calm::ModelLoader: failed to load mu head from %s", muHeadPath.c_str());
        return false;
    }

    llc.setNetwork(std::move(network));
    llc.setMuHead(std::move(muHead));

    SDL_Log("calm::ModelLoader: loaded LLC from %s", modelDir.c_str());
    return true;
}

bool ModelLoader::loadEncoder(const std::string& modelDir, LatentSpace& latentSpace) {
    std::string encoderPath = joinPath(modelDir, "encoder.bin");

    if (!std::filesystem::exists(encoderPath)) {
        SDL_Log("calm::ModelLoader: no encoder.bin found (optional)");
        return false;
    }

    MLPNetwork encoder;
    if (!ml::ModelLoader::loadMLP(encoderPath, encoder)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "calm::ModelLoader: failed to load encoder from %s", encoderPath.c_str());
        return false;
    }

    latentSpace.setEncoder(std::move(encoder));
    SDL_Log("calm::ModelLoader: loaded encoder from %s", encoderPath.c_str());
    return true;
}

bool ModelLoader::loadLatentLibrary(const std::string& modelDir, LatentSpace& latentSpace) {
    std::string libraryPath = joinPath(modelDir, "latent_library.json");

    if (!std::filesystem::exists(libraryPath)) {
        SDL_Log("calm::ModelLoader: no latent_library.json found (optional)");
        return false;
    }

    return latentSpace.loadLibraryFromJSON(libraryPath);
}

bool ModelLoader::loadHLC(const std::string& modelDir, const std::string& taskName,
                           TaskController& hlc) {
    std::string hlcPath = joinPath(modelDir, "hlc_" + taskName + ".bin");

    if (!std::filesystem::exists(hlcPath)) {
        SDL_Log("calm::ModelLoader: no hlc_%s.bin found (optional)", taskName.c_str());
        return false;
    }

    MLPNetwork network;
    if (!ml::ModelLoader::loadMLP(hlcPath, network)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "calm::ModelLoader: failed to load HLC from %s", hlcPath.c_str());
        return false;
    }

    hlc.setNetwork(std::move(network));
    SDL_Log("calm::ModelLoader: loaded HLC '%s' from %s", taskName.c_str(), hlcPath.c_str());
    return true;
}

bool ModelLoader::loadRetargetMap(const std::string& path, RetargetMap& map) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "calm::ModelLoader: failed to open retarget map %s", path.c_str());
        return false;
    }

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "calm::ModelLoader: JSON parse error in %s: %s", path.c_str(), e.what());
        return false;
    }

    map.scaleFactor = doc.value("scale_factor", 1.0f);

    if (doc.contains("training_to_engine_joint_map") &&
        doc["training_to_engine_joint_map"].is_object()) {
        for (auto& [key, val] : doc["training_to_engine_joint_map"].items()) {
            map.jointMap[key] = val.get<std::string>();
        }
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "calm::ModelLoader: missing 'training_to_engine_joint_map' in %s",
                     path.c_str());
        return false;
    }

    SDL_Log("calm::ModelLoader: loaded retarget map from %s (%zu joints, scale=%.2f)",
            path.c_str(), map.jointMap.size(), map.scaleFactor);
    return true;
}

bool ModelLoader::loadAll(const std::string& modelDir, ModelSet& models, int latentDim) {
    models.latentSpace = LatentSpace(latentDim);

    if (!loadLLC(modelDir, models.llc)) {
        return false;
    }

    models.hasEncoder = loadEncoder(modelDir, models.latentSpace);
    models.hasLibrary = loadLatentLibrary(modelDir, models.latentSpace);
    models.hasHeadingHLC = loadHLC(modelDir, "heading", models.headingHLC);
    models.hasLocationHLC = loadHLC(modelDir, "location", models.locationHLC);
    models.hasStrikeHLC = loadHLC(modelDir, "strike", models.strikeHLC);

    SDL_Log("calm::ModelLoader: loaded model set from %s (encoder=%s, library=%s, "
            "heading=%s, location=%s, strike=%s)",
            modelDir.c_str(),
            models.hasEncoder ? "yes" : "no",
            models.hasLibrary ? "yes" : "no",
            models.hasHeadingHLC ? "yes" : "no",
            models.hasLocationHLC ? "yes" : "no",
            models.hasStrikeHLC ? "yes" : "no");
    return true;
}

} // namespace ml::calm
