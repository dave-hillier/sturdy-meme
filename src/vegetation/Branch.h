#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>

// Represents a single branch in a tree's hierarchical structure
// Branches form a tree structure where each branch can have multiple children
class Branch {
public:
    struct Properties {
        float length = 1.0f;
        float startRadius = 0.1f;
        float endRadius = 0.05f;
        int level = 0;              // 0 = trunk, higher = smaller branches
        int radialSegments = 6;     // Segments around circumference
        int lengthSegments = 4;     // Segments along length
    };

    Branch() = default;

    // Construction with position and orientation
    Branch(const glm::vec3& start, const glm::quat& orientation, const Properties& props)
        : startPosition(start)
        , orientation(orientation)
        , properties(props)
    {
        calculateEndPosition();
    }

    // Accessors
    const glm::vec3& getStartPosition() const { return startPosition; }
    const glm::vec3& getEndPosition() const { return endPosition; }
    const glm::quat& getOrientation() const { return orientation; }
    glm::vec3 getDirection() const { return orientation * glm::vec3(0.0f, 1.0f, 0.0f); }

    const Properties& getProperties() const { return properties; }
    Properties& getProperties() { return properties; }

    int getLevel() const { return properties.level; }
    float getLength() const { return properties.length; }
    float getStartRadius() const { return properties.startRadius; }
    float getEndRadius() const { return properties.endRadius; }

    // Hierarchy accessors
    const std::vector<Branch>& getChildren() const { return children; }
    std::vector<Branch>& getChildren() { return children; }
    bool isTerminal() const { return children.empty(); }

    // Mutators
    void setStartPosition(const glm::vec3& pos) {
        startPosition = pos;
        calculateEndPosition();
    }

    void setOrientation(const glm::quat& orient) {
        orientation = orient;
        calculateEndPosition();
    }

    void setProperties(const Properties& props) {
        properties = props;
        calculateEndPosition();
    }

    // Add a child branch
    Branch& addChild(const glm::vec3& start, const glm::quat& orient, const Properties& props) {
        children.emplace_back(start, orient, props);
        return children.back();
    }

    void addChild(Branch&& child) {
        children.push_back(std::move(child));
    }

    // Get interpolated position along the branch (t: 0-1)
    glm::vec3 getPositionAt(float t) const {
        return glm::mix(startPosition, endPosition, t);
    }

    // Get interpolated radius along the branch (t: 0-1)
    float getRadiusAt(float t) const {
        return glm::mix(properties.startRadius, properties.endRadius, t);
    }

    // Count total branches in subtree (including this one)
    size_t countBranches() const {
        size_t count = 1;
        for (const auto& child : children) {
            count += child.countBranches();
        }
        return count;
    }

    // Get maximum depth of subtree
    int getMaxDepth() const {
        int maxChildDepth = 0;
        for (const auto& child : children) {
            maxChildDepth = std::max(maxChildDepth, child.getMaxDepth());
        }
        return properties.level + 1 + maxChildDepth;
    }

private:
    void calculateEndPosition() {
        glm::vec3 direction = orientation * glm::vec3(0.0f, 1.0f, 0.0f);
        endPosition = startPosition + direction * properties.length;
    }

    glm::vec3 startPosition{0.0f};
    glm::vec3 endPosition{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity quaternion
    Properties properties;
    std::vector<Branch> children;
};
