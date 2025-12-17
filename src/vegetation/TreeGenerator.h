#pragma once

#include "TreeParameters.h"
#include "TreeGeometry.h"
#include "TreeStructure.h"
#include "TreeBuilder.h"
#include "Mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <random>
#include <memory>
#include <map>

// TreeGenerator - Facade for tree generation using the new component system
// Maintains backward compatibility with existing code while internally using
// TreeBuilder, strategies, and geometry generators
class TreeGenerator {
public:
    TreeGenerator() = default;
    ~TreeGenerator() = default;

    // Generate tree geometry from parameters
    // This is the main entry point that delegates to the new component system
    void generate(const TreeParameters& params);

    // Get generated geometry (for backward compatibility)
    const std::vector<Vertex>& getBranchVertices() const { return branchVertices; }
    const std::vector<uint32_t>& getBranchIndices() const { return branchIndices; }
    const std::vector<LeafInstance>& getLeafInstances() const { return leafInstances; }

    // Get branch segments (for visualization/debugging)
    const std::vector<BranchSegment>& getBranchSegments() const { return segments; }

    // Access the hierarchical tree structure (new API)
    const TreeStructure& getTreeStructure() const { return treeStructure; }
    TreeStructure& getTreeStructure() { return treeStructure; }

    // Build mesh from generated geometry
    void buildMesh(Mesh& outMesh);

    // Get leaf vertex data (quad billboards)
    void buildLeafMesh(Mesh& outMesh, const TreeParameters& params);

    // Access the underlying builder for advanced customization
    TreeBuilder& getBuilder() { return builder; }

private:
    // Legacy support - generates branch segments from tree structure
    void updateLegacySegments();

    // Space colonisation with optimized curved geometry
    void generateSpaceColonisationDirect(const TreeParameters& params);

    // Convert TreeNodes to hierarchical Branch structure
    void convertNodesToTreeStructure(const std::vector<TreeNode>& nodes,
                                     const TreeParameters& params);

    // Storage
    TreeBuilder builder;
    TreeStructure treeStructure;

    // Legacy output buffers (populated from TreeBuilder results)
    std::vector<BranchSegment> segments;
    std::vector<Vertex> branchVertices;
    std::vector<uint32_t> branchIndices;
    std::vector<LeafInstance> leafInstances;

    // Cached leaf vertices for buildLeafMesh
    std::vector<Vertex> leafVertices;
    std::vector<uint32_t> leafIndices;

    std::mt19937 rng;
};
