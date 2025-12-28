#pragma once

#include "TreeGenerator.h"
#include "TreeOptions.h"
#include <vector>

/**
 * Generates simplified LOD meshes from a full-detail TreeMeshData.
 *
 * Rather than regenerating trees with different parameters (which produces
 * different tree shapes), this simplifies the existing structure:
 * - Prunes branches below a radius threshold
 * - Reduces section count per branch
 * - Places fewer, larger leaves on the remaining branches
 *
 * This ensures consistent tree silhouette across LOD levels.
 */
class TreeLODMeshGenerator {
public:
    struct LODConfig {
        // Branch simplification
        float minBranchRadius = 0.0f;     // Prune branches thinner than this
        int maxBranchLevel = 999;          // Prune branches deeper than this level
        int sectionReduction = 1;          // Divide section count by this (1 = no reduction)

        // Leaf simplification
        float leafDensity = 1.0f;          // Fraction of leaves to keep (0.5 = half)
        float leafScale = 1.0f;            // Size multiplier for remaining leaves

        // Default configs for each LOD level
        static LODConfig fullDetail() {
            return LODConfig{};  // Keep everything
        }

        static LODConfig mediumDetail() {
            LODConfig cfg;
            cfg.minBranchRadius = 0.02f;   // Prune very thin branches
            cfg.maxBranchLevel = 2;         // Keep trunk + 2 levels of branches
            cfg.sectionReduction = 2;       // Half the sections per branch
            cfg.leafDensity = 0.5f;         // Half the leaves
            cfg.leafScale = 1.5f;           // 1.5x larger to compensate
            return cfg;
        }
    };

    /**
     * Simplify a full-detail tree mesh according to LOD config.
     *
     * @param fullDetail The complete tree mesh data
     * @param options Original tree options (for leaf generation parameters)
     * @param config LOD simplification settings
     * @return Simplified mesh data with branches and leaves
     */
    static TreeMeshData simplify(const TreeMeshData& fullDetail,
                                  const TreeOptions& options,
                                  const LODConfig& config);

private:
    /**
     * Filter and simplify branches according to config.
     * Returns indices of branches that passed the filter (for leaf placement).
     */
    static std::vector<BranchData> simplifyBranches(
        const std::vector<BranchData>& branches,
        const LODConfig& config,
        std::vector<size_t>& keptBranchIndices);

    /**
     * Generate leaves for the simplified branch set.
     * Places leaves only on branches that exist in the simplified mesh.
     */
    static std::vector<LeafData> generateLeavesForLOD(
        const std::vector<BranchData>& simplifiedBranches,
        const TreeOptions& options,
        const LODConfig& config,
        uint32_t seed);
};
