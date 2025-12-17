#pragma once

#include "TreeStructure.h"
#include "TreeParameters.h"
#include <random>
#include <memory>

// Interface for tree generation algorithms (Strategy pattern)
// Different implementations provide different growth algorithms
class ITreeGenerationStrategy {
public:
    virtual ~ITreeGenerationStrategy() = default;

    // Generate a tree structure based on parameters
    virtual void generate(const TreeParameters& params,
                         std::mt19937& rng,
                         TreeStructure& outTree) = 0;

    // Get the name of this strategy for debugging/UI
    virtual const char* getName() const = 0;
};

// Factory function type for creating strategies
using TreeGenerationStrategyFactory = std::unique_ptr<ITreeGenerationStrategy>(*)();
