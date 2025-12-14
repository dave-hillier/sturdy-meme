#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

// Represents a branch segment for building the tree
struct BranchSegment {
    glm::vec3 startPos;
    glm::vec3 endPos;
    glm::quat orientation;
    float startRadius;
    float endRadius;
    int level;              // Branch depth level (0 = trunk)
    int parentIndex;        // Index of parent segment (-1 for trunk base)
};

// Leaf billboard data
struct LeafInstance {
    glm::vec3 position;
    glm::vec3 normal;
    float size;
    float rotation;         // Rotation around normal
};

// Node for space colonisation algorithm
struct TreeNode {
    glm::vec3 position;
    int parentIndex;        // -1 for root
    int childCount;         // Number of children (for thickness calculation)
    float thickness;        // Calculated branch thickness
    bool isTerminal;        // Is this a leaf node?
    int depth;              // Depth from root (for level calculation)
    std::vector<int> childIndices;  // Indices of child nodes
};
