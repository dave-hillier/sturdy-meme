#include "TreeGenerator.h"
#include "RecursiveBranchingStrategy.h"
#include "SpaceColonisationStrategy.h"
#include "TubeBranchGeometry.h"
#include "BillboardLeafGenerator.h"
#include "SpaceColonisationGenerator.h"
#include "CurvedGeometryGenerator.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void TreeGenerator::generate(const TreeParameters& params) {
    // Clear previous data
    segments.clear();
    branchVertices.clear();
    branchIndices.clear();
    leafInstances.clear();
    leafVertices.clear();
    leafIndices.clear();

    // Seed random generator
    rng.seed(params.seed);

    // Configure builder based on algorithm
    builder.withParameters(params);
    builder.withSeed(params.seed);

    if (params.algorithm == TreeAlgorithm::SpaceColonisation) {
        // For space colonisation, we use the existing optimized path
        // that generates curved geometry directly from nodes
        generateSpaceColonisationDirect(params);
    } else {
        // Use the new component system for recursive branching
        builder.useRecursiveBranching();
        builder.build();

        // Copy results from builder to our storage
        treeStructure = builder.getTreeStructure();
        branchVertices = builder.getBranchVertices();
        branchIndices = builder.getBranchIndices();
        leafInstances = builder.getLeafInstances();
        leafVertices = builder.getLeafVertices();
        leafIndices = builder.getLeafIndices();

        // Generate legacy segments for compatibility
        updateLegacySegments();
    }

    SDL_Log("TreeGenerator: Generated %zu vertices, %zu indices, %zu leaves",
            branchVertices.size(), branchIndices.size(), leafInstances.size());
}

void TreeGenerator::generateSpaceColonisationDirect(const TreeParameters& params) {
    // Space colonisation uses the optimized CurvedGeometryGenerator
    // which produces better curved branches than the tube geometry
    SpaceColonisationGenerator scGen(rng);
    std::vector<TreeNode> nodes;

    scGen.generate(params, nodes);

    // Generate curved geometry directly (existing optimized path)
    CurvedGeometryGenerator curveGen;
    curveGen.generateCurvedBranchGeometry(nodes, params, branchVertices, branchIndices);

    // Generate leaves using the new leaf generator
    BillboardLeafGenerator leafGen;

    // Create a temporary tree structure for leaf generation
    // We need to convert TreeNodes to our Branch hierarchy
    convertNodesToTreeStructure(nodes, params);

    if (params.generateLeaves) {
        leafGen.generateLeaves(treeStructure, params, rng, leafInstances);
        leafGen.buildLeafMesh(leafInstances, params, leafVertices, leafIndices);
    }

    SDL_Log("Space colonisation: %zu vertices, %zu indices",
            branchVertices.size(), branchIndices.size());
}

