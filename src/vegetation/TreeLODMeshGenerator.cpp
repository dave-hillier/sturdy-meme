#include "TreeLODMeshGenerator.h"
#include <algorithm>
#include <cmath>
#include <SDL3/SDL_log.h>

TreeMeshData TreeLODMeshGenerator::simplify(const TreeMeshData& fullDetail,
                                             const TreeOptions& options,
                                             const LODConfig& config) {
    TreeMeshData result;

    // Simplify branches
    std::vector<size_t> keptBranchIndices;
    result.branches = simplifyBranches(fullDetail.branches, config, keptBranchIndices);

    // Generate leaves for the simplified branches
    // Use a fixed seed derived from tree options for consistency
    uint32_t seed = static_cast<uint32_t>(options.seed);
    result.leaves = generateLeavesForLOD(result.branches, options, config, seed);

    SDL_Log("TreeLODMeshGenerator: Simplified %zu branches to %zu, %zu leaves to %zu",
            fullDetail.branches.size(), result.branches.size(),
            fullDetail.leaves.size(), result.leaves.size());

    return result;
}

std::vector<BranchData> TreeLODMeshGenerator::simplifyBranches(
    const std::vector<BranchData>& branches,
    const LODConfig& config,
    std::vector<size_t>& keptBranchIndices) {

    std::vector<BranchData> result;
    keptBranchIndices.clear();

    for (size_t i = 0; i < branches.size(); ++i) {
        const auto& branch = branches[i];

        // Filter by level
        if (branch.level > config.maxBranchLevel) {
            continue;
        }

        // Filter by radius
        if (branch.radius < config.minBranchRadius) {
            continue;
        }

        // Keep this branch, but simplify its sections
        BranchData simplified = branch;

        if (config.sectionReduction > 1 && simplified.sections.size() > 2) {
            // Reduce section count while preserving first and last
            std::vector<SectionData> reducedSections;
            size_t originalCount = simplified.sections.size();
            size_t step = static_cast<size_t>(config.sectionReduction);

            for (size_t s = 0; s < originalCount; s += step) {
                reducedSections.push_back(simplified.sections[s]);
            }

            // Always include the last section for proper branch endpoint
            if (reducedSections.back().origin != simplified.sections.back().origin) {
                reducedSections.push_back(simplified.sections.back());
            }

            simplified.sections = std::move(reducedSections);
            simplified.sectionCount = static_cast<int>(simplified.sections.size());
        }

        result.push_back(std::move(simplified));
        keptBranchIndices.push_back(i);
    }

    return result;
}

std::vector<LeafData> TreeLODMeshGenerator::generateLeavesForLOD(
    const std::vector<BranchData>& simplifiedBranches,
    const TreeOptions& options,
    const LODConfig& config,
    uint32_t seed) {

    std::vector<LeafData> result;

    if (simplifiedBranches.empty() || config.leafDensity <= 0.0f) {
        return result;
    }

    // Use RNG for consistent leaf placement
    TreeRNG rng(seed + 12345);  // Offset seed to get different sequence than branch generation

    // Find the final-level branches (where leaves are placed)
    int maxLevel = 0;
    for (const auto& branch : simplifiedBranches) {
        maxLevel = std::max(maxLevel, branch.level);
    }

    // Calculate how many leaves to place based on density
    int targetLeafCount = static_cast<int>(options.leaves.count * config.leafDensity);
    if (targetLeafCount <= 0) {
        return result;
    }

    // Collect final-level branches for leaf placement
    std::vector<const BranchData*> leafBranches;
    for (const auto& branch : simplifiedBranches) {
        if (branch.level == maxLevel) {
            leafBranches.push_back(&branch);
        }
    }

    if (leafBranches.empty()) {
        // If no final-level branches, use all branches
        for (const auto& branch : simplifiedBranches) {
            leafBranches.push_back(&branch);
        }
    }

    // Distribute leaves across available branches
    int leavesPerBranch = std::max(1, targetLeafCount / static_cast<int>(leafBranches.size()));
    int remainingLeaves = targetLeafCount;

    for (const auto* branch : leafBranches) {
        if (remainingLeaves <= 0) break;
        if (branch->sections.size() < 2) continue;

        int leavesForThisBranch = std::min(leavesPerBranch, remainingLeaves);

        for (int i = 0; i < leavesForThisBranch; ++i) {
            // Position along the branch (biased toward the end based on leaves.start)
            float t = rng.random(1.0f, options.leaves.start);
            int sectionIdx = static_cast<int>(t * (branch->sections.size() - 1));
            sectionIdx = std::clamp(sectionIdx, 0, static_cast<int>(branch->sections.size()) - 1);

            const SectionData& section = branch->sections[sectionIdx];

            // Random offset from branch center
            float angle = rng.random(2.0f * 3.14159f);
            float radialDist = section.radius * (1.0f + rng.random(0.5f));
            glm::vec3 offset(
                std::cos(angle) * radialDist,
                0.0f,
                std::sin(angle) * radialDist
            );

            // Rotate offset by section orientation
            glm::vec3 worldOffset = section.orientation * offset;
            glm::vec3 leafPos = section.origin + worldOffset;

            // Random leaf orientation
            float yaw = rng.random(2.0f * 3.14159f);
            float pitch = rng.random(0.3f, -0.3f);  // Slight tilt
            glm::quat leafOrientation = glm::angleAxis(yaw, glm::vec3(0, 1, 0)) *
                                        glm::angleAxis(pitch, glm::vec3(1, 0, 0));

            // Scale leaf size (size +/- sizeVariance)
            float variance = options.leaves.size * options.leaves.sizeVariance;
            float minSize = options.leaves.size - variance;
            float maxSize = options.leaves.size + variance;
            float baseSize = rng.random(maxSize, minSize);
            float scaledSize = baseSize * config.leafScale;

            LeafData leaf;
            leaf.position = leafPos;
            leaf.orientation = leafOrientation;
            leaf.size = scaledSize;
            result.push_back(leaf);

            --remainingLeaves;
        }
    }

    return result;
}
