#pragma once

#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace MotionMatching {

// Feature dimension for KD-tree search
// Uses per-component trajectory positions (x,z ground plane) and root velocity
// Direction-aware: stores vector components, not scalar magnitudes
constexpr size_t KD_FEATURE_DIM = 16;  // 6 trajectory samples * 2 (pos_x+pos_z) + 3 root vel + 1 angular vel

// A single point in the KD-tree
struct KDPoint {
    std::array<float, KD_FEATURE_DIM> features;
    size_t poseIndex = 0;

    float& operator[](size_t i) { return features[i]; }
    const float& operator[](size_t i) const { return features[i]; }

    float squaredDistance(const KDPoint& other) const {
        float dist = 0.0f;
        for (size_t i = 0; i < KD_FEATURE_DIM; ++i) {
            float d = features[i] - other.features[i];
            dist += d * d;
        }
        return dist;
    }
};

// Node in the KD-tree
struct KDNode {
    KDPoint point;
    size_t splitDimension = 0;
    int32_t leftChild = -1;   // Index in node array, -1 = leaf
    int32_t rightChild = -1;
};

// Result from KD-tree search
struct KDSearchResult {
    size_t poseIndex = 0;
    float squaredDistance = std::numeric_limits<float>::max();

    bool operator<(const KDSearchResult& other) const {
        return squaredDistance < other.squaredDistance;
    }
};

// KD-tree for efficient nearest neighbor search in motion matching
class MotionKDTree {
public:
    MotionKDTree() = default;

    // Build tree from a set of points
    void build(std::vector<KDPoint> points);

    // Find the K nearest neighbors to a query point
    // Returns results sorted by distance (nearest first)
    std::vector<KDSearchResult> findKNearest(const KDPoint& query, size_t k) const;

    // Find all points within a given radius
    std::vector<KDSearchResult> findWithinRadius(const KDPoint& query, float radius) const;

    // Check if tree is built
    bool isBuilt() const { return !nodes_.empty(); }

    // Get number of points in tree
    size_t size() const { return nodes_.empty() ? 0 : points_.size(); }

    // Clear the tree
    void clear() {
        nodes_.clear();
        points_.clear();
    }

    // Serialization access
    const std::vector<KDNode>& getNodes() const { return nodes_; }
    const std::vector<KDPoint>& getPoints() const { return points_; }
    void setData(std::vector<KDNode> nodes, std::vector<KDPoint> points) {
        nodes_ = std::move(nodes);
        points_ = std::move(points);
    }

private:
    std::vector<KDNode> nodes_;
    std::vector<KDPoint> points_;  // Store original points for rebuilding

    // Recursively build the tree
    int32_t buildRecursive(std::vector<size_t>& indices, size_t begin, size_t end, size_t depth);

    // Find the dimension with highest variance for splitting
    size_t findBestSplitDimension(const std::vector<size_t>& indices, size_t begin, size_t end) const;

    // Recursive k-nearest neighbor search
    void searchKNearestRecursive(int32_t nodeIdx, const KDPoint& query,
                                   size_t k, std::vector<KDSearchResult>& results,
                                   float& maxDist) const;

    // Recursive radius search
    void searchRadiusRecursive(int32_t nodeIdx, const KDPoint& query,
                                float radiusSquared, std::vector<KDSearchResult>& results) const;
};

} // namespace MotionMatching
