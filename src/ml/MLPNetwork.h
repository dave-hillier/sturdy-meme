#pragma once

#include "Tensor.h"
#include <vector>
#include <string>
#include <cstddef>

namespace ml {

// Activation function applied after a linear layer
enum class Activation {
    None,
    ReLU,
    Tanh
};

// A single fully-connected layer: output = activation(W * input + bias)
struct LinearLayer {
    Tensor weights;  // [outFeatures x inFeatures], row-major
    Tensor bias;     // [outFeatures]
    int inFeatures = 0;
    int outFeatures = 0;
};

// Feedforward MLP for neural network inference.
// Supports linear layers with ReLU/Tanh activations.
// Designed for CALM policy/encoder/discriminator networks.
class MLPNetwork {
public:
    MLPNetwork() = default;

    // Add a layer with given weight dimensions and activation.
    // Weights and biases are set later via setLayerWeights() or ModelLoader.
    void addLayer(int inFeatures, int outFeatures, Activation activation);

    // Set weights and bias for a specific layer.
    // weights: row-major [outFeatures x inFeatures]
    // bias: [outFeatures]
    void setLayerWeights(size_t layerIndex,
                         std::vector<float> weights,
                         std::vector<float> bias);

    // Forward pass: input → output
    // input size must match first layer's inFeatures
    // output size will be last layer's outFeatures
    void forward(const Tensor& input, Tensor& output) const;

    // Get the expected input size
    int inputSize() const;

    // Get the output size
    int outputSize() const;

    // Get number of layers
    size_t numLayers() const { return layers_.size(); }

    // Access layer for weight loading
    LinearLayer& layer(size_t index) { return layers_[index]; }
    const LinearLayer& layer(size_t index) const { return layers_[index]; }

private:
    std::vector<LinearLayer> layers_;
    std::vector<Activation> activations_;

    // Scratch buffers to avoid per-frame allocations (mutable for const forward())
    mutable Tensor scratch1_;
    mutable Tensor scratch2_;
};

// Style-conditioned network matching CALM's AMPStyleCatNet1 architecture.
// Forward: styleEmbed = tanh(styleMLP(z)), combined = concat(styleEmbed, obs),
//          output = mainMLP(combined)
class StyleConditionedNetwork {
public:
    StyleConditionedNetwork() = default;

    // Configure the two sub-networks.
    // styleMLP: processes latent z → style embedding
    // mainMLP: processes concat(styleEmbed, obs) → output
    void setStyleMLP(MLPNetwork styleMLP);
    void setMainMLP(MLPNetwork mainMLP);

    // Forward pass with style conditioning
    // latent: the z vector (e.g., 64D)
    // observation: the state observation
    // output: result from the main MLP
    void forward(const Tensor& latent, const Tensor& observation, Tensor& output) const;

    // Forward pass without style (passes zero style embedding)
    void forwardNoStyle(const Tensor& observation, Tensor& output) const;

    // Accessors for weight loading
    MLPNetwork& styleMLP() { return styleMLP_; }
    MLPNetwork& mainMLP() { return mainMLP_; }
    const MLPNetwork& styleMLP() const { return styleMLP_; }
    const MLPNetwork& mainMLP() const { return mainMLP_; }

private:
    MLPNetwork styleMLP_;   // z → tanh → style embed
    MLPNetwork mainMLP_;    // concat(styleEmbed, obs) → output

    mutable Tensor styleEmbed_;
    mutable Tensor combined_;
};

} // namespace ml
