#pragma once

#include "ILeafGenerator.h"
#include <random>

// Generates billboard quad leaves placed along terminal branches
class BillboardLeafGenerator : public ILeafGenerator {
public:
    void generateLeaves(const TreeStructure& tree,
                       const TreeParameters& params,
                       std::mt19937& rng,
                       std::vector<LeafInstance>& outLeaves) override;

    void buildLeafMesh(const std::vector<LeafInstance>& leaves,
                      const TreeParameters& params,
                      std::vector<Vertex>& outVertices,
                      std::vector<uint32_t>& outIndices) override;

    const char* getName() const override { return "Billboard Leaves"; }

private:
    // Generate leaves for a single branch
    void generateLeavesForBranch(const Branch& branch,
                                const TreeParameters& params,
                                std::mt19937& rng,
                                std::vector<LeafInstance>& outLeaves);

    // Random utilities
    static float randomFloat(std::mt19937& rng, float min, float max);
    static glm::vec3 randomOnSphere(std::mt19937& rng);
};
