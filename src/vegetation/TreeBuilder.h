#pragma once

#include "TreeStructure.h"
#include "TreeParameters.h"
#include "ITreeGenerationStrategy.h"
#include "IBranchGeometryGenerator.h"
#include "ILeafGenerator.h"
#include "Mesh.h"
#include <memory>
#include <random>

// Builder class for constructing trees with a fluent API
// Composes generation strategy, geometry generator, and leaf generator
class TreeBuilder {
public:
    TreeBuilder();

    // Fluent API for configuration
    TreeBuilder& withParameters(const TreeParameters& params);
    TreeBuilder& withSeed(uint32_t seed);

    // Strategy selection
    TreeBuilder& withGenerationStrategy(std::unique_ptr<ITreeGenerationStrategy> strategy);
    TreeBuilder& withGeometryGenerator(std::unique_ptr<IBranchGeometryGenerator> generator);
    TreeBuilder& withLeafGenerator(std::unique_ptr<ILeafGenerator> generator);

    // Convenience methods for common strategies
    TreeBuilder& useRecursiveBranching();
    TreeBuilder& useSpaceColonisation();

    // Build the tree
    TreeBuilder& build();

    // Access results
    const TreeStructure& getTreeStructure() const { return tree; }
    TreeStructure& getTreeStructure() { return tree; }

    const std::vector<Vertex>& getBranchVertices() const { return branchVertices; }
    const std::vector<uint32_t>& getBranchIndices() const { return branchIndices; }

    const std::vector<LeafInstance>& getLeafInstances() const { return leafInstances; }
    const std::vector<Vertex>& getLeafVertices() const { return leafVertices; }
    const std::vector<uint32_t>& getLeafIndices() const { return leafIndices; }

    // Build meshes from generated geometry
    void buildBranchMesh(Mesh& outMesh);
    void buildLeafMesh(Mesh& outMesh);

    // Get tree statistics
    size_t getBranchCount() const { return tree.getTotalBranchCount(); }
    size_t getLeafCount() const { return leafInstances.size(); }
    float getTreeHeight() const { return tree.getApproximateHeight(); }
    glm::vec3 getTreeCenter() const { return tree.getCenter(); }

private:
    // Components
    std::unique_ptr<ITreeGenerationStrategy> generationStrategy;
    std::unique_ptr<IBranchGeometryGenerator> geometryGenerator;
    std::unique_ptr<ILeafGenerator> leafGenerator;

    // Configuration
    TreeParameters params;
    std::mt19937 rng;

    // Output data
    TreeStructure tree;
    std::vector<Vertex> branchVertices;
    std::vector<uint32_t> branchIndices;
    std::vector<LeafInstance> leafInstances;
    std::vector<Vertex> leafVertices;
    std::vector<uint32_t> leafIndices;
};
