#include "LatentSpace.h"
#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cassert>
#include <fstream>

namespace ml {

LatentSpace::LatentSpace(int latentDim)
    : latentDim_(latentDim) {
}

// --- Latent Library ---

void LatentSpace::addBehavior(const std::string& clipName,
                               const std::vector<std::string>& tags,
                               Tensor latent) {
    assert(static_cast<int>(latent.size()) == latentDim_);
    Tensor::l2Normalize(latent);
    library_.push_back({clipName, tags, std::move(latent)});
}

const Tensor& LatentSpace::sampleRandom(std::mt19937& rng) const {
    if (library_.empty()) {
        if (fallbackLatent_.empty()) {
            fallbackLatent_ = zeroLatent();
        }
        return fallbackLatent_;
    }
    std::uniform_int_distribution<size_t> dist(0, library_.size() - 1);
    return library_[dist(rng)].latent;
}

const Tensor& LatentSpace::sampleByTag(const std::string& tag,
                                        std::mt19937& rng) const {
    // Collect matching indices
    std::vector<size_t> matching;
    for (size_t i = 0; i < library_.size(); ++i) {
        for (const auto& t : library_[i].tags) {
            if (t == tag) {
                matching.push_back(i);
                break;
            }
        }
    }

    if (matching.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "LatentSpace: no behaviors with tag '%s', falling back to random",
                    tag.c_str());
        return sampleRandom(rng);
    }

    std::uniform_int_distribution<size_t> dist(0, matching.size() - 1);
    return library_[matching[dist(rng)]].latent;
}

std::vector<const LatentSpace::EncodedBehavior*>
LatentSpace::getBehaviorsByTag(const std::string& tag) const {
    std::vector<const EncodedBehavior*> result;
    for (const auto& behavior : library_) {
        for (const auto& t : behavior.tags) {
            if (t == tag) {
                result.push_back(&behavior);
                break;
            }
        }
    }
    return result;
}

// --- File I/O ---

bool LatentSpace::loadLibraryFromJSON(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "LatentSpace: failed to open %s", path.c_str());
        return false;
    }

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "LatentSpace: JSON parse error in %s: %s",
                     path.c_str(), e.what());
        return false;
    }

    // Read latent dimension (optional, defaults to current)
    if (doc.contains("latent_dim")) {
        int fileDim = doc["latent_dim"].get<int>();
        if (latentDim_ != 0 && fileDim != latentDim_) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "LatentSpace: file latent_dim=%d differs from current=%d, using file value",
                        fileDim, latentDim_);
        }
        latentDim_ = fileDim;
    }

    if (!doc.contains("behaviors") || !doc["behaviors"].is_array()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "LatentSpace: missing 'behaviors' array in %s", path.c_str());
        return false;
    }

    size_t loaded = 0;
    for (const auto& entry : doc["behaviors"]) {
        std::string clipName = entry.value("clip", "");
        if (clipName.empty()) continue;

        std::vector<std::string> tags;
        if (entry.contains("tags") && entry["tags"].is_array()) {
            for (const auto& t : entry["tags"]) {
                tags.push_back(t.get<std::string>());
            }
        }

        if (!entry.contains("latent") || !entry["latent"].is_array()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "LatentSpace: skipping '%s' — no latent array", clipName.c_str());
            continue;
        }

        const auto& latentArr = entry["latent"];
        std::vector<float> data;
        data.reserve(latentArr.size());
        for (const auto& v : latentArr) {
            data.push_back(v.get<float>());
        }

        if (static_cast<int>(data.size()) != latentDim_) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "LatentSpace: '%s' has %zu dims (expected %d), skipping",
                        clipName.c_str(), data.size(), latentDim_);
            continue;
        }

        size_t dim = data.size();
        Tensor latent(1, dim, std::move(data));
        addBehavior(clipName, tags, std::move(latent));
        ++loaded;
    }

    SDL_Log("LatentSpace: loaded %zu behaviors from %s", loaded, path.c_str());
    return loaded > 0;
}

// --- Encoder ---

void LatentSpace::setEncoder(MLPNetwork encoder) {
    encoder_ = std::move(encoder);
}

Tensor LatentSpace::encode(const Tensor& stackedObs) const {
    assert(hasEncoder());
    Tensor latent;
    encoder_.forward(stackedObs, latent);

    // Resize to latentDim if encoder output differs
    if (static_cast<int>(latent.size()) != latentDim_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "LatentSpace: encoder output size %zu != latentDim %d",
                    latent.size(), latentDim_);
    }

    Tensor::l2Normalize(latent);
    return latent;
}

// --- Interpolation ---

Tensor LatentSpace::interpolate(const Tensor& z0, const Tensor& z1, float alpha) {
    assert(z0.size() == z1.size());
    size_t dim = z0.size();

    std::vector<float> data(dim);
    float oneMinusAlpha = 1.0f - alpha;
    for (size_t i = 0; i < dim; ++i) {
        data[i] = z0[i] * oneMinusAlpha + z1[i] * alpha;
    }

    Tensor result(1, dim, std::move(data));
    Tensor::l2Normalize(result);
    return result;
}

Tensor LatentSpace::zeroLatent() const {
    // Return a unit vector along first dimension
    std::vector<float> data(latentDim_, 0.0f);
    if (latentDim_ > 0) {
        data[0] = 1.0f;
    }
    return Tensor(1, latentDim_, std::move(data));
}

} // namespace ml
