#pragma once

#include "../core/HierarchicalPose.h"
#include "../core/NodeMask.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <cstdint>

// Forward declaration
struct BranchData;
struct TreeMeshData;

// A single branch in the tree skeleton hierarchy.
// Mirrors the Joint structure from skeletal animation.
struct TreeBranch {
    std::string name;               // e.g., "trunk", "branch_1_0", "branch_2_3"
    int32_t parentIndex;            // -1 for root (trunk)
    glm::mat4 restPoseLocal;        // Local transform in rest pose (relative to parent)
    float radius;                   // Branch radius at this joint
    float length;                   // Branch length
    int level;                      // Branch level (0=trunk, 1=primary, 2=secondary, etc.)

    // Create identity branch
    static TreeBranch identity(const std::string& name, int32_t parent, int level) {
        TreeBranch branch;
        branch.name = name;
        branch.parentIndex = parent;
        branch.restPoseLocal = glm::mat4(1.0f);
        branch.radius = 0.1f;
        branch.length = 1.0f;
        branch.level = level;
        return branch;
    }
};

// Complete tree skeleton - hierarchical representation of all branches.
// Enables skeletal-animation-style operations on trees.
struct TreeSkeleton {
    std::vector<TreeBranch> branches;

    // Build skeleton from TreeMeshData branch hierarchy
    static TreeSkeleton fromTreeMeshData(const TreeMeshData& meshData);

    // Get number of branches
    size_t size() const { return branches.size(); }
    bool empty() const { return branches.empty(); }

    // Access branches
    TreeBranch& operator[](size_t i) { return branches[i]; }
    const TreeBranch& operator[](size_t i) const { return branches[i]; }

    // Find branch by name (returns -1 if not found)
    int32_t findBranchIndex(const std::string& name) const;

    // Get depths of all branches (for NodeMask::fromDepthRange)
    std::vector<int> getBranchDepths() const;

    // Get parent indices (for NodeMask::fromSubtree)
    std::vector<int32_t> getParentIndices() const;

    // Get indices of branches at a specific level
    std::vector<size_t> getBranchesAtLevel(int level) const;

    // Get indices of all leaf-bearing branches (highest level or marked as leaf branches)
    std::vector<size_t> getLeafBranches() const;

    // Create a rest pose (all identity transforms)
    HierarchyPose getRestPose() const;

    // Create masks for common tree parts
    NodeMask trunkMask() const;           // Level 0 only
    NodeMask primaryBranchesMask() const; // Level 1 only
    NodeMask outerBranchesMask() const;   // Levels 2+
    NodeMask allBranchesMask() const;     // All branches

    // Create mask based on level range [minLevel, maxLevel]
    NodeMask levelRangeMask(int minLevel, int maxLevel) const;

    // Create flexibility mask (higher weight for outer branches)
    // Useful for wind animation - outer branches flex more
    NodeMask flexibilityMask() const;
};
