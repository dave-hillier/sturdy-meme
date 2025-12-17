#pragma once

#include "ITreeGenerationStrategy.h"
#include "SpaceColonisationGenerator.h"
#include <random>

// Space colonisation tree generation strategy
// Creates trees by growing towards attraction points in a volume
class SpaceColonisationStrategy : public ITreeGenerationStrategy {
public:
    void generate(const TreeParameters& params,
                 std::mt19937& rng,
                 TreeStructure& outTree) override;

    const char* getName() const override { return "Space Colonisation"; }

private:
    // Convert TreeNodes to hierarchical Branch structure
    void convertNodesToTree(const std::vector<TreeNode>& nodes,
                           const TreeParameters& params,
                           TreeStructure& outTree);
};
