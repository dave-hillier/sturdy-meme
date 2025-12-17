#include "TreeBuilder.h"
#include "RecursiveBranchingStrategy.h"
#include "SpaceColonisationStrategy.h"
#include "TubeBranchGeometry.h"
#include "BillboardLeafGenerator.h"
#include <SDL3/SDL.h>

TreeBuilder::TreeBuilder() {
    // Set up defaults
    generationStrategy = std::make_unique<RecursiveBranchingStrategy>();
    geometryGenerator = std::make_unique<TubeBranchGeometry>();
    leafGenerator = std::make_unique<BillboardLeafGenerator>();
}

TreeBuilder& TreeBuilder::withParameters(const TreeParameters& newParams) {
    params = newParams;
    return *this;
}

TreeBuilder& TreeBuilder::withSeed(uint32_t seed) {
    params.seed = seed;
    rng.seed(seed);
    return *this;
}

TreeBuilder& TreeBuilder::withGenerationStrategy(std::unique_ptr<ITreeGenerationStrategy> strategy) {
    if (strategy) {
        generationStrategy = std::move(strategy);
    }
    return *this;
}

TreeBuilder& TreeBuilder::withGeometryGenerator(std::unique_ptr<IBranchGeometryGenerator> generator) {
    if (generator) {
        geometryGenerator = std::move(generator);
    }
    return *this;
}

TreeBuilder& TreeBuilder::withLeafGenerator(std::unique_ptr<ILeafGenerator> generator) {
    if (generator) {
        leafGenerator = std::move(generator);
    }
    return *this;
}

TreeBuilder& TreeBuilder::useRecursiveBranching() {
    generationStrategy = std::make_unique<RecursiveBranchingStrategy>();
    params.algorithm = TreeAlgorithm::Recursive;
    return *this;
}

TreeBuilder& TreeBuilder::useSpaceColonisation() {
    generationStrategy = std::make_unique<SpaceColonisationStrategy>();
    params.algorithm = TreeAlgorithm::SpaceColonisation;
    return *this;
}

TreeBuilder& TreeBuilder::build() {
    // Clear previous results
    branchVertices.clear();
    branchIndices.clear();
    leafInstances.clear();
    leafVertices.clear();
    leafIndices.clear();

    // Seed RNG
    rng.seed(params.seed);

    SDL_Log("TreeBuilder: Building tree with strategy '%s'",
            generationStrategy ? generationStrategy->getName() : "none");

    // Step 1: Generate tree structure
    if (generationStrategy) {
        generationStrategy->generate(params, rng, tree);
    }

    // Step 2: Generate branch geometry
    if (geometryGenerator) {
        geometryGenerator->generate(tree, params, branchVertices, branchIndices);
    }

    // Step 3: Generate leaves
    if (leafGenerator && params.generateLeaves) {
        leafGenerator->generateLeaves(tree, params, rng, leafInstances);
        leafGenerator->buildLeafMesh(leafInstances, params, leafVertices, leafIndices);

        // Store leaves in tree structure too
        tree.clearLeaves();
        for (const auto& leaf : leafInstances) {
            tree.addLeaf(leaf);
        }
    }

    SDL_Log("TreeBuilder: Complete - %zu branches, %zu branch verts, %zu leaves",
            tree.getTotalBranchCount(), branchVertices.size(), leafInstances.size());

    return *this;
}

void TreeBuilder::buildBranchMesh(Mesh& outMesh) {
    if (!branchVertices.empty()) {
        outMesh.setCustomGeometry(branchVertices, branchIndices);
    }
}

void TreeBuilder::buildLeafMesh(Mesh& outMesh) {
    if (!leafVertices.empty()) {
        outMesh.setCustomGeometry(leafVertices, leafIndices);
    }
}
