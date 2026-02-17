#pragma once

#include "MLPNetwork.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ml {

// Loads neural network weights from binary files exported from PyTorch.
//
// File format (.bin):
//   Header:
//     uint32_t magic        = 0x4D4C5031  ("MLP1")
//     uint32_t version      = 1
//     uint32_t numLayers
//
//   Per layer:
//     uint32_t inFeatures
//     uint32_t outFeatures
//     uint32_t activationType  (0=None, 1=ReLU, 2=Tanh)
//     float[outFeatures * inFeatures]  weights (row-major)
//     float[outFeatures]               bias
//
// Total floats per layer: outFeatures * (inFeatures + 1)
//
// Companion Python export script generates this format from PyTorch state dicts.
class ModelLoader {
public:
    static constexpr uint32_t MAGIC = 0x4D4C5031;  // "MLP1"
    static constexpr uint32_t VERSION = 1;

    // Load an MLP from a binary weight file.
    // Returns true on success.
    static bool loadMLP(const std::string& path, MLPNetwork& network);

    // Save an MLP to a binary weight file.
    // Returns true on success.
    static bool saveMLP(const std::string& path, const MLPNetwork& network,
                        const std::vector<Activation>& activations);

    // Load a StyleConditionedNetwork from two separate files.
    // stylePath: weights for the style MLP
    // mainPath: weights for the main MLP
    static bool loadStyleConditioned(const std::string& stylePath,
                                      const std::string& mainPath,
                                      StyleConditionedNetwork& network);
};

} // namespace ml
