#pragma once

#include "ITreeGenerationStrategy.h"
#include <random>

// Recursive branching tree generation strategy
// Creates trees by recursively spawning child branches from parent branches
class RecursiveBranchingStrategy : public ITreeGenerationStrategy {
public:
    void generate(const TreeParameters& params,
                 std::mt19937& rng,
                 TreeStructure& outTree) override;

    const char* getName() const override { return "Recursive Branching"; }

private:
    // Recursive branch generation
    void generateBranch(const TreeParameters& params,
                       Branch& parentBranch,
                       const glm::vec3& startPos,
                       const glm::quat& orientation,
                       float length,
                       float radius,
                       int level);

    // Random utilities
    float randomFloat(float min, float max);

    std::mt19937* rngPtr = nullptr;
};
