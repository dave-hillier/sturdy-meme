#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

// MLP with forward pass, backward pass, and Adam optimizer.
// Used for both policy and value networks during training.
// Weight format is compatible with MLPPolicy for inference.
class TrainingMLP {
public:
    struct Config {
        size_t inputDim = 429;
        size_t outputDim = 60;
        size_t hiddenDim = 1024;
        size_t hiddenLayers = 3;
    };

    explicit TrainingMLP(const Config& config);

    // Forward pass — stores activations for backward pass.
    // Returns output vector.
    const std::vector<float>& forward(const float* input);

    // Backward pass — computes gradients w.r.t. all weights/biases.
    // outputGrad: dL/d(output), same size as outputDim.
    void backward(const float* outputGrad);

    // Adam optimizer step. Call after backward().
    void adamStep(float lr, float beta1 = 0.9f, float beta2 = 0.999f, float epsilon = 1e-8f);

    // Zero all accumulated gradients.
    void zeroGrad();

    // Save weights in MLPPolicy binary format.
    bool saveWeights(const std::string& path) const;

    // Load weights from MLPPolicy binary format.
    bool loadWeights(const std::string& path);

    size_t getInputDim() const { return config_.inputDim; }
    size_t getOutputDim() const { return config_.outputDim; }
    size_t parameterCount() const;

private:
    struct Layer {
        size_t inputDim;
        size_t outputDim;

        std::vector<float> weights;    // [outDim * inDim] row-major
        std::vector<float> biases;     // [outDim]

        // Gradients
        std::vector<float> dWeights;
        std::vector<float> dBiases;

        // Adam state
        std::vector<float> mWeights, vWeights;
        std::vector<float> mBiases, vBiases;

        // Activations stored during forward pass
        std::vector<float> preActivation;  // before ELU
        std::vector<float> postActivation; // after ELU (or linear for output)
    };

    void initXavier();

    Config config_;
    std::vector<Layer> layers_;
    std::vector<float> inputCopy_; // copy of input for backward pass
    size_t adamT_ = 0;            // Adam timestep counter
};

// Gaussian policy layer on top of an MLP.
// Outputs a mean (from the MLP) and learned log-std per action dimension.
struct GaussianPolicy {
    TrainingMLP network;
    std::vector<float> logStd;       // learnable per-action log std dev
    std::vector<float> dLogStd;      // gradient
    std::vector<float> mLogStd, vLogStd; // Adam state

    explicit GaussianPolicy(const TrainingMLP::Config& config, float initialLogStd = -0.5f);

    // Sample action from N(mean, std^2). Returns log_prob of the sampled action.
    float sampleAction(const float* observation, float* actionOut,
                       std::mt19937& rng);

    // Compute log probability of a given action under current parameters.
    float logProb(const float* observation, const float* action);

    // Backward pass for policy gradient.
    // Accumulates gradients into network and logStd.
    void backward(const float* observation, const float* action, float gradScale);

    // Adam step for both network weights and logStd.
    void adamStep(float lr, float beta1 = 0.9f, float beta2 = 0.999f);

    void zeroGrad();
    bool saveWeights(const std::string& path) const;
    bool loadWeights(const std::string& path);
};
