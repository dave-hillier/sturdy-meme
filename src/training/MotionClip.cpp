#include "MotionClip.h"

#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ─── MotionClip ─────────────────────────────────────────────────────────────────

MotionFrame MotionClip::sampleAt(float time) const {
    if (frames.empty()) return {};
    if (frames.size() == 1) return frames[0];

    float frameF = time * fps;
    frameF = std::clamp(frameF, 0.0f, static_cast<float>(frames.size() - 1));

    size_t f0 = static_cast<size_t>(std::floor(frameF));
    size_t f1 = std::min(f0 + 1, frames.size() - 1);
    float alpha = frameF - static_cast<float>(f0);

    const auto& a = frames[f0];
    const auto& b = frames[f1];
    size_t numJoints = a.jointPositions.size();

    MotionFrame result;
    result.rootPos = glm::mix(a.rootPos, b.rootPos, alpha);
    result.rootRot = glm::slerp(a.rootRot, b.rootRot, alpha);

    result.jointPositions.resize(numJoints);
    result.jointRotations.resize(numJoints);
    for (size_t i = 0; i < numJoints; ++i) {
        result.jointPositions[i] = glm::mix(a.jointPositions[i], b.jointPositions[i], alpha);
        result.jointRotations[i] = glm::slerp(a.jointRotations[i], b.jointRotations[i], alpha);
    }

    return result;
}

// ─── MotionLibrary ──────────────────────────────────────────────────────────────

bool MotionLibrary::loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MotionLibrary: cannot open '%s'", path.c_str());
        return false;
    }

    nlohmann::json data;
    try {
        file >> data;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MotionLibrary: JSON parse error in '%s': %s",
                     path.c_str(), e.what());
        return false;
    }

    MotionClip clip;
    clip.fps = data.value("fps", 30.0f);
    clip.name = fs::path(path).stem().string();

    const auto& framesJson = data["frames"];
    clip.frames.reserve(framesJson.size());

    for (const auto& frameJson : framesJson) {
        MotionFrame frame;

        // Root position
        auto rp = frameJson["root_pos"];
        frame.rootPos = glm::vec3(rp[0].get<float>(), rp[1].get<float>(), rp[2].get<float>());

        // Root rotation (w,x,y,z)
        auto rr = frameJson["root_rot"];
        frame.rootRot = glm::quat(rr[0].get<float>(), rr[1].get<float>(),
                                   rr[2].get<float>(), rr[3].get<float>());

        // Joint positions
        const auto& jpJson = frameJson["joint_positions"];
        size_t nj = jpJson.size();
        frame.jointPositions.resize(nj);
        for (size_t i = 0; i < nj; ++i) {
            frame.jointPositions[i] = glm::vec3(
                jpJson[i][0].get<float>(), jpJson[i][1].get<float>(), jpJson[i][2].get<float>());
        }

        // Joint rotations (w,x,y,z)
        const auto& jrJson = frameJson["joint_rotations"];
        frame.jointRotations.resize(nj);
        for (size_t i = 0; i < nj; ++i) {
            frame.jointRotations[i] = glm::quat(
                jrJson[i][0].get<float>(), jrJson[i][1].get<float>(),
                jrJson[i][2].get<float>(), jrJson[i][3].get<float>());
        }

        clip.frames.push_back(std::move(frame));
    }

    if (!clip.frames.empty()) {
        numJoints = clip.frames[0].jointPositions.size();
    }

    SDL_Log("MotionLibrary: loaded '%s' (%zu frames, %.1f fps, %.2fs)",
            clip.name.c_str(), clip.frames.size(), clip.fps, clip.duration());

    clips.push_back(std::move(clip));
    return true;
}

bool MotionLibrary::loadDirectory(const std::string& dir) {
    if (!fs::exists(dir)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "MotionLibrary: directory '%s' not found", dir.c_str());
        return false;
    }

    size_t loaded = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            if (loadFile(entry.path().string())) {
                ++loaded;
            }
        }
    }

    SDL_Log("MotionLibrary: loaded %zu clips from '%s'", loaded, dir.c_str());
    return loaded > 0;
}

void MotionLibrary::addStandingClip(float durationSec, float fps) {
    MotionClip clip;
    clip.fps = fps;
    clip.name = "standing";

    size_t numFrames = static_cast<size_t>(durationSec * fps);
    clip.frames.resize(numFrames);

    for (auto& frame : clip.frames) {
        frame.rootPos = glm::vec3(0.0f, 1.0f, 0.0f);
        frame.rootRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        frame.jointPositions.assign(numJoints, glm::vec3(0.0f));
        frame.jointRotations.assign(numJoints, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }

    clips.push_back(std::move(clip));
}

size_t MotionLibrary::totalFrames() const {
    size_t total = 0;
    for (const auto& clip : clips) {
        total += clip.frames.size();
    }
    return total;
}
