#include "SpaceColonisationGenerator.h"
#include <SDL3/SDL.h>
#include <cmath>

SpaceColonisationGenerator::SpaceColonisationGenerator(std::mt19937& rng)
    : rng(rng), volumeGen(rng) {}

void SpaceColonisationGenerator::generate(const TreeParameters& params,
                                          std::vector<TreeNode>& outNodes) {
    const auto& scParams = params.spaceColonisation;

    outNodes.clear();
    std::vector<glm::vec3> attractionPoints;

    // Create initial trunk nodes
    glm::vec3 trunkBase(0.0f, 0.0f, 0.0f);
    int trunkSegmentCount = scParams.trunkSegments;
    float segmentHeight = scParams.trunkHeight / static_cast<float>(trunkSegmentCount);

    for (int i = 0; i <= trunkSegmentCount; ++i) {
        TreeNode node;
        node.position = trunkBase + glm::vec3(0.0f, i * segmentHeight, 0.0f);
        node.parentIndex = i > 0 ? i - 1 : -1;
        node.childCount = (i < trunkSegmentCount) ? 1 : 0;
        node.thickness = scParams.baseThickness;
        node.isTerminal = (i == trunkSegmentCount);
        node.depth = 0;  // Trunk is level 0
        outNodes.push_back(node);
    }

    // Crown center is at top of trunk plus offset
    glm::vec3 crownCenter = glm::vec3(0.0f, scParams.trunkHeight, 0.0f) + scParams.crownOffset;

    // Generate attraction points for crown
    volumeGen.generateAttractionPoints(scParams, crownCenter, false, attractionPoints);

    SDL_Log("Space colonisation: Generated %zu attraction points for crown",
            attractionPoints.size());

    // Generate attraction points for roots if enabled
    std::vector<glm::vec3> rootAttractionPoints;
    if (scParams.generateRoots) {
        volumeGen.generateAttractionPoints(scParams, trunkBase, true, rootAttractionPoints);
        SDL_Log("Space colonisation: Generated %zu attraction points for roots",
                rootAttractionPoints.size());
    }

    // Run space colonisation algorithm for crown
    int iterations = 0;
    while (iterations < scParams.maxIterations && !attractionPoints.empty()) {
        bool grew = spaceColonisationStep(outNodes, attractionPoints,
                                          scParams,
                                          scParams.tropismDirection,
                                          scParams.tropismStrength);
        if (!grew) break;
        iterations++;
    }

    SDL_Log("Space colonisation: Crown completed in %d iterations, %zu nodes",
            iterations, outNodes.size());

    // Run space colonisation for roots
    if (scParams.generateRoots && !rootAttractionPoints.empty()) {
        // Add root base node
        TreeNode rootBase;
        rootBase.position = trunkBase;
        rootBase.parentIndex = 0;  // Connect to trunk base
        rootBase.childCount = 0;
        rootBase.thickness = scParams.baseThickness * 0.8f;
        rootBase.isTerminal = true;
        rootBase.depth = 0;  // Root base is level 0
        int rootBaseIdx = static_cast<int>(outNodes.size());
        outNodes.push_back(rootBase);

        // Create separate list for root nodes
        std::vector<TreeNode> rootNodes;
        rootNodes.push_back(rootBase);

        int rootIterations = 0;
        while (rootIterations < scParams.maxIterations / 2 && !rootAttractionPoints.empty()) {
            bool grew = spaceColonisationStep(rootNodes, rootAttractionPoints,
                                              scParams,
                                              glm::vec3(0.0f, -1.0f, 0.0f),
                                              scParams.rootTropismStrength);
            if (!grew) break;
            rootIterations++;
        }

        // Merge root nodes into main node list (adjusting parent indices)
        for (size_t i = 1; i < rootNodes.size(); ++i) {
            TreeNode node = rootNodes[i];
            node.parentIndex += rootBaseIdx;
            outNodes.push_back(node);
        }

        SDL_Log("Space colonisation: Roots completed in %d iterations, %zu additional nodes",
                rootIterations, rootNodes.size() - 1);
    }

    // Calculate branch thicknesses
    calculateBranchThickness(outNodes, scParams);

    // Build child index lists
    buildChildIndices(outNodes);
}

