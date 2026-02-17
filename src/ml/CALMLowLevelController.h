#pragma once

#include "MLPNetwork.h"
#include "Tensor.h"
#include "CALMCharacterConfig.h"

namespace ml {

// CALM Low-Level Controller (LLC): takes a latent code z and observation,
// produces character actions (target joint angles).
//
// Architecture mirrors CALM's AMPStyleCatNet1:
//   1. styleEmbed = tanh(styleMLP(z))        — [512, 256] + tanh
//   2. combined = concat(styleEmbed, obs)
//   3. hidden = relu(mainMLP(combined))       — [1024, 1024, 512] + ReLU
//   4. actions = muHead(hidden)               — linear → actionDim
//
// The style MLP and main MLP are wrapped in a StyleConditionedNetwork.
// The muHead is a separate final linear layer (no activation).
class CALMLowLevelController {
public:
    CALMLowLevelController() = default;

    // Set the style-conditioned network (style MLP + main body MLP).
    // The main MLP's output feeds into the muHead.
    void setNetwork(StyleConditionedNetwork network);

    // Set the final linear layer that produces action means.
    // Input: last hidden layer size, Output: actionDim
    void setMuHead(MLPNetwork muHead);

    // Evaluate the policy: latent + observation → actions
    // latent: 64D latent code (L2 normalized)
    // observation: per-frame observation from CALMObservationExtractor
    // output: action vector of size actionDim (target joint angles)
    void evaluate(const Tensor& latent, const Tensor& observation, Tensor& actions) const;

    // Check if the controller has loaded weights
    bool isLoaded() const { return network_.styleMLP().numLayers() > 0; }

    // Access internals for weight loading
    StyleConditionedNetwork& network() { return network_; }
    const StyleConditionedNetwork& network() const { return network_; }
    MLPNetwork& muHead() { return muHead_; }
    const MLPNetwork& muHead() const { return muHead_; }

private:
    StyleConditionedNetwork network_;  // Style MLP + main body
    MLPNetwork muHead_;                // Final linear: hidden → actions

    mutable Tensor hiddenOutput_;
};

} // namespace ml
