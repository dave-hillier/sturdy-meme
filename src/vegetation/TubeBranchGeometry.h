#pragma once

#include "IBranchGeometryGenerator.h"
#include "TreeVertex.h"

// Generates cylindrical tube geometry for branches
// This is the standard visualization for tree branches
class TubeBranchGeometry : public IBranchGeometryGenerator {
public:
    void generate(const TreeStructure& tree,
                 const TreeParameters& params,
                 std::vector<Vertex>& outVertices,
                 std::vector<uint32_t>& outIndices) override;

    // Generate geometry with wind animation data (TreeVertex)
    // Based on Ghost of Tsushima's approach where each vertex stores:
    // - Branch origin point for rotation
    // - Branch level for different sway characteristics
    // - Flexibility (0 at base, 1 at tip)
    // - Phase offset for motion variation
    void generateWithWind(const TreeStructure& tree,
                          const TreeParameters& params,
                          std::vector<TreeVertex>& outVertices,
                          std::vector<uint32_t>& outIndices);

    const char* getName() const override { return "Tube Geometry"; }

private:
    // Generate geometry for a single branch (standard Vertex)
    void generateBranchGeometry(const Branch& branch,
                                const TreeParameters& params,
                                std::vector<Vertex>& outVertices,
                                std::vector<uint32_t>& outIndices);

    // Generate geometry for a single branch with wind data (TreeVertex)
    void generateBranchGeometryWithWind(const Branch& branch,
                                        const TreeParameters& params,
                                        std::vector<TreeVertex>& outVertices,
                                        std::vector<uint32_t>& outIndices);
};
