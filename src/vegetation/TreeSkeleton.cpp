#include "TreeSkeleton.h"
#include "TreeGenerator.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <sstream>

TreeSkeleton TreeSkeleton::fromTreeMeshData(const TreeMeshData& meshData) {
    // Delegate to the generateSkeleton method on TreeMeshData
    return meshData.generateSkeleton();
}

int32_t TreeSkeleton::findBranchIndex(const std::string& name) const {
    for (size_t i = 0; i < branches.size(); ++i) {
        if (branches[i].name == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

std::vector<int> TreeSkeleton::getBranchDepths() const {
    std::vector<int> depths;
    depths.reserve(branches.size());
    for (const auto& branch : branches) {
        depths.push_back(branch.level);
    }
    return depths;
}

std::vector<int32_t> TreeSkeleton::getParentIndices() const {
    std::vector<int32_t> parents;
    parents.reserve(branches.size());
    for (const auto& branch : branches) {
        parents.push_back(branch.parentIndex);
    }
    return parents;
}

std::vector<size_t> TreeSkeleton::getBranchesAtLevel(int level) const {
    std::vector<size_t> result;
    for (size_t i = 0; i < branches.size(); ++i) {
        if (branches[i].level == level) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<size_t> TreeSkeleton::getLeafBranches() const {
    // Find the maximum level
    int maxLevel = 0;
    for (const auto& branch : branches) {
        maxLevel = std::max(maxLevel, branch.level);
    }

    // Return branches at maximum level (they bear leaves)
    return getBranchesAtLevel(maxLevel);
}

HierarchyPose TreeSkeleton::getRestPose() const {
    HierarchyPose pose;
    pose.resize(branches.size());

    for (size_t i = 0; i < branches.size(); ++i) {
        pose[i] = NodePose::fromMatrix(branches[i].restPoseLocal);
    }

    return pose;
}

NodeMask TreeSkeleton::trunkMask() const {
    return levelRangeMask(0, 0);
}

NodeMask TreeSkeleton::primaryBranchesMask() const {
    return levelRangeMask(1, 1);
}

NodeMask TreeSkeleton::outerBranchesMask() const {
    // Find max level
    int maxLevel = 0;
    for (const auto& branch : branches) {
        maxLevel = std::max(maxLevel, branch.level);
    }
    return levelRangeMask(2, maxLevel);
}

NodeMask TreeSkeleton::allBranchesMask() const {
    return NodeMask(branches.size(), 1.0f);
}

NodeMask TreeSkeleton::levelRangeMask(int minLevel, int maxLevel) const {
    std::vector<int> depths = getBranchDepths();
    return NodeMask::fromDepthRange(branches.size(), depths, minLevel, maxLevel);
}

NodeMask TreeSkeleton::flexibilityMask() const {
    // Find max level
    int maxLevel = 0;
    for (const auto& branch : branches) {
        maxLevel = std::max(maxLevel, branch.level);
    }

    // Create mask where weight increases with level
    // Level 0 = 0.0 (trunk doesn't flex)
    // Max level = 1.0 (outer branches flex most)
    return NodeMask::fromPredicate(branches.size(), [this, maxLevel](size_t i) {
        if (maxLevel == 0) return 0.0f;
        return static_cast<float>(branches[i].level) / static_cast<float>(maxLevel);
    });
}
