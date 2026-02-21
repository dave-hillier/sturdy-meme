#include "MLPNetwork.h"
#include <cassert>
#include <SDL3/SDL_log.h>

namespace ml {

// ---------------------------------------------------------------------------
// MLPNetwork
// ---------------------------------------------------------------------------

void MLPNetwork::addLayer(int inFeatures, int outFeatures, Activation activation) {
    LinearLayer layer;
    layer.inFeatures = inFeatures;
    layer.outFeatures = outFeatures;
    layer.weights = Tensor(outFeatures, inFeatures);
    layer.bias = Tensor(outFeatures);
    layers_.push_back(std::move(layer));
    activations_.push_back(activation);
}

void MLPNetwork::setLayerWeights(size_t layerIndex,
                                  std::vector<float> weights,
                                  std::vector<float> bias) {
    assert(layerIndex < layers_.size());
    auto& l = layers_[layerIndex];
    assert(static_cast<int>(weights.size()) == l.outFeatures * l.inFeatures);
    assert(static_cast<int>(bias.size()) == l.outFeatures);
    l.weights = Tensor(l.outFeatures, l.inFeatures, std::move(weights));
    l.bias = Tensor(1, l.outFeatures, std::move(bias));
}

void MLPNetwork::forward(const Tensor& input, Tensor& output) const {
    if (layers_.empty()) {
        return;
    }

    assert(static_cast<int>(input.size()) == layers_[0].inFeatures);

    // Ping-pong between two scratch tensors, resized exactly per layer
    scratch1_ = Tensor(layers_[0].outFeatures);
    scratch2_ = Tensor(layers_.size() > 1
                        ? layers_[1].outFeatures
                        : layers_[0].outFeatures);

    const Tensor* current = &input;
    bool useFirst = true;

    for (size_t i = 0; i < layers_.size(); ++i) {
        const auto& layer = layers_[i];
        Tensor& dest = useFirst ? scratch1_ : scratch2_;

        // Ensure dest is exactly the right size for this layer's output
        if (static_cast<int>(dest.size()) != layer.outFeatures) {
            dest = Tensor(layer.outFeatures);
        }

        // out = W * input + bias
        Tensor::matVecMul(layer.weights, *current, dest);
        Tensor::addBias(dest, layer.bias);

        // Apply activation
        switch (activations_[i]) {
            case Activation::ReLU:
                Tensor::relu(dest);
                break;
            case Activation::Tanh:
                Tensor::tanh(dest);
                break;
            case Activation::None:
                break;
        }

        current = &dest;
        useFirst = !useFirst;
    }

    // Copy result to output
    const int lastOut = layers_.back().outFeatures;
    if (static_cast<int>(output.size()) != lastOut) {
        output = Tensor(lastOut);
    }
    for (int i = 0; i < lastOut; ++i) {
        output[i] = (*current)[i];
    }
}

int MLPNetwork::inputSize() const {
    if (layers_.empty()) return 0;
    return layers_.front().inFeatures;
}

int MLPNetwork::outputSize() const {
    if (layers_.empty()) return 0;
    return layers_.back().outFeatures;
}

// ---------------------------------------------------------------------------
// StyleConditionedNetwork
// ---------------------------------------------------------------------------

void StyleConditionedNetwork::setStyleMLP(MLPNetwork styleMLP) {
    styleMLP_ = std::move(styleMLP);
}

void StyleConditionedNetwork::setMainMLP(MLPNetwork mainMLP) {
    mainMLP_ = std::move(mainMLP);
}

void StyleConditionedNetwork::forward(const Tensor& latent,
                                       const Tensor& observation,
                                       Tensor& output) const {
    // Step 1: styleEmbed = tanh(styleMLP(z))
    // Note: tanh is expected to be the last activation in styleMLP
    styleMLP_.forward(latent, styleEmbed_);

    // Step 2: combined = concat(styleEmbed, obs)
    combined_ = Tensor::concat(styleEmbed_, observation);

    // Step 3: output = mainMLP(combined)
    mainMLP_.forward(combined_, output);
}

void StyleConditionedNetwork::forwardNoStyle(const Tensor& observation,
                                              Tensor& output) const {
    // Create zero style embedding
    int styleSize = styleMLP_.outputSize();
    if (static_cast<int>(styleEmbed_.size()) != styleSize) {
        styleEmbed_ = Tensor(styleSize);
    }
    styleEmbed_.fill(0.0f);

    combined_ = Tensor::concat(styleEmbed_, observation);
    mainMLP_.forward(combined_, output);
}

} // namespace ml
