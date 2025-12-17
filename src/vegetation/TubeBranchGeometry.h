#pragma once

#include "IBranchGeometryGenerator.h"

// Generates cylindrical tube geometry for branches
// This is the standard visualization for tree branches
class TubeBranchGeometry : public IBranchGeometryGenerator {
public:
    void generate(const TreeStructure& tree,
                 const TreeParameters& params,
                 std::vector<Vertex>& outVertices,
                 std::vector<uint32_t>& outIndices) override;

    const char* getName() const override { return "Tube Geometry"; }

private:
    // Generate geometry for a single branch
    void generateBranchGeometry(const Branch& branch,
                                const TreeParameters& params,
                                std::vector<Vertex>& outVertices,
                                std::vector<uint32_t>& outIndices);
};
