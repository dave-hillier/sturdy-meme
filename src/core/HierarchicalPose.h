#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

// Domain-agnostic pose representation for any hierarchical structure.
// Used by both skeletal animation (bones) and tree animation (branches).
// Uses T/R/S decomposition for clean blending.
struct NodePose {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    // Convert to matrix (T * R * S)
    glm::mat4 toMatrix() const;

    // Convert to matrix with pre-rotation (T * Rpre * R * S)
    glm::mat4 toMatrix(const glm::quat& preRotation) const;

    // Create from matrix (assumes T * R * S decomposition)
    static NodePose fromMatrix(const glm::mat4& matrix);

    // Create from matrix, extracting the animated rotation (removing preRotation)
    // Matrix format: T * Rpre * R * S
    static NodePose fromMatrix(const glm::mat4& matrix, const glm::quat& preRotation);

    // Identity pose
    static NodePose identity() {
        return NodePose{glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)};
    }

    // Equality comparison
    bool operator==(const NodePose& other) const {
        return translation == other.translation &&
               rotation == other.rotation &&
               scale == other.scale;
    }

    bool operator!=(const NodePose& other) const {
        return !(*this == other);
    }
};

// Full hierarchy pose (all nodes in a tree/skeleton)
struct HierarchyPose {
    std::vector<NodePose> nodePoses;

    void resize(size_t count) { nodePoses.resize(count); }
    size_t size() const { return nodePoses.size(); }
    bool empty() const { return nodePoses.empty(); }

    NodePose& operator[](size_t i) { return nodePoses[i]; }
    const NodePose& operator[](size_t i) const { return nodePoses[i]; }

    // Iterator support
    auto begin() { return nodePoses.begin(); }
    auto end() { return nodePoses.end(); }
    auto begin() const { return nodePoses.begin(); }
    auto end() const { return nodePoses.end(); }

    // Clear all poses
    void clear() { nodePoses.clear(); }

    // Reserve capacity
    void reserve(size_t count) { nodePoses.reserve(count); }

    // Add a pose
    void push_back(const NodePose& pose) { nodePoses.push_back(pose); }
};
