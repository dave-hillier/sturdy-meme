#pragma once

#include "TreeParameters.h"
#include "TreeGeometry.h"
#include "VolumeGenerator.h"
#include <vector>
#include <random>

// Space colonisation algorithm implementation
class SpaceColonisationGenerator {
public:
    explicit SpaceColonisationGenerator(std::mt19937& rng);

    // Generate tree structure using space colonisation
    void generate(const TreeParameters& params, std::vector<TreeNode>& outNodes);

    // Calculate branch thicknesses using pipe model
    static void calculateBranchThickness(std::vector<TreeNode>& nodes,
                                         const SpaceColonisationParams& params);

    // Build child index lists for nodes
    static void buildChildIndices(std::vector<TreeNode>& nodes);

private:
    // Run single space colonisation iteration
    bool spaceColonisationStep(std::vector<TreeNode>& nodes,
                               std::vector<glm::vec3>& attractionPoints,
                               const SpaceColonisationParams& params,
                               const glm::vec3& tropismDir,
                               float tropismStrength);

    std::mt19937& rng;
    VolumeGenerator volumeGen;
};