bool SpaceColonisationGenerator::spaceColonisationStep(std::vector<TreeNode>& nodes,
                                                        std::vector<glm::vec3>& attractionPoints,
                                                        const SpaceColonisationParams& params,
                                                        const glm::vec3& tropismDir,
                                                        float tropismStrength) {
    if (attractionPoints.empty() || nodes.empty()) {
        return false;
    }

    // For each node, accumulate influence from nearby attraction points
    std::vector<glm::vec3> growthDirections(nodes.size(), glm::vec3(0.0f));
    std::vector<int> influenceCount(nodes.size(), 0);
    std::vector<bool> pointsToRemove(attractionPoints.size(), false);

    // Find closest node for each attraction point
    for (size_t pi = 0; pi < attractionPoints.size(); ++pi) {
        const glm::vec3& point = attractionPoints[pi];

        int closestNode = -1;
        float closestDist = params.attractionDistance;

        for (size_t ni = 0; ni < nodes.size(); ++ni) {
            float dist = glm::distance(nodes[ni].position, point);

            // Check kill distance
            if (dist < params.killDistance) {
                pointsToRemove[pi] = true;
                closestNode = -1;
                break;
            }

            if (dist < closestDist) {
                closestDist = dist;
                closestNode = static_cast<int>(ni);
            }
        }

        if (closestNode >= 0) {
            glm::vec3 dir = glm::normalize(point - nodes[closestNode].position);
            growthDirections[closestNode] += dir;
            influenceCount[closestNode]++;
        }
    }

    // Remove killed points
    std::vector<glm::vec3> remainingPoints;
    for (size_t i = 0; i < attractionPoints.size(); ++i) {
        if (!pointsToRemove[i]) {
            remainingPoints.push_back(attractionPoints[i]);
        }
    }
    attractionPoints = std::move(remainingPoints);

    // Grow new nodes
    bool grewAny = false;
    size_t originalNodeCount = nodes.size();

    for (size_t i = 0; i < originalNodeCount; ++i) {
        if (influenceCount[i] > 0) {
            glm::vec3 avgDir = glm::normalize(growthDirections[i]);

            // Apply tropism
            if (tropismStrength > 0.0f) {
                avgDir = glm::normalize(avgDir + tropismDir * tropismStrength);
            }

            // Create new node
            TreeNode newNode;
            newNode.position = nodes[i].position + avgDir * params.segmentLength;
            newNode.parentIndex = static_cast<int>(i);
            newNode.childCount = 0;
            newNode.thickness = params.minThickness;
            newNode.isTerminal = true;
            newNode.depth = nodes[i].depth + 1;

            // Update parent
            nodes[i].childCount++;
            nodes[i].isTerminal = false;

            nodes.push_back(newNode);
            grewAny = true;
        }
    }

    return grewAny;
}

void SpaceColonisationGenerator::calculateBranchThickness(std::vector<TreeNode>& nodes,
                                                           const SpaceColonisationParams& params) {
    if (nodes.empty()) return;

    // Calculate child count for each node by traversing from leaves to root
    // First, find all terminal nodes and propagate thickness upward

    // Using pipe model (da Vinci's rule):
    // parent_area = sum of children_areas
    // parent_radius^n = sum of children_radius^n

    // Start from terminal nodes with minimum thickness
    for (auto& node : nodes) {
        if (node.isTerminal || node.childCount == 0) {
            node.thickness = params.minThickness;
        }
    }

    // Propagate thickness from leaves to root
    // Process nodes in reverse order (children before parents)
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
        TreeNode& node = nodes[i];

        if (node.parentIndex >= 0 && node.parentIndex < static_cast<int>(nodes.size())) {
            TreeNode& parent = nodes[node.parentIndex];

            // Accumulate thickness using pipe model
            float childPow = std::pow(node.thickness, params.thicknessPower);
            float parentPow = std::pow(parent.thickness, params.thicknessPower);
            parent.thickness = std::pow(parentPow + childPow, 1.0f / params.thicknessPower);
        }
    }

    // Clamp maximum thickness
    for (auto& node : nodes) {
        node.thickness = std::min(node.thickness, params.baseThickness);
    }
}

void SpaceColonisationGenerator::buildChildIndices(std::vector<TreeNode>& nodes) {
    // Clear existing child indices
    for (auto& node : nodes) {
        node.childIndices.clear();
    }

    // Build child index lists
    for (size_t i = 0; i < nodes.size(); ++i) {
        int parentIdx = nodes[i].parentIndex;
        if (parentIdx >= 0 && parentIdx < static_cast<int>(nodes.size())) {
            nodes[parentIdx].childIndices.push_back(static_cast<int>(i));
        }
    }
}