void TreeGenerator::convertNodesToTreeStructure(const std::vector<TreeNode>& nodes,
                                                 const TreeParameters& params) {
    if (nodes.empty()) return;

    // Find root node
    int rootIdx = -1;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].parentIndex == -1) {
            rootIdx = static_cast<int>(i);
            break;
        }
    }
    if (rootIdx == -1) return;

    // Build Branch hierarchy from nodes
    std::map<int, Branch*> nodeToBranch;

    const TreeNode& rootNode = nodes[rootIdx];
    glm::quat rootOrientation(1.0f, 0.0f, 0.0f, 0.0f);

    if (!rootNode.childIndices.empty()) {
        glm::vec3 toChild = nodes[rootNode.childIndices[0]].position - rootNode.position;
        float len = glm::length(toChild);
        if (len > 0.0001f) {
            glm::vec3 dir = toChild / len;
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            float dot = glm::dot(up, dir);
            if (dot < 0.9999f && dot > -0.9999f) {
                glm::vec3 axis = glm::normalize(glm::cross(up, dir));
                rootOrientation = glm::angleAxis(std::acos(dot), axis);
            }
        }
    }

    Branch::Properties rootProps;
    rootProps.length = 0.01f;
    rootProps.startRadius = params.spaceColonisation.baseThickness;
    rootProps.endRadius = rootNode.thickness;
    rootProps.level = 0;
    rootProps.radialSegments = params.spaceColonisation.radialSegments;
    rootProps.lengthSegments = 2;

    Branch root(rootNode.position, rootOrientation, rootProps);
    nodeToBranch[rootIdx] = &root;

    // Process nodes BFS
    std::vector<int> toProcess = {rootIdx};

    while (!toProcess.empty()) {
        int currentIdx = toProcess.front();
        toProcess.erase(toProcess.begin());

        const TreeNode& currentNode = nodes[currentIdx];
        Branch* currentBranch = nodeToBranch[currentIdx];
        if (!currentBranch) continue;

        for (int childIdx : currentNode.childIndices) {
            if (childIdx < 0 || childIdx >= static_cast<int>(nodes.size())) continue;

            const TreeNode& childNode = nodes[childIdx];

            glm::vec3 toChild = childNode.position - currentNode.position;
            float len = glm::length(toChild);
            glm::quat childOrientation(1.0f, 0.0f, 0.0f, 0.0f);

            if (len > 0.0001f) {
                glm::vec3 dir = toChild / len;
                glm::vec3 up(0.0f, 1.0f, 0.0f);
                float dot = glm::dot(up, dir);
                if (dot < 0.9999f && dot > -0.9999f) {
                    glm::vec3 axis = glm::normalize(glm::cross(up, dir));
                    childOrientation = glm::angleAxis(std::acos(dot), axis);
                }
            }

            Branch::Properties childProps;
            childProps.length = len;
            childProps.startRadius = childNode.thickness;
            childProps.endRadius = childNode.thickness * 0.8f;
            childProps.level = childNode.depth;
            childProps.radialSegments = params.spaceColonisation.radialSegments;
            childProps.lengthSegments = 2;

            Branch& newChild = currentBranch->addChild(currentNode.position, childOrientation, childProps);
            nodeToBranch[childIdx] = &newChild;
            toProcess.push_back(childIdx);
        }
    }

    treeStructure.setRoot(std::move(root));
}

void TreeGenerator::updateLegacySegments() {
    segments = treeStructure.flattenToSegments();
}

void TreeGenerator::buildMesh(Mesh& outMesh) {
    if (branchVertices.empty()) return;
    outMesh.setCustomGeometry(branchVertices, branchIndices);
}

void TreeGenerator::buildLeafMesh(Mesh& outMesh, const TreeParameters& params) {
    if (leafVertices.empty()) {
        // If we don't have cached leaf vertices, build them
        if (!leafInstances.empty()) {
            BillboardLeafGenerator leafGen;
            leafGen.buildLeafMesh(leafInstances, params, leafVertices, leafIndices);
        }
    }

    if (!leafVertices.empty()) {
        outMesh.setCustomGeometry(leafVertices, leafIndices);
    }
}

void TreeGenerator::buildWindMesh(TreeMesh& outMesh, const TreeParameters& params) {
    // Generate TreeVertex data with wind animation parameters
    // Uses TubeBranchGeometry::generateWithWind() for branches
    std::vector<TreeVertex> windVertices;
    std::vector<uint32_t> windIndices;

    TubeBranchGeometry tubeGen;

    // Generate wind vertices from the tree structure
    tubeGen.generateWithWind(treeStructure, params, windVertices, windIndices);

    if (!windVertices.empty()) {
        outMesh.setCustomGeometry(windVertices, windIndices);
        SDL_Log("TreeGenerator: Built wind mesh with %zu vertices", windVertices.size());
    }
}

void TreeGenerator::buildWindLeafMesh(TreeMesh& outMesh, const TreeParameters& params) {
    std::vector<TreeVertex> windLeafVertices;
    std::vector<uint32_t> windLeafIndices;

    if (!leafInstances.empty()) {
        BillboardLeafGenerator leafGen;
        leafGen.buildLeafMeshWithWind(leafInstances, params, windLeafVertices, windLeafIndices);
    }

    if (!windLeafVertices.empty()) {
        outMesh.setCustomGeometry(windLeafVertices, windLeafIndices);
        SDL_Log("TreeGenerator: Built wind leaf mesh with %zu vertices", windLeafVertices.size());
    }
}
