#pragma once

#include "Tensor.h"
#include "MLPNetwork.h"
#include <vector>
#include <string>
#include <random>
#include <cstdint>

namespace ml {

// Manages the CALM latent space: a 64D unit hypersphere encoding character behaviors.
//
// Three capabilities:
//   1. Latent library — pre-encoded behaviors loaded from disk, tagged by name
//   2. Encoder network — encodes stacked AMP observations into a latent vector
//   3. Interpolation — smooth blending between latents on the unit hypersphere
class CALMLatentSpace {
public:
    static constexpr int DEFAULT_LATENT_DIM = 64;

    // A single pre-encoded behavior in the library
    struct EncodedBehavior {
        std::string clipName;
        std::vector<std::string> tags;   // Semantic tags: "walk", "run", "crouch", etc.
        Tensor latent;                   // 64D, L2-normalized
    };

    CALMLatentSpace() = default;
    explicit CALMLatentSpace(int latentDim);

    // --- Latent Library ---

    // Add a pre-encoded behavior to the library
    void addBehavior(const std::string& clipName,
                     const std::vector<std::string>& tags,
                     Tensor latent);

    // Sample a random behavior from the library
    const Tensor& sampleRandom(std::mt19937& rng) const;

    // Sample a random behavior matching a tag
    const Tensor& sampleByTag(const std::string& tag, std::mt19937& rng) const;

    // Get all behaviors matching a tag
    std::vector<const EncodedBehavior*> getBehaviorsByTag(const std::string& tag) const;

    // Get library size
    size_t librarySize() const { return library_.size(); }

    // Get a specific behavior
    const EncodedBehavior& getBehavior(size_t index) const { return library_[index]; }

    // --- Encoder ---

    // Set the encoder network (loaded via ModelLoader)
    void setEncoder(MLPNetwork encoder);

    // Encode stacked AMP observations into a latent vector.
    // Input: flattened temporal observation window
    // Output: L2-normalized 64D latent
    Tensor encode(const Tensor& stackedObs) const;

    // Check if encoder is available
    bool hasEncoder() const { return encoder_.numLayers() > 0; }

    // --- Interpolation ---

    // Linearly interpolate between two latents on the unit hypersphere.
    // Result is L2-normalized after interpolation.
    //   alpha=0 → z0,  alpha=1 → z1
    static Tensor interpolate(const Tensor& z0, const Tensor& z1, float alpha);

    // Get a zero latent (for initialization)
    Tensor zeroLatent() const;

    // Get latent dimension
    int latentDim() const { return latentDim_; }

private:
    int latentDim_ = DEFAULT_LATENT_DIM;
    std::vector<EncodedBehavior> library_;
    MLPNetwork encoder_;

    // Fallback latent returned when library is empty
    mutable Tensor fallbackLatent_;
};

} // namespace ml
