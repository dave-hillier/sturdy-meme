#pragma once

#include "TreeStructure.h"
#include "TreeParameters.h"
#include "Mesh.h"
#include <vector>
#include <random>

// Interface for generating leaf instances on a tree
// Different implementations can produce different leaf placement strategies
class ILeafGenerator {
public:
    virtual ~ILeafGenerator() = default;

    // Generate leaf instances for a tree structure
    virtual void generateLeaves(const TreeStructure& tree,
                               const TreeParameters& params,
                               std::mt19937& rng,
                               std::vector<LeafInstance>& outLeaves) = 0;

    // Build leaf mesh from instances
    virtual void buildLeafMesh(const std::vector<LeafInstance>& leaves,
                               const TreeParameters& params,
                               std::vector<Vertex>& outVertices,
                               std::vector<uint32_t>& outIndices) = 0;

    // Get the name of this generator for debugging/UI
    virtual const char* getName() const = 0;
};
