#pragma once

#include "TreeStructure.h"
#include "TreeParameters.h"
#include "Mesh.h"
#include <vector>

// Interface for generating renderable geometry from branch structures
// Different implementations can produce different visual styles
class IBranchGeometryGenerator {
public:
    virtual ~IBranchGeometryGenerator() = default;

    // Generate vertices and indices from a tree structure
    virtual void generate(const TreeStructure& tree,
                         const TreeParameters& params,
                         std::vector<Vertex>& outVertices,
                         std::vector<uint32_t>& outIndices) = 0;

    // Get the name of this generator for debugging/UI
    virtual const char* getName() const = 0;
};
