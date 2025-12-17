#pragma once

#include "Branch.h"
#include "TreeGeometry.h"
#include <glm/glm.hpp>
#include <vector>
#include <optional>

// Represents the complete structure of a tree
// Contains the hierarchical branch structure and optional leaf data
class TreeStructure {
public:
    TreeStructure() = default;

    // Access the root branch (trunk)
    Branch& getRoot() { return root; }
    const Branch& getRoot() const { return root; }

    // Set/replace the root branch
    void setRoot(Branch&& newRoot) { root = std::move(newRoot); }
    void setRoot(const Branch& newRoot) { root = newRoot; }

    // Leaf instances
    std::vector<LeafInstance>& getLeaves() { return leaves; }
    const std::vector<LeafInstance>& getLeaves() const { return leaves; }

    void addLeaf(const LeafInstance& leaf) { leaves.push_back(leaf); }
    void clearLeaves() { leaves.clear(); }

    // Statistics
    size_t getTotalBranchCount() const { return root.countBranches(); }
    int getMaxDepth() const { return root.getMaxDepth(); }
    size_t getLeafCount() const { return leaves.size(); }

    // Calculate bounding box
    struct BoundingBox {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    BoundingBox calculateBounds() const {
        BoundingBox bounds;
        bounds.min = glm::vec3(std::numeric_limits<float>::max());
        bounds.max = glm::vec3(std::numeric_limits<float>::lowest());
        calculateBoundsRecursive(root, bounds);
        return bounds;
    }

    float getApproximateHeight() const {
        auto bounds = calculateBounds();
        return bounds.max.y - bounds.min.y;
    }

    glm::vec3 getCenter() const {
        auto bounds = calculateBounds();
        return (bounds.min + bounds.max) * 0.5f;
    }

    // Iteration helpers - visit all branches
    template<typename Func>
    void forEachBranch(Func&& func) const {
        forEachBranchRecursive(root, std::forward<Func>(func));
    }

    template<typename Func>
    void forEachBranch(Func&& func) {
        forEachBranchRecursive(root, std::forward<Func>(func));
    }

    // Flatten to BranchSegment list (for compatibility with existing geometry generation)
    std::vector<BranchSegment> flattenToSegments() const {
        std::vector<BranchSegment> segments;
        flattenToSegmentsRecursive(root, -1, segments);
        return segments;
    }

private:
    void calculateBoundsRecursive(const Branch& branch, BoundingBox& bounds) const {
        // Include start and end of this branch
        bounds.min = glm::min(bounds.min, branch.getStartPosition());
        bounds.max = glm::max(bounds.max, branch.getStartPosition());
        bounds.min = glm::min(bounds.min, branch.getEndPosition());
        bounds.max = glm::max(bounds.max, branch.getEndPosition());

        // Include children
        for (const auto& child : branch.getChildren()) {
            calculateBoundsRecursive(child, bounds);
        }
    }

    template<typename Func>
    void forEachBranchRecursive(const Branch& branch, Func&& func) const {
        func(branch);
        for (const auto& child : branch.getChildren()) {
            forEachBranchRecursive(child, std::forward<Func>(func));
        }
    }

    template<typename Func>
    void forEachBranchRecursive(Branch& branch, Func&& func) {
        func(branch);
        for (auto& child : branch.getChildren()) {
            forEachBranchRecursive(child, std::forward<Func>(func));
        }
    }

    void flattenToSegmentsRecursive(const Branch& branch, int parentIdx,
                                     std::vector<BranchSegment>& segments) const {
        BranchSegment seg;
        seg.startPos = branch.getStartPosition();
        seg.endPos = branch.getEndPosition();
        seg.orientation = branch.getOrientation();
        seg.startRadius = branch.getStartRadius();
        seg.endRadius = branch.getEndRadius();
        seg.level = branch.getLevel();
        seg.parentIndex = parentIdx;

        int myIdx = static_cast<int>(segments.size());
        segments.push_back(seg);

        for (const auto& child : branch.getChildren()) {
            flattenToSegmentsRecursive(child, myIdx, segments);
        }
    }

    Branch root;
    std::vector<LeafInstance> leaves;
};
