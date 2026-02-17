#include "CALMLatentSpace.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cassert>

namespace ml {

CALMLatentSpace::CALMLatentSpace(int latentDim)
    : latentDim_(latentDim) {
}

// --- Latent Library ---

void CALMLatentSpace::addBehavior(const std::string& clipName,
                                   const std::vector<std::string>& tags,
                                   Tensor latent) {
    assert(static_cast<int>(latent.size()) == latentDim_);
    Tensor::l2Normalize(latent);
    library_.push_back({clipName, tags, std::move(latent)});
}

const Tensor& CALMLatentSpace::sampleRandom(std::mt19937& rng) const {
    if (library_.empty()) {
        if (fallbackLatent_.empty()) {
            fallbackLatent_ = zeroLatent();
        }
        return fallbackLatent_;
    }
    std::uniform_int_distribution<size_t> dist(0, library_.size() - 1);
    return library_[dist(rng)].latent;
}

const Tensor& CALMLatentSpace::sampleByTag(const std::string& tag,
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
                    "CALMLatentSpace: no behaviors with tag '%s', falling back to random",
                    tag.c_str());
        return sampleRandom(rng);
    }

    std::uniform_int_distribution<size_t> dist(0, matching.size() - 1);
    return library_[matching[dist(rng)]].latent;
}

std::vector<const CALMLatentSpace::EncodedBehavior*>
CALMLatentSpace::getBehaviorsByTag(const std::string& tag) const {
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

// --- Encoder ---

void CALMLatentSpace::setEncoder(MLPNetwork encoder) {
    encoder_ = std::move(encoder);
}

Tensor CALMLatentSpace::encode(const Tensor& stackedObs) const {
    assert(hasEncoder());
    Tensor latent;
    encoder_.forward(stackedObs, latent);

    // Resize to latentDim if encoder output differs
    if (static_cast<int>(latent.size()) != latentDim_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "CALMLatentSpace: encoder output size %zu != latentDim %d",
                    latent.size(), latentDim_);
    }

    Tensor::l2Normalize(latent);
    return latent;
}

// --- Interpolation ---

Tensor CALMLatentSpace::interpolate(const Tensor& z0, const Tensor& z1, float alpha) {
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

Tensor CALMLatentSpace::zeroLatent() const {
    // Return a unit vector along first dimension
    std::vector<float> data(latentDim_, 0.0f);
    if (latentDim_ > 0) {
        data[0] = 1.0f;
    }
    return Tensor(1, latentDim_, std::move(data));
}

} // namespace ml
