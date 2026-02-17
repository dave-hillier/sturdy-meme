#include "CALMLowLevelController.h"
#include <cassert>

namespace ml {

void CALMLowLevelController::setNetwork(StyleConditionedNetwork network) {
    network_ = std::move(network);
}

void CALMLowLevelController::setMuHead(MLPNetwork muHead) {
    muHead_ = std::move(muHead);
}

void CALMLowLevelController::evaluate(const Tensor& latent,
                                       const Tensor& observation,
                                       Tensor& actions) const {
    // Step 1-3: Style conditioning + main MLP
    network_.forward(latent, observation, hiddenOutput_);

    // Step 4: muHead produces action means
    if (muHead_.numLayers() > 0) {
        muHead_.forward(hiddenOutput_, actions);
    } else {
        // No separate muHead — the network output IS the actions
        actions = hiddenOutput_;
    }
}

} // namespace ml
